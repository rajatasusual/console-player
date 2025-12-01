find_package(CURL REQUIRED)
find_package(nlohmann_json 3.2.0 QUIET)
find_package(SQLite3 REQUIRED)


# If nlohmann_json not found, include it as header-only
if(NOT nlohmann_json_FOUND)
    message(STATUS "nlohmann_json not found, using header-only version")
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)  
endif()

# Add source to this project's executable.
add_executable (console-player "console-player.cpp" "console-player.h")

# Link libraries
target_link_libraries(console-player 
    PRIVATE 
        CURL::libcurl
        SQLite::SQLite3
        nlohmann_json::nlohmann_json
)

# Include directories
target_include_directories(console-player 
    PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET console-player PROPERTY CXX_STANDARD 20)
endif()

# TODO: Add tests and install targets if needed.
