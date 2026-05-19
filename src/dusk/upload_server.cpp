#include "upload_server.hpp"

#include <cpp-httplib/httplib.h>

#include <aurora/lib/logging.hpp>

#include <atomic>
#include <fstream>
#include <mutex>
#include <system_error>
#include <thread>
#include <utility>

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>

namespace dusk {
namespace {
aurora::Module Log{"dusk::upload_server"};

// Preferred listening port. Falls back to an OS-assigned port if taken.
constexpr int kPreferredPort = 8080;

// The single page served to a browser. The file is POSTed as a raw request
// body (not multipart form data) so the server can stream it straight to
// disk without buffering a multi-gigabyte disc image in memory.
constexpr const char* kUploadPageHtml = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8" />
<meta name="viewport" content="width=device-width, initial-scale=1" />
<title>Dusklight &mdash; Upload Disc Image</title>
<style>
  :root { color-scheme: dark; }
  body {
    margin: 0; min-height: 100vh; display: flex; align-items: center;
    justify-content: center; font-family: -apple-system, system-ui, sans-serif;
    background: #14161c; color: #e8e8ec;
  }
  main {
    width: 100%; max-width: 30rem; padding: 2rem 1.5rem; box-sizing: border-box;
  }
  h1 { font-size: 1.4rem; margin: 0 0 0.25rem; }
  p.sub { margin: 0 0 1.5rem; color: #9aa0ad; font-size: 0.95rem; }
  label.pick {
    display: block; padding: 1.5rem 1rem; text-align: center; cursor: pointer;
    border: 2px dashed #3a3f4d; border-radius: 0.75rem; color: #c5c9d3;
    transition: border-color 0.15s, background 0.15s;
  }
  label.pick:hover { border-color: #5b8cff; background: #1b1e27; }
  input[type=file] { display: none; }
  #file { margin: 1rem 0 0; font-size: 0.9rem; color: #9aa0ad; word-break: break-all; }
  button {
    margin-top: 1.25rem; width: 100%; padding: 0.85rem; font-size: 1rem;
    border: 0; border-radius: 0.6rem; background: #5b8cff; color: #fff;
    font-weight: 600; cursor: pointer;
  }
  button:disabled { background: #3a3f4d; color: #777d8a; cursor: default; }
  progress { width: 100%; height: 0.6rem; margin-top: 1.25rem; }
  #status { margin-top: 1rem; font-size: 0.95rem; min-height: 1.2rem; }
  .ok { color: #51cf66; }
  .err { color: #ff6b6b; }
</style>
</head>
<body>
<main>
  <h1>Upload a disc image</h1>
  <p class="sub">Pick a GameCube disc image to send to Dusklight on your Apple TV.</p>
  <label class="pick" for="picker">
    <span id="pick-label">Tap to choose a file</span>
  </label>
  <input type="file" id="picker" />
  <div id="file"></div>
  <button id="send" disabled>Upload</button>
  <progress id="bar" value="0" max="100" hidden></progress>
  <div id="status"></div>
</main>
<script>
  var picker = document.getElementById('picker');
  var send = document.getElementById('send');
  var bar = document.getElementById('bar');
  var status = document.getElementById('status');
  var fileLabel = document.getElementById('file');
  var pickLabel = document.getElementById('pick-label');
  var chosen = null;

  picker.addEventListener('change', function () {
    chosen = picker.files && picker.files[0] ? picker.files[0] : null;
    if (chosen) {
      pickLabel.textContent = 'Choose a different file';
      fileLabel.textContent = chosen.name + ' (' + fmt(chosen.size) + ')';
      send.disabled = false;
      status.textContent = '';
      status.className = '';
    }
  });

  send.addEventListener('click', function () {
    if (!chosen) return;
    send.disabled = true;
    picker.disabled = true;
    bar.hidden = false;
    status.textContent = 'Uploading...';
    status.className = '';
    var xhr = new XMLHttpRequest();
    xhr.open('POST', '/upload?name=' + encodeURIComponent(chosen.name));
    xhr.upload.onprogress = function (e) {
      if (e.lengthComputable) {
        var pct = Math.round((e.loaded / e.total) * 100);
        bar.value = pct;
        status.textContent = 'Uploading... ' + pct + '%';
      }
    };
    xhr.onload = function () {
      if (xhr.status >= 200 && xhr.status < 300) {
        bar.value = 100;
        status.textContent = 'Upload complete. Return to your TV.';
        status.className = 'ok';
      } else {
        fail(xhr.responseText || ('Upload failed (' + xhr.status + ').'));
      }
    };
    xhr.onerror = function () { fail('Upload failed. Check your connection and try again.'); };
    xhr.send(chosen);
  });

  function fail(msg) {
    status.textContent = msg;
    status.className = 'err';
    bar.hidden = true;
    send.disabled = false;
    picker.disabled = false;
  }

  function fmt(bytes) {
    var u = ['B', 'KB', 'MB', 'GB'];
    var i = 0;
    while (bytes >= 1024 && i < u.length - 1) { bytes /= 1024; i++; }
    return bytes.toFixed(i ? 1 : 0) + ' ' + u[i];
  }
</script>
</body>
</html>)HTML";

// Reduces a client-supplied file name to a safe basename. Strips directory
// components and characters that are invalid or risky in a path, and falls
// back to a default when nothing usable remains.
std::string sanitize_filename(std::string name) {
    if (const auto slash = name.find_last_of("/\\"); slash != std::string::npos) {
        name = name.substr(slash + 1);
    }

    std::string out;
    out.reserve(name.size());
    for (const char c : name) {
        const auto uc = static_cast<unsigned char>(c);
        if (uc < 0x20 || c == ':' || c == '"' || c == '*' || c == '?' || c == '<' || c == '>' ||
            c == '|')
        {
            continue;
        }
        out.push_back(c);
    }

    while (!out.empty() && (out.front() == '.' || out.front() == ' ')) {
        out.erase(out.begin());
    }
    while (!out.empty() && out.back() == ' ') {
        out.pop_back();
    }

    if (out.empty()) {
        out = "upload.iso";
    }
    return out;
}

// Finds the device's IPv4 address on the local network. Prefers Wi-Fi /
// Ethernet ("en") interfaces and skips loopback and self-assigned addresses.
std::string local_ipv4() {
    ifaddrs* addrs = nullptr;
    if (getifaddrs(&addrs) != 0 || addrs == nullptr) {
        return {};
    }

    std::string preferred;
    std::string fallback;
    for (ifaddrs* ifa = addrs; ifa != nullptr; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == nullptr || ifa->ifa_addr->sa_family != AF_INET) {
            continue;
        }
        if ((ifa->ifa_flags & IFF_UP) == 0 || (ifa->ifa_flags & IFF_LOOPBACK) != 0) {
            continue;
        }

        char buffer[INET_ADDRSTRLEN] = {};
        auto* sin = reinterpret_cast<sockaddr_in*>(ifa->ifa_addr);
        if (inet_ntop(AF_INET, &sin->sin_addr, buffer, sizeof(buffer)) == nullptr) {
            continue;
        }

        const std::string address(buffer);
        if (address.rfind("169.254.", 0) == 0) {
            continue;  // link-local / self-assigned, not routable
        }

        const std::string name = ifa->ifa_name != nullptr ? ifa->ifa_name : "";
        if (name.rfind("en", 0) == 0) {
            if (preferred.empty()) {
                preferred = address;
            }
        } else if (fallback.empty()) {
            fallback = address;
        }
    }

    freeifaddrs(addrs);
    return !preferred.empty() ? preferred : fallback;
}

}  // namespace

struct UploadServer::Impl {
    httplib::Server server;
    std::thread thread;
    std::filesystem::path destinationDir;

    mutable std::mutex mutex;
    Status status;
    std::optional<std::filesystem::path> completedUpload;

    void run();
    void handle_upload(const httplib::Request& req, httplib::Response& res,
        const httplib::ContentReader& reader);
};

void UploadServer::Impl::handle_upload(const httplib::Request& req, httplib::Response& res,
    const httplib::ContentReader& reader) {
    if (req.is_multipart_form_data()) {
        res.status = 400;
        res.set_content("Expected a raw file body, not multipart form data.", "text/plain");
        return;
    }

    {
        std::lock_guard lock(mutex);
        if (status.uploadActive) {
            res.status = 409;
            res.set_content("Another upload is already in progress.", "text/plain");
            return;
        }
        status.uploadActive = true;
    }

    // Clears the active flag no matter which path returns below.
    struct ActiveGuard {
        Impl& impl;
        ~ActiveGuard() {
            std::lock_guard lock(impl.mutex);
            impl.status.uploadActive = false;
        }
    } activeGuard{*this};

    const std::string name = sanitize_filename(req.get_param_value("name"));
    const auto total = static_cast<std::uint64_t>(req.get_header_value_u64("Content-Length", 0));
    const auto finalPath = destinationDir / name;
    const auto partPath = destinationDir / (name + ".part");

    {
        std::lock_guard lock(mutex);
        status.activeFileName = name;
        status.bytesReceived = 0;
        status.bytesTotal = total;
    }

    std::ofstream out(partPath, std::ios::binary | std::ios::trunc);
    if (!out) {
        Log.error("Upload: could not open '{}' for writing", partPath.string());
        res.status = 500;
        res.set_content("The app could not open a file to save the upload.", "text/plain");
        return;
    }

    std::uint64_t received = 0;
    const bool readOk = reader([&](const char* data, size_t length) {
        out.write(data, static_cast<std::streamsize>(length));
        if (!out) {
            return false;
        }
        received += length;
        std::lock_guard lock(mutex);
        status.bytesReceived = received;
        return true;
    });
    out.close();

    if (!readOk || !out) {
        Log.error("Upload of '{}' failed after {} bytes", name, received);
        std::error_code ec;
        std::filesystem::remove(partPath, ec);
        res.status = 500;
        res.set_content("The upload failed while saving. Please try again.", "text/plain");
        return;
    }

    std::error_code ec;
    std::filesystem::remove(finalPath, ec);
    std::filesystem::rename(partPath, finalPath, ec);
    if (ec) {
        Log.error("Upload: could not move '{}' into place: {}", partPath.string(), ec.message());
        std::filesystem::remove(partPath, ec);
        res.status = 500;
        res.set_content("The upload completed but could not be saved.", "text/plain");
        return;
    }

    {
        std::lock_guard lock(mutex);
        completedUpload = finalPath;
    }
    Log.info("Upload complete: '{}' ({} bytes)", finalPath.string(), received);
    res.set_content("Upload complete. Return to your TV.", "text/plain");
}

void UploadServer::Impl::run() {
    server.Get("/", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(kUploadPageHtml, "text/html; charset=utf-8");
    });
    server.Post("/upload", [this](const httplib::Request& req, httplib::Response& res,
                                 const httplib::ContentReader& reader) {
        handle_upload(req, res, reader);
    });

    // Uploads of multi-gigabyte disc images take a while; allow generous
    // idle time between socket reads before giving up.
    server.set_read_timeout(120, 0);

    // httplib rejects request bodies over 100 MB by default. Disc images are
    // far larger, so raise the cap well above any real image while still
    // bounding how much a stray request can write to disk.
    server.set_payload_max_length(static_cast<size_t>(16) * 1024 * 1024 * 1024);

    int port = 0;
    if (server.bind_to_port("0.0.0.0", kPreferredPort)) {
        port = kPreferredPort;
    } else {
        port = server.bind_to_any_port("0.0.0.0");
    }

    if (port <= 0) {
        Log.error("Upload server could not bind a network port");
        std::lock_guard lock(mutex);
        status.state = State::Failed;
        status.error = "Could not start the upload server (no free network port).";
        return;
    }

    const std::string ip = local_ipv4();
    if (ip.empty()) {
        Log.error("Upload server: no local network address available");
        std::lock_guard lock(mutex);
        status.state = State::Failed;
        status.error = "This device is not connected to a local network.";
        return;
    }

    {
        std::lock_guard lock(mutex);
        status.state = State::Running;
        status.url = "http://" + ip + ":" + std::to_string(port);
    }
    Log.info("Upload server listening at http://{}:{}", ip, port);

    server.listen_after_bind();

    std::lock_guard lock(mutex);
    if (status.state == State::Running) {
        status.state = State::Stopped;
    }
}

UploadServer::UploadServer() : mImpl(std::make_unique<Impl>()) {}

UploadServer::~UploadServer() {
    stop();
}

void UploadServer::start(std::filesystem::path destinationDir) {
    if (mImpl->thread.joinable()) {
        return;  // already running
    }

    std::error_code ec;
    std::filesystem::create_directories(destinationDir, ec);
    if (ec) {
        Log.error("Upload server: could not create '{}': {}", destinationDir.string(),
            ec.message());
    }

    {
        std::lock_guard lock(mImpl->mutex);
        mImpl->destinationDir = std::move(destinationDir);
        mImpl->status = Status{};
        mImpl->completedUpload.reset();
    }

    mImpl->thread = std::thread([this] { mImpl->run(); });
}

void UploadServer::stop() {
    mImpl->server.stop();
    if (mImpl->thread.joinable()) {
        mImpl->thread.join();
    }
}

bool UploadServer::running() const noexcept {
    return mImpl->thread.joinable();
}

UploadServer::Status UploadServer::status() const {
    std::lock_guard lock(mImpl->mutex);
    return mImpl->status;
}

std::optional<std::filesystem::path> UploadServer::take_completed_upload() {
    std::lock_guard lock(mImpl->mutex);
    if (!mImpl->completedUpload.has_value()) {
        return std::nullopt;
    }
    auto path = std::move(*mImpl->completedUpload);
    mImpl->completedUpload.reset();
    return path;
}

}  // namespace dusk
