#include "audio-player.hpp"
#include <iostream>
#include <cmath>
#include <filesystem>

namespace fs = std::filesystem;

// ============================================================================
// Constructor & Destructor
// ============================================================================

AudioPlayer::AudioPlayer()
    : read_buffer_(&buffer1_), fill_buffer_(&buffer2_) {
    // Initialize PortAudio
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        throw std::runtime_error(
            std::string("PortAudio init failed: ") + Pa_GetErrorText(err)
        );
    }

    // Initialize buffers - MEMSET FIRST!
    std::memset(&buffer1_, 0, sizeof(AudioBuffer));
    std::memset(&buffer2_, 0, sizeof(AudioBuffer));
    buffer1_.frames_read = 0;
    buffer2_.frames_read = 0;

    int max_channels = 2; // assume mono or stereo for now
    buffer1_.data.resize(AUDIO_BUFFER_SIZE * max_channels);
    buffer2_.data.resize(AUDIO_BUFFER_SIZE * max_channels);

    // Launch worker threads
    audio_thread_ = std::thread(&AudioPlayer::audioThreadLoop, this);
    file_reader_thread_ = std::thread(&AudioPlayer::fileReaderThreadLoop, this);

    // CRITICAL: Give threads time to initialize
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

AudioPlayer::~AudioPlayer() {
    // Signal threads to exit
    should_exit_.store(true, std::memory_order_release);

    // Wait for threads to finish
    if (audio_thread_.joinable()) audio_thread_.join();
    if (file_reader_thread_.joinable()) file_reader_thread_.join();

    // Clean up PortAudio
    if (stream_) {
        Pa_StopStream(stream_);
        Pa_CloseStream(stream_);
    }
    Pa_Terminate();

    // Close any open file
    if (current_file_) {
        sf_close(current_file_);
    }
}

// ============================================================================
// Public API (Main Thread)
// ============================================================================

void AudioPlayer::play(const std::string& filepath) {
    if (filepath.empty()) return;

    // Convert relative path to absolute path
    std::string absolute_path;
    try {
        absolute_path = fs::absolute(filepath).string();
    }
    catch (...) {
        absolute_path = filepath;  // Fallback if conversion fails
    }

    std::cout << "Opening file: " << absolute_path << "\n";

    next_filepath_ = absolute_path;
    next_file_requested_.store(true, std::memory_order_release);

    // Wait for file to be opened by reader thread
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Check if file actually opened
    if (!current_file_) {
        std::cerr << "Failed to open file: " << absolute_path << "\n";
        return;
    }

    state_.store(PlaybackState::Playing, std::memory_order_release);
}

void AudioPlayer::pause() {
    state_.store(PlaybackState::Paused, std::memory_order_release);
}

void AudioPlayer::resume() {
    if (current_file_) {
        state_.store(PlaybackState::Playing, std::memory_order_release);
    }
}

void AudioPlayer::stop() {
    state_.store(PlaybackState::Stopped, std::memory_order_release);
    current_frame_.store(0, std::memory_order_release);
    file_ended_.store(false, std::memory_order_release);
}

PlaybackState AudioPlayer::getState() const {
    return state_.load(std::memory_order_acquire);
}

float AudioPlayer::getProgress() const {
    int total = total_frames_.load(std::memory_order_acquire);
    if (total <= 0) return 0.0f;

    int current = current_frame_.load(std::memory_order_acquire);
    return static_cast<float>(current) / static_cast<float>(total);
}

int AudioPlayer::getCurrentFrame() const {
    return current_frame_.load(std::memory_order_acquire);
}

int AudioPlayer::getTotalFrames() const {
    return total_frames_.load(std::memory_order_acquire);
}

std::string AudioPlayer::getCurrentFile() const {
    return current_filepath_;
}

// ============================================================================
// Audio Thread: PortAudio Callback
// ============================================================================

int AudioPlayer::audioCallback(
    const void* input,
    void* output,
    unsigned long frameCount,
    const PaStreamCallbackTimeInfo* timeInfo,
    PaStreamCallbackFlags statusFlags,
    void* userData
) {
    (void)input; (void)timeInfo; (void)statusFlags;

    auto* self = static_cast<AudioPlayer*>(userData);
    float* out = static_cast<float*>(output);

    PlaybackState state = self->state_.load(std::memory_order_acquire);

    // Default: silence
    std::fill(out, out + frameCount * self->file_info_.channels, 0.0f);

    if (state != PlaybackState::Playing)
        return paContinue;

    if (!self->read_buffer_ || self->read_buffer_->data.empty())
        return paContinue;

    int ch = self->file_info_.channels > 0 ? self->file_info_.channels : 1;
    const float* src = self->read_buffer_->data.data();

    // How much data do we actually have in the current buffer?
    int available_frames = self->read_buffer_->frames_read;

    // Don't copy more than requested, and don't copy more than available
    int frames_to_copy = std::min<int>(frameCount, available_frames);

    // Copy audio data
    for (int i = 0; i < frames_to_copy; ++i) {
        for (int c = 0; c < ch; ++c) {
            out[i * ch + c] = src[i * ch + c];
        }
    }

    self->current_frame_.fetch_add(frames_to_copy, std::memory_order_release);

    // If we have exhausted the current read buffer (consumed all available frames),
    // we must swap immediately so the next callback (or the rest of this one) has data.
    if (frames_to_copy >= available_frames) {
        // Reset the read count for the recycled buffer before swapping
        // (The reader thread will overwrite this, but good practice)
        self->read_buffer_->frames_read = 0;

        if (self->swap_ready_.load(std::memory_order_acquire)) {
            self->swapBuffers();
        }
        else if (self->file_ended_.load(std::memory_order_acquire)) {
            // Only stop if there is no next buffer ready AND the file is done
            self->state_.store(PlaybackState::Stopped, std::memory_order_release);
            self->file_ended_.store(false, std::memory_order_release);
        }
    }
    // -----------------------------------

    return paContinue;
}

void AudioPlayer::swapBuffers() {
    // Lock-free swap: atomically exchange read/fill buffers
    std::swap(read_buffer_, fill_buffer_);
    swap_ready_.store(false, std::memory_order_release);
}

// ============================================================================
// File Reader Thread: Load audio data ahead of time
// ============================================================================

void AudioPlayer::fileReaderThreadLoop() {
    while (!should_exit_.load(std::memory_order_acquire)) {
        // Check if new file requested
        if (next_file_requested_.load(std::memory_order_acquire)) {
            // Close current file if open
            if (current_file_) {
                sf_close(current_file_);
                current_file_ = nullptr;
            }

            std::memset(&file_info_, 0, sizeof(file_info_));
            current_file_ = sf_open(next_filepath_.c_str(), SFM_READ, &file_info_);
            if (!current_file_) {
                std::cerr << "Failed to open: " << next_filepath_
                    << " error: " << sf_strerror(nullptr) << "\n";
                next_file_requested_.store(false, std::memory_order_release);
                continue;
            }

            int ch = file_info_.channels > 0 ? file_info_.channels : 1;
            int sr = file_info_.samplerate > 0 ? file_info_.samplerate : SAMPLE_RATE;

            // Resize buffers for channels
            buffer1_.data.assign(AUDIO_BUFFER_SIZE * ch, 0.0f);
            buffer2_.data.assign(AUDIO_BUFFER_SIZE * ch, 0.0f);

            // PRE-FILL BOTH BUFFERS before starting stream
            int frames1 = sf_readf_float(current_file_, buffer1_.data.data(), AUDIO_BUFFER_SIZE);
            buffer1_.frames_read = frames1;

            int frames2 = sf_readf_float(current_file_, buffer2_.data.data(), AUDIO_BUFFER_SIZE);
            buffer2_.frames_read = frames2;

            std::cout << "Pre-filled buffers: " << frames1 << " and " << frames2 << " frames\n";

            // (Re)open PortAudio stream with correct channel count and SR
            if (stream_) {
                Pa_StopStream(stream_);
                Pa_CloseStream(stream_);
                stream_ = nullptr;
            }

            PaStreamParameters out;
            out.device = Pa_GetDefaultOutputDevice();
            out.channelCount = ch;
            out.sampleFormat = paFloat32;
            out.suggestedLatency = Pa_GetDeviceInfo(out.device)->defaultLowOutputLatency;
            out.hostApiSpecificStreamInfo = nullptr;

            PaError err = Pa_OpenStream(
                &stream_,
                nullptr,
                &out,
                sr,
                AUDIO_BUFFER_SIZE,
                paClipOff,
                &AudioPlayer::audioCallback,
                this
            );
            if (err != paNoError) {
                std::cerr << "Pa_OpenStream error: " << Pa_GetErrorText(err) << "\n";
            }
            else {
                err = Pa_StartStream(stream_);
                if (err != paNoError) {
                    std::cerr << "Pa_StartStream error: " << Pa_GetErrorText(err) << "\n";
                }
                else {
                    std::cout << "Stream started successfully\n";
                }
            }

            std::cout << "Sample rate: " << file_info_.samplerate
                << ", Channels: " << file_info_.channels
                << ", Frames: " << file_info_.frames << "\n";

            current_filepath_ = next_filepath_;
            total_frames_.store(file_info_.frames, std::memory_order_release);
            current_frame_.store(0, std::memory_order_release);
            file_ended_.store(false, std::memory_order_release);
            next_file_requested_.store(false, std::memory_order_release);
            swap_ready_.store(true, std::memory_order_release); // Signal second buffer is ready
        }

        if (!current_file_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        // Only refill if swap has been consumed
        if (swap_ready_.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        if (!fill_buffer_ || fill_buffer_->data.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        int frames_read = sf_readf_float(
            current_file_,
            fill_buffer_->data.data(),
            AUDIO_BUFFER_SIZE
        );

        if (frames_read < 0) {
            std::cerr << "sf_readf_float error: "
                << sf_strerror(current_file_) << "\n";
            file_ended_.store(true, std::memory_order_release);
            continue;
        }

        fill_buffer_->frames_read = frames_read;

        if (frames_read < AUDIO_BUFFER_SIZE) {
            file_ended_.store(true, std::memory_order_release);
        }

        // Signal that buffer is ready to swap
        swap_ready_.store(true, std::memory_order_release);

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void AudioPlayer::audioThreadLoop() {
    // Coordination thread
    while (!should_exit_.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}
