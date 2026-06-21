#include "dev_log_server.hpp"

#include <cpp-httplib/httplib.h>

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_log.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace dusk::dev_log {
namespace {

constexpr int kPort = 8081;
constexpr std::size_t kMaxEntries = 3000;

struct Entry {
    std::uint64_t seq = 0;
    std::int64_t epochMs = 0;
    int level = 1;
    std::string module;
    std::string message;
};

std::mutex gMutex;
std::deque<Entry> gBuffer;
std::uint64_t gNextSeq = 1;

std::unique_ptr<httplib::Server> gServer;
std::thread gThread;

// Previous SDL log sink, kept so SDL output still reaches the console.
SDL_LogOutputFunction gPrevSdlLog = nullptr;
void* gPrevSdlLogUserdata = nullptr;

std::int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch())
        .count();
}

void append_json_escaped(std::string& out, const std::string& value) {
    for (const char c : value) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            } else {
                out += c;
            }
            break;
        }
    }
}

std::string entry_to_json(const Entry& entry) {
    std::string out;
    out.reserve(entry.message.size() + entry.module.size() + 64);
    out += "{\"seq\":";
    out += std::to_string(entry.seq);
    out += ",\"t\":";
    out += std::to_string(entry.epochMs);
    out += ",\"lvl\":";
    out += std::to_string(entry.level);
    out += ",\"mod\":\"";
    append_json_escaped(out, entry.module);
    out += "\",\"msg\":\"";
    append_json_escaped(out, entry.message);
    out += "\"}";
    return out;
}

constexpr const char* kViewerHtml = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8" />
<meta name="viewport" content="width=device-width, initial-scale=1" />
<title>Dusklight &mdash; Logs</title>
<style>
  :root { color-scheme: dark; }
  * { box-sizing: border-box; }
  body { margin: 0; font-family: -apple-system, system-ui, sans-serif;
         background: #14161c; color: #e8e8ec; }
  header { position: sticky; top: 0; background: #1b1e27; padding: 0.6rem 0.9rem;
           display: flex; gap: 0.6rem; flex-wrap: wrap; align-items: center;
           border-bottom: 1px solid #2a2f3c; }
  header h1 { font-size: 1rem; margin: 0 0.4rem 0 0; font-weight: 600; }
  select, input, button { background: #14161c; color: #e8e8ec; border: 1px solid #3a3f4d;
           border-radius: 0.35rem; padding: 0.3rem 0.5rem; font-size: 0.85rem; }
  button { cursor: pointer; }
  #status { font-size: 0.8rem; color: #9aa0ad; margin-left: auto; }
  #log { font-family: ui-monospace, Menlo, monospace; font-size: 0.82rem;
         padding: 0.5rem 0.9rem; }
  .row { padding: 1px 0; white-space: pre-wrap; word-break: break-word; }
  .row .t { color: #6b7180; }
  .row .m { color: #7d8694; }
  .lvl0 { color: #8a8f9c; }
  .lvl1 { color: #cdd2dc; }
  .lvl2 { color: #ffd479; }
  .lvl3 { color: #ff6b6b; }
  .lvl4 { color: #fff; background: #5a1d1d; }
  .hidden { display: none; }
</style>
</head>
<body>
<header>
  <h1>Dusklight logs</h1>
  <label>Level <select id="level">
    <option value="0">debug+</option><option value="1" selected>info+</option>
    <option value="2">warning+</option><option value="3">error+</option>
  </select></label>
  <label>Channel <select id="channel"><option value="">all</option></select></label>
  <input id="search" placeholder="filter text" size="16" />
  <label><input type="checkbox" id="follow" checked /> follow</label>
  <button id="clear">clear</button>
  <span id="status">connecting...</span>
</header>
<div id="log"></div>
<script>
  var rows = [];
  var channels = {};
  var logEl = document.getElementById('log');
  var levelEl = document.getElementById('level');
  var channelEl = document.getElementById('channel');
  var searchEl = document.getElementById('search');
  var followEl = document.getElementById('follow');
  var statusEl = document.getElementById('status');
  var names = ['DEBUG', 'INFO', 'WARN', 'ERROR', 'FATAL'];

  function visible(r) {
    if (r.lvl < +levelEl.value) return false;
    if (channelEl.value && r.mod !== channelEl.value) return false;
    var s = searchEl.value.toLowerCase();
    if (s && (r.mod + ' ' + r.msg).toLowerCase().indexOf(s) < 0) return false;
    return true;
  }
  function fmtTime(ms) {
    var d = new Date(ms);
    return d.toTimeString().slice(0, 8) + '.' +
      ('00' + d.getMilliseconds()).slice(-3);
  }
  function render(r) {
    var div = document.createElement('div');
    div.className = 'row lvl' + r.lvl + (visible(r) ? '' : ' hidden');
    div.innerHTML = '<span class="t">' + fmtTime(r.t) + '</span> ' +
      names[r.lvl] + ' <span class="m">[' + r.mod + ']</span> ';
    div.appendChild(document.createTextNode(r.msg));
    r.el = div;
    logEl.appendChild(div);
  }
  function refilter() {
    rows.forEach(function (r) {
      r.el.classList.toggle('hidden', !visible(r));
    });
  }
  function add(r) {
    rows.push(r);
    if (!channels[r.mod]) {
      channels[r.mod] = true;
      var o = document.createElement('option');
      o.value = o.textContent = r.mod;
      channelEl.appendChild(o);
    }
    render(r);
    while (rows.length > 4000) {
      var old = rows.shift();
      if (old.el) old.el.remove();
    }
    if (followEl.checked) window.scrollTo(0, document.body.scrollHeight);
  }
  levelEl.onchange = channelEl.onchange = refilter;
  searchEl.oninput = refilter;
  document.getElementById('clear').onclick = function () {
    rows = []; logEl.innerHTML = '';
  };

  var es = new EventSource('/events');
  es.onopen = function () { statusEl.textContent = 'live'; };
  es.onerror = function () { statusEl.textContent = 'reconnecting...'; };
  es.onmessage = function (e) {
    try { add(JSON.parse(e.data)); } catch (err) {}
  };
</script>
</body>
</html>)HTML";

void handle_events(const httplib::Request& req, httplib::Response& res) {
    std::uint64_t since = 0;
    if (req.has_param("since")) {
        since = std::strtoull(req.get_param_value("since").c_str(), nullptr, 10);
    }
    res.set_header("Cache-Control", "no-cache");
    res.set_chunked_content_provider(
        "text/event-stream",
        [since](std::size_t /*offset*/, httplib::DataSink& sink) mutable {
            std::string out;
            {
                std::lock_guard lock(gMutex);
                for (const auto& entry : gBuffer) {
                    if (entry.seq > since) {
                        out += "data: ";
                        out += entry_to_json(entry);
                        out += "\n\n";
                        since = entry.seq;
                    }
                }
            }
            if (out.empty()) {
                out = ": keepalive\n\n";  // also detects a dropped client
            }
            if (!sink.write(out.data(), out.size())) {
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            return true;
        });
}

void run() {
    gServer->Get("/", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(kViewerHtml, "text/html; charset=utf-8");
    });
    gServer->Get("/events", handle_events);
    gServer->set_read_timeout(5, 0);

    int port = kPort;
    if (!gServer->bind_to_port("0.0.0.0", kPort)) {
        port = gServer->bind_to_any_port("0.0.0.0");
    }
    if (port <= 0) {
        std::fprintf(stderr, "[dev_log] could not bind a log-server port\n");
        return;
    }
    std::fprintf(stderr, "[dev_log] log server listening on port %d\n", port);
    gServer->listen_after_bind();
}

int sdl_priority_to_level(SDL_LogPriority priority) {
    switch (priority) {
    case SDL_LOG_PRIORITY_WARN:
        return 2;
    case SDL_LOG_PRIORITY_ERROR:
        return 3;
    case SDL_LOG_PRIORITY_CRITICAL:
        return 4;
    case SDL_LOG_PRIORITY_INFO:
        return 1;
    default:
        return 0;
    }
}

void sdl_log_sink(
    void* userdata, int category, SDL_LogPriority priority, const char* message) {
    record(sdl_priority_to_level(priority), "sdl", message,
        message != nullptr ? static_cast<unsigned int>(std::strlen(message)) : 0);
    if (gPrevSdlLog != nullptr) {
        gPrevSdlLog(gPrevSdlLogUserdata, category, priority, message);
    }
}

}  // namespace

void record(int level, const char* module, const char* message, unsigned int len) {
    Entry entry;
    entry.epochMs = now_ms();
    entry.level = level;
    entry.module = module != nullptr ? module : "";
    entry.message.assign(message != nullptr ? message : "", message != nullptr ? len : 0);

    std::lock_guard lock(gMutex);
    entry.seq = gNextSeq++;
    gBuffer.push_back(std::move(entry));
    while (gBuffer.size() > kMaxEntries) {
        gBuffer.pop_front();
    }
}

void note_sdl_event(const SDL_Event& event) {
    // Crossing this magnitude counts as a stick/trigger "press" for logging,
    // so a held axis is logged once rather than every frame.
    constexpr int kAxisZone = 8000;
    static int sAxisZone[SDL_GAMEPAD_AXIS_COUNT] = {};

    char buffer[256];
    int len = 0;

    switch (event.type) {
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP: {
        const char* name = SDL_GetGamepadStringForButton(
            static_cast<SDL_GamepadButton>(event.gbutton.button));
        len = std::snprintf(buffer, sizeof(buffer), "button %s %s",
            name != nullptr ? name : "?", event.gbutton.down ? "down" : "up");
        break;
    }
    case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
        const int axis = event.gaxis.axis;
        if (axis < 0 || axis >= SDL_GAMEPAD_AXIS_COUNT) {
            return;
        }
        const int value = event.gaxis.value;
        const int zone = value > kAxisZone ? 1 : (value < -kAxisZone ? -1 : 0);
        if (zone == sAxisZone[axis]) {
            return;  // no threshold crossing; skip to avoid per-frame spam
        }
        sAxisZone[axis] = zone;
        const char* name =
            SDL_GetGamepadStringForAxis(static_cast<SDL_GamepadAxis>(axis));
        len = std::snprintf(buffer, sizeof(buffer), "axis %s %d",
            name != nullptr ? name : "?", value);
        break;
    }
    case SDL_EVENT_GAMEPAD_ADDED:
        len = std::snprintf(buffer, sizeof(buffer), "gamepad added (id %u)",
            static_cast<unsigned>(event.gdevice.which));
        break;
    case SDL_EVENT_GAMEPAD_REMOVED:
        len = std::snprintf(buffer, sizeof(buffer), "gamepad removed (id %u)",
            static_cast<unsigned>(event.gdevice.which));
        break;
    default:
        return;
    }

    if (len > 0) {
        record(1, "dusk.input", buffer, static_cast<unsigned int>(len));
    }
}

void start() {
    if (gServer != nullptr) {
        return;
    }
    SDL_GetLogOutputFunction(&gPrevSdlLog, &gPrevSdlLogUserdata);
    SDL_SetLogOutputFunction(&sdl_log_sink, nullptr);

    gServer = std::make_unique<httplib::Server>();
    gThread = std::thread(&run);

    // Ensure the server thread is joined before static teardown, so a
    // joinable std::thread is never destroyed (which would terminate).
    static bool atexitRegistered = false;
    if (!atexitRegistered) {
        atexitRegistered = true;
        std::atexit([] { stop(); });
    }
}

void stop() {
    if (gServer != nullptr) {
        gServer->stop();
    }
    if (gThread.joinable()) {
        gThread.join();
    }
    gServer.reset();
}

}  // namespace dusk::dev_log
