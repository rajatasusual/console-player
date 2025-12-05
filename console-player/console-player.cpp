// console-player.cpp : Defines the entry point for the application.
//

#include "console-player.hpp"
#include "audio-player.hpp" 

#include <iostream>
#include <string>
#include <curl/curl.h>
#include "oauth2.hpp"
#include <vector>
#include <iomanip>
#include "database.hpp"
#include "downloader.hpp"

static void displaySounds(const std::vector<Sound>& sounds) {
    std::cout << std::string(80, '-') << "\n";
    std::cout << std::left << std::setw(8) << "ID"
        << std::setw(30) << "Name"
        << std::setw(10) << "Duration"
        << std::setw(10) << "Rating"
        << "Downloads\n";
    std::cout << std::string(80, '-') << "\n";

    for (const auto& s : sounds) {
        std::cout << std::left << std::setw(8) << s.id
            << std::setw(30) << s.name.substr(0, 28)
            << std::setw(10)
            << std::fixed << std::setprecision(1) << s.duration
            << std::setw(10) << s.rating
            << s.download_count << "\n";
    }
    std::cout << std::string(80, '-') << "\n";
}

static std::string trim(std::string str) {
    size_t start = str.find_first_not_of(" \n\r\t");
    size_t end = str.find_last_not_of(" \n\r\t");
    if (start == str.npos) return "";
    return str.substr(start, end - start + 1);
}

int main() {
    AudioPlayer player;
    std::cout << "=== Freesound OAuth2 Authorization Flow ===\n\n";

    // Configuration
    std::string client_id = "7FZXaYAs1qtPnnElA61x";
    std::string client_secret = "iLQASrQhEgZcBFufIjz1EHsTmX0FKVmwqTqMUaRt";
    std::string redirect_uri = "http://freesound.org/home/app_permissions/permission_granted/.";

    std::string download_dir = "./sounds";
    Database db("freesound.db");
    db.initSchema();

    // Step 1: Generate authorization URL
    std::string auth_url = OAuth2::buildAuthorizationUrl(
        client_id,
        redirect_uri
    );

    std::cout << "Step 1: Open this URL in your browser:\n"
        << auth_url << "\n\n";

    // Step 2: Wait for user to authorize and get code
    std::cout << "After authorizing, you'll be redirected.\n"
        << "Paste the authorization code here: ";

    std::string auth_code;
    std::getline(std::cin, auth_code);

    // Step 3: Exchange code for access token
    std::cout << "\nExchanging authorization code for token...\n";

    std::string response = OAuth2::exchangeCodeForToken(
        client_id,
        client_secret,
        auth_code
    );


    // Step 4: Extract and display token
    std::string access_token = OAuth2::extractAccessToken(response);

    if (!access_token.empty()) {
        std::cout << "✓ Success! Access token received:\n"
            << access_token << "\n\n";
    }

    Downloader downloader(access_token, download_dir);

    std::string command;
    while (true) {
        std::cout << "\n[Commands: search, list, download, play, pause, status, help, quit]\n"
            << "> ";
        std::getline(std::cin, command);
        command = trim(command);

        if (command == "quit") {
            break;
        }
        else if (command.substr(0, 4) == "play") {
            int sound_id;
            std::cout << "Enter sound ID to play: ";
            std::cin >> sound_id;
            std::cin.ignore();

            auto sounds = db.getAllSounds();

            for (auto& s : sounds) {
                if (s.id == sound_id) {
                    std::string abs_path = fs::absolute(s.file_path).string();
                    player.play(abs_path);
                    std::cout << "▶ Playing: " << s.name << "\n";
                }
            }
        }
        else if (command == "pause") {
            player.pause();
        }
        else if (command == "status") {
            std::cout << "Progress: " << (player.getProgress() * 100) << "%\n";
        }
        else if (command == "search") {
            std::cout << "Enter search query: ";
            std::string query;
            std::getline(std::cin, query);
            query = trim(query);

            std::cout << "Searching...\n";
            json results = downloader.searchSounds(query, 10);

            if (results.contains("results")) {
                for (const auto& item : results["results"]) {
                    int id = item["id"];
                    std::cout << "ID: " << id << " | "
                        << item["name"] << "\n";
                }
            }
        }
        else if (command == "download") {
            int sound_id;
            std::cout << "Enter sound ID to download: ";
            std::cin >> sound_id;
            std::cin.ignore();

            if (!db.soundExists(sound_id)) {
                std::cout << "Downloading...\n";
                std::string filepath;
                if (downloader.downloadSound(sound_id, filepath)) {
                    json metadata = downloader.getSoundMetadata(sound_id);
                    Sound s;
                    s.id = sound_id;
                    s.name = metadata.value("name", "Unknown");
                    s.url = metadata.value("url", "");
                    s.duration = metadata.value("duration", 0.0);
                    s.rating = metadata.value("avg_rating", 0.0);
                    s.download_count = metadata.value("num_downloads", 0);
                    s.file_path = filepath;

                    db.addSound(s);
                    std::cout << "✓ Downloaded and saved to database\n";
                }
                else {
                    std::cout << "✗ Download failed\n";
                }
            }
            else {
                std::cout << "Sound already downloaded\n";
            }
        }
        else if (command == "list") {
            std::cout << "[Sort by: date, rating, downloads, duration]\n"
                << "> ";
            std::string sort_by;
            std::getline(std::cin, sort_by);
            sort_by = trim(sort_by);

            if (sort_by.empty()) sort_by = "added_date";

            auto sounds = db.getSoundsSorted(sort_by);
            displaySounds(sounds);
        }
        else if (command == "playlists") {
            std::cout << "[pl-create, pl-list, pl-add, pl-remove, pl-view, pl-delete]\n"
                << "> ";
            std::string subcmd;
            std::getline(std::cin, subcmd);
            subcmd = trim(subcmd);

            if (subcmd.find("pl-create") == 0) {
                std::cout << "Playlist name: ";
                std::string name;
                std::getline(std::cin, name);
                db.createPlaylist(trim(name));
                std::cout << "✓ Playlist created\n";
            }
            else if (subcmd.find("pl-list") == 0) {
                auto playlists = db.getAllPlaylists();
                for (const auto& p : playlists) {
                    std::cout << "[ID:" << p.id << "] " << p.name
                        << " (created: " << p.created_date << ")\n";
                }
            }
        }
        else if (command == "help") {
            std::cout << "search       - Find sounds on Freesound\n"
                << "download     - Download a sound by ID\n"
                << "list         - Show all downloaded sounds\n"
                << "playlists    - Manage playlists\n"
                << "quit         - Exit\n";
        }
    }
    std::cout << "Goodbye!\n";

    return 0;
}