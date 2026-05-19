#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace dusk {

// A minimal HTTP server that lets a phone or computer on the same local
// network upload a game disc image to the app through a web browser.
//
// This exists for tvOS, which has no system file picker, no Files app, and
// no way to receive files over AirDrop. It mirrors the approach used by
// RetroArch and Provenance on Apple TV.
//
// The server runs on its own background thread. All public methods are safe
// to call from the main (UI) thread while the server is running.
class UploadServer {
public:
    enum class State {
        Stopped,  // not started, or stopped cleanly
        Running,  // listening and ready to accept an upload
        Failed,   // could not bind a socket; see Status::error
    };

    struct Status {
        State state = State::Stopped;
        // Address to type into a browser, e.g. "http://192.168.1.42:8080".
        // Empty unless state == Running.
        std::string url;
        // Human-readable failure reason; populated only when state == Failed.
        std::string error;
        // True while a file is actively being received.
        bool uploadActive = false;
        std::uint64_t bytesReceived = 0;
        // Total expected bytes, or 0 if the client sent no Content-Length.
        std::uint64_t bytesTotal = 0;
        std::string activeFileName;
    };

    UploadServer();
    ~UploadServer();

    UploadServer(const UploadServer&) = delete;
    UploadServer& operator=(const UploadServer&) = delete;

    // Starts the server on a background thread, writing completed uploads
    // into destinationDir (created if it does not exist). No-op if the
    // server is already running.
    void start(std::filesystem::path destinationDir);

    // Stops the server and joins its thread. Safe to call when stopped.
    // Called automatically by the destructor.
    void stop();

    [[nodiscard]] bool running() const noexcept;

    // Thread-safe snapshot of the current server and upload status.
    [[nodiscard]] Status status() const;

    // Returns the path of a finished upload exactly once, then forgets it.
    // Returns std::nullopt when no upload has completed since the last call.
    [[nodiscard]] std::optional<std::filesystem::path> take_completed_upload();

private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

}  // namespace dusk
