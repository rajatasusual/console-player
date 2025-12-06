set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================================
# Dependencies
# ============================================================================

# PortAudio (Audio I/O) - EXPLICIT LINKING
find_package(portaudio CONFIG QUIET)
if(NOT portaudio_FOUND)
    # Fallback: Try module mode
    find_package(portaudio MODULE QUIET)
endif()

# libsndfile (Audio file I/O)
find_package(SndFile CONFIG QUIET)
if(NOT SndFile_FOUND)
    find_package(SndFile MODULE QUIET)
endif()

# libcurl
find_package(CURL REQUIRED)

# nlohmann_json
find_package(nlohmann_json QUIET)
if(NOT nlohmann_json_FOUND)
    include_directories(${CMAKE_SOURCE_DIR}/include)
endif()

# SQLite3
find_package(SQLite3 REQUIRED)

#FTXUI
find_package(ftxui REQUIRED)


# ============================================================================
# Audio Player Library
# ============================================================================

add_library(audio-player STATIC
    audio-player.cpp
)

target_include_directories(audio-player PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# CRITICAL: Link PortAudio and SndFile to audio-player library
if(portaudio_FOUND)
    target_link_libraries(audio-player PUBLIC portaudio)
else()
    # Manual linking for Windows (if vcpkg)
    target_link_libraries(audio-player PUBLIC portaudio_static)
endif()

if(SndFile_FOUND)
    target_link_libraries(audio-player PUBLIC SndFile::sndfile)
else()
    # Manual linking
    target_link_libraries(audio-player PUBLIC sndfile)
endif()

# ============================================================================
# Main Application
# ============================================================================

add_executable(console-player 
    console-player.cpp
)

# Link ALL libraries to console-player
target_link_libraries(console-player 
    PRIVATE 
        audio-player
        CURL::libcurl
        SQLite::SQLite3
        ftxui::screen
        ftxui::screen
        ftxui::dom
        ftxui::component
)

if(nlohmann_json_FOUND)
    target_link_libraries(console-player PRIVATE nlohmann_json::nlohmann_json)
endif()

# Include directories
target_include_directories(console-player 
    PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}
)

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET console-player PROPERTY CXX_STANDARD 20)
endif()

# config of environmental variables
configure_file(config.json.in config.json COPYONLY)
