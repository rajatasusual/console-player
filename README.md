# Console Audio Player

A high-performance terminal-based audio player with Freesound.org integration, built with modern C++ and a sleek TUI interface.

[![Build Console Player](https://github.com/rajatasusual/console-player/actions/workflows/build.yml/badge.svg)](https://github.com/rajatasusual/console-player/actions/workflows/build.yml)

## Features

- **Lock-Free Audio Engine** - Real-time audio playback with PortAudio, zero-copy buffer swapping
- **Multi-Format Support** - WAV, FLAC, OGG, MP3 via libsndfile
- **Freesound Integration** - Search and download from 500k+ Creative Commons sounds
- **Local Library Management** - SQLite-based collection with playlists
- **Real-Time Waveform Visualization** - Live audio graph in terminal
- **Intuitive TUI** - Built with FTXUI, keyboard-driven navigation
- **Cross-Platform** - Windows, Linux, macOS

## Screenshots

```
┌─ AUDIO PLAYER ────────────────────────────────────── Status: Playing: Explosion.wav ─┐
│ Library | Search Online | Player                                                      │
├──────────────────────────────────────────────────────────────────────────────────────┤
│                          ┌───────────────────────────────────────┐                    │
│                          │         Explosion.wav                 │                    │
│                          │         by SoundDesigner              │                    │
│                          └───────────────────────────────────────┘                    │
│                                                                                       │
│ ┌────────────────────────────────────────────────────────────────────────────────┐    │
│ │                       [Waveform Visualization]                                 │    │
│ │  ▁▂▃▅▇█████▇▅▃▂▁  ▁▃▅▇███████▇▅▃▁  ▂▄▆████▆▄▂  ▁▃▅▇█████▇▅▃▁                  │
│ └────────────────────────────────────────────────────────────────────────────────┘    │
│  1:23                                                                          3:45   │
│                                                                                       │
│                              Playing ▶                                               │
│                                                                                       │
│         Keyboard: [Space]=Play/Pause | [←/→]=Seek | [Ctrl+←/→]=Tabs                  │
├──────────────────────────────────────────────────────────────────────────────────────┤
│ Status: ████████████████████████████████████████░░░░░░░░ 87%                         │
└──────────────────────────────────────────────────────────────────────────────────────┘
```

## Prerequisites

- **C++17 compiler** (MSVC, GCC 7+, Clang 5+)
- **CMake 3.19+**
- **vcpkg** (for dependency management)
- **Git**

### Platform-Specific Requirements

**Linux:**
```bash
sudo apt-get install build-essential cmake ninja-build pkg-config libasound2-dev
```

**macOS:**
```bash
brew install cmake ninja pkg-config
```

**Windows:**
- Visual Studio 2019+ with C++ desktop development workload

## Building

### 1. Clone the repository
```bash
git clone https://github.com/yourusername/console-player.git
cd console-player
```

### 2. Setup vcpkg
```bash
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg
./bootstrap-vcpkg.sh  # Linux/macOS
# or
bootstrap-vcpkg.bat   # Windows

# Set environment variable
export VCPKG_ROOT=/path/to/vcpkg  # Linux/macOS
# or
set VCPKG_ROOT=C:\path\to\vcpkg   # Windows
```

### 3. Build the project

**Windows:**
```bash
cmake --preset x64-release
cmake --build --preset x64-release --config Release
```

**Linux/macOS:**
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```

Binary will be in `out/build/x64-release/console-player/Release/` (Windows) or `build/console-player/` (Linux/macOS).

## Configuration

### Freesound API Setup

1. Register at [freesound.org](https://freesound.org/apiv2/apply/)
2. Create `config.json` in the build directory:

```json
{
  "FREESOUND_CLIENT_ID": "your_client_id_here",
  "FREESOUND_CLIENT_SECRET": "your_client_secret_here"
}
```

**Offline Mode:** Press ENTER at the auth prompt to skip API setup.

## Usage

### Running the Player
```bash
./console-player  # Linux/macOS
console-player.exe  # Windows
```

### Keyboard Shortcuts

**Global:**
- `Space` - Play/Pause
- `Ctrl + ←/→` - Switch tabs

**Library Tab:**
- `↑/↓` - Navigate tracks
- `Enter` - Play selected track
- Type in search box to filter

**Search Tab:**
- Type query and press `Enter` to search
- `↑/↓` - Navigate results
- `Enter` - Download selected sound

**Player Tab:**
- `←/→` - Seek backward/forward (5 seconds)
- `S` - Stop playback

## Project Structure

```
console-player/
├── console-player/
│   ├── include/
│   │   ├── audio-player.hpp      # Lock-free audio engine
│   │   ├── database.hpp          # SQLite wrapper
│   │   ├── downloader.hpp        # Freesound API client
│   │   └── oauth2.hpp            # OAuth2 authentication
│   ├── audio-player.cpp          # Audio implementation
│   └── console-player.cpp        # Main TUI application
├── CMakeLists.txt
├── vcpkg.json                    # Dependencies manifest
└── .github/workflows/build.yml   # CI/CD pipeline
```

## Dependencies

All dependencies are managed via vcpkg:

- **PortAudio** - Cross-platform audio I/O
- **libsndfile** - Audio file format support
- **FTXUI** - Terminal UI framework
- **libcurl** - HTTP client for API calls
- **SQLite3** - Embedded database
- **nlohmann/json** - JSON parsing

## Architecture Highlights

### Lock-Free Audio System
The audio engine uses atomic operations and double-buffering to avoid mutex contention in the real-time audio thread:
- Pre-allocated circular buffers
- Lock-free swap mechanism
- Separate file reader thread
- Sample-accurate seeking

### Thread Model
- **Main Thread** - UI rendering and event handling
- **Audio Callback** - PortAudio real-time thread (highest priority)
- **File Reader Thread** - Asynchronous buffer filling
- **Task Queue** - Safe cross-thread UI updates

## Contributing

Contributions welcome! Please:
1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Commit your changes (`git commit -m 'Add amazing feature'`)
4. Push to the branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Acknowledgments

- [Freesound.org](https://freesound.org) - Audio content API
- [FTXUI](https://github.com/ArthurSonzogni/FTXUI) - Terminal UI framework
- [PortAudio](http://www.portaudio.com/) - Audio I/O library

## Known Issues

- MP3 support requires libsndfile built with LAME
- Windows: First-run may take longer due to vcpkg dependency compilation

## Roadmap

- [ ] Equalizer controls
- [ ] Playlist export/import
- [ ] Volume control
- [ ] Audio effects (reverb, delay)
- [ ] Spectrum analyzer visualization
- [ ] Multiple playlist support
- [ ] Tag editing

---

**Questions?** Open an issue or discussion on GitHub.
