#pragma once

#ifdef _WIN32
#include <Windows.h>
#include <wrl/client.h>
#endif

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

// A looping video file played by the Media Foundation Media Engine in frame-server mode (no
// window; video_player_windows.cpp) or by FFmpeg and SDL3 audio (video_player_ffmpeg.cpp): its
// sound goes to the default device, its frames are copied out on the UI thread.
// UI thread only; the player's own threads only raise the error flag.
class VideoPlayer {
public:
    VideoPlayer();
    ~VideoPlayer();
    VideoPlayer(const VideoPlayer&) = delete;
    VideoPlayer& operator=(const VideoPlayer&) = delete;

    bool open(const std::filesystem::path& file, double volume);
    void stop();
    bool active() const;
    bool failed() const { return error_->load(); }
    void setVolume(double volume);
    // The frame shown now when it changed since the last call: BGRA, stride width * 4,
    // scaled down to at most max_width pixels wide.
    bool frame(std::vector<uint8_t>& pixels, uint32_t& width, uint32_t& height, uint32_t max_width = 1280);

private:
    struct Impl; // The platform's engine and frame buffers.
    std::unique_ptr<Impl> impl_;
    std::shared_ptr<std::atomic_bool> error_ = std::make_shared<std::atomic_bool>(false);
};
