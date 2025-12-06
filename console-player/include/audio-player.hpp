#pragma once

#include <thread>
#include <atomic>
#include <queue>
#include <memory>
#include <string>
#include <mutex>
#include <vector>

#include <portaudio.h>
#include <sndfile.h>

/**
 * Lock-free audio player for TUI-based music player
 *
 * Architecture:
 * - Main thread: Handles FTXUI TUI input (no blocking)
 * - Audio thread: PortAudio callback + file reading (real-time safe)
 * - Communication: Atomic variables + double buffering (lock-free)
 *
 * Key Design Decisions:
 * 1. No mutexes on audio thread (priority inversion prevention)
 * 2. Pre-allocated buffers (no alloc/dealloc in audio callback)
 * 3. Double-buffered state (reader-writer synchronization)
 * 4. Cross-platform via PortAudio (ALSA/Windows/macOS)
 */

constexpr int AUDIO_BUFFER_SIZE = 4096;
constexpr int SAMPLE_RATE = 44100;

enum class PlaybackState {
    Stopped,
    Playing,
    Paused,
};

struct AudioBuffer {
    std::vector<float> data;
    int frames_read = 0;
};

class AudioPlayer {
public:
    AudioPlayer();
    ~AudioPlayer();

    // Playback control (thread-safe, non-blocking)
    void play(const std::string& filepath);
    void pause();
    void resume();
    void stop();

    std::vector<float> getWaveformData();

    // Query state (lock-free reads)
    PlaybackState getState() const;
    float getProgress() const;  // 0.0 to 1.0
    int getCurrentFrame() const;
    int getTotalFrames() const;
    std::string getCurrentFile() const;

private:
    // Audio thread entry point
    void audioThreadLoop();

    // PortAudio callback (static, delegates to instance)
    static int audioCallback(
        const void* input,
        void* output,
        unsigned long frameCount,
        const PaStreamCallbackTimeInfo* timeInfo,
        PaStreamCallbackFlags statusFlags,
        void* userData
    );

    // File I/O thread (reads ahead to fill buffers)
    void fileReaderThreadLoop();

    // Lock-free double buffer management
    void swapBuffers();

    // === Audio State (atomic, lock-free) ===
    std::atomic<PlaybackState> state_{ PlaybackState::Stopped };
    std::atomic<int> current_frame_{ 0 };
    std::atomic<int> total_frames_{ 0 };
    std::atomic<bool> should_exit_{ false };
    std::atomic<bool> file_ended_{ false };

    // === Buffers ===
    AudioBuffer buffer1_, buffer2_;
    AudioBuffer* read_buffer_;   // Audio thread reads from this
    AudioBuffer* fill_buffer_;   // File reader writes to this
    std::atomic<bool> swap_ready_{ false };

    // === File State ===
    SNDFILE* current_file_ = nullptr;
    SF_INFO file_info_{};
    std::string current_filepath_;
    std::atomic<bool> next_file_requested_{ false };
    std::string next_filepath_;

    // === PortAudio ===
    PaStream* stream_ = nullptr;

    // === Threads ===
    std::thread audio_thread_;
    std::thread file_reader_thread_;

    // VISUALIZATION
    std::vector<float> wave_buffer_; // Holds a snapshot of audio
    mutable std::mutex wave_mutex_;  // Protects the buffer
};
