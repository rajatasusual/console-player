#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/util/ref.hpp>
#include <ftxui/dom/node.hpp>
#include <ftxui/screen/color.hpp>

#include "audio-player.hpp"
#include "database.hpp"
#include "downloader.hpp"
#include "oauth2.hpp"

#include <iostream>
#include <thread>
#include <atomic>
#include <mutex>
#include <regex>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <deque>
#include <functional>

using namespace ftxui;

// ============================================================================
// Helpers
// ============================================================================

std::string cleanInput(const std::string& input) {
    std::string clean = std::regex_replace(input, std::regex("[\\r\\n\\t]+"), " ");
    return std::regex_replace(clean, std::regex("^\\s+|\\s+$"), "");
}

std::string formatDuration(double seconds) {
    int m = static_cast<int>(seconds) / 60;
    int s = static_cast<int>(seconds) % 60;
    std::stringstream ss;
    ss << m << ":" << std::setfill('0') << std::setw(2) << s;
    return ss.str();
}

// ============================================================================
// App State & Task Queue
// ============================================================================

struct AppState {
    AudioPlayer player;
    Database db{ "freesound.db" };
    std::unique_ptr<Downloader> downloader;
    bool offline_mode = false;

    // UI Data (Read by Main Thread only)
    std::vector<Sound> all_sounds;
    std::vector<Sound> visible_sounds;
    std::vector<std::string> menu_items;

    std::vector<std::pair<int, std::string>> search_results;
    std::vector<std::string> search_menu_items;

    std::string status_message = "Ready.";

    // Sorting
    int sort_selected = 0;
    std::vector<std::string> sort_options = { "Date Added", "Name", "Artist", "Duration" };

    // CURRENT PLAYING INFO
    Sound current_sound; // Holds info about currently playing track
    ftxui::Box graph_box;

    // Thread Safety Mechanism
    std::mutex task_mutex;
    std::deque<std::function<void()>> task_queue;

    // Call this from ANY thread to schedule an update on the Main Thread
    void postTask(std::function<void()> task, ScreenInteractive* screen) {
        {
            std::lock_guard<std::mutex> lock(task_mutex);
            task_queue.push_back(task);
        }
        screen->Post(Event::Custom); // Wake up the UI loop
    }

    // Call this ONLY from the Main Thread (inside render loop)
    void processTasks() {
        std::lock_guard<std::mutex> lock(task_mutex);
        while (!task_queue.empty()) {
            task_queue.front()(); // Execute the task
            task_queue.pop_front();
        }
    }

    // pause / play music by changing the player state
    void togglePlayback() {
        auto state = player.getState();
        if (state == PlaybackState::Playing) player.pause();
        else if (state == PlaybackState::Paused) player.resume();
    }

    // Logic to run on Main Thread
    void refreshLibraryUI(const std::string& query = "") {
        // 1. Filter
        visible_sounds.clear();
        std::string lower_query = query;
        std::transform(lower_query.begin(), lower_query.end(), lower_query.begin(), ::tolower);

        for (const auto& s : all_sounds) {
            if (query.empty()) {
                visible_sounds.push_back(s);
                continue;
            }
            std::string lower_name = s.name;
            std::string lower_artist = s.username;
            std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
            std::transform(lower_artist.begin(), lower_artist.end(), lower_artist.begin(), ::tolower);

            if (lower_name.find(lower_query) != std::string::npos ||
                lower_artist.find(lower_query) != std::string::npos) {
                visible_sounds.push_back(s);
            }
        }

        // 2. Sort
        std::sort(visible_sounds.begin(), visible_sounds.end(), [&](const Sound& a, const Sound& b) {
            if (sort_selected == 1) return a.name < b.name;
            if (sort_selected == 2) return a.username < b.username;
            if (sort_selected == 3) return a.duration < b.duration;
            return a.added_date > b.added_date;
            });

        // 3. Rebuild Menu Strings
        menu_items.clear();
        for (const auto& s : visible_sounds) {
            std::stringstream ss;
            ss << std::left << std::setw(25) << s.name.substr(0, 23) << " | "
                << std::setw(15) << s.username.substr(0, 15) << " | "
                << s.added_date.substr(0, 10);
            menu_items.push_back(ss.str());
        }
    }

    void reloadDbAndRefresh() {
        all_sounds = db.getAllSounds();
        refreshLibraryUI("");
    }
};

// ============================================================================
// Auth Logic
// ============================================================================
std::string performAuthCLI(bool& is_offline) {
    std::cout << "=== Freesound Setup ===\n";
    std::cout << "Press ENTER for Offline Mode.\n";

    std::string auth_url = OAuth2::buildAuthorizationUrl();

    std::cout << "Auth URL: " << auth_url << "\nCode: ";
    std::string code;
    std::getline(std::cin, code);
    code = cleanInput(code);

    if (code.empty()) {
        is_offline = true;
        return "";
    }
    std::string response = OAuth2::exchangeCodeForToken(code);
    return OAuth2::extractAccessToken(response);
}

// ============================================================================
// Main
// ============================================================================
int main() {
    auto app_state = std::make_shared<AppState>();
    app_state->db.initSchema();

    std::string token = performAuthCLI(app_state->offline_mode);
    if (!app_state->offline_mode) {
        app_state->downloader = std::make_unique<Downloader>(token, "./sounds");
    }

    // Initial Load
    app_state->all_sounds = app_state->db.getAllSounds();
    app_state->refreshLibraryUI();

    auto screen = ScreenInteractive::Fullscreen();
    Component lib_input;
    Component search_input;

    // =========================
    // TAB 1: LIBRARY
    // =========================
    std::string lib_query_str;
    auto sort_radio = Radiobox(&app_state->sort_options, &app_state->sort_selected);

    InputOption lib_input_opt;
    lib_input_opt.on_enter = [&] {
        std::string q = cleanInput(lib_query_str);
        lib_query_str = q;
        // Direct call safe here because on_enter is Main Thread
        app_state->refreshLibraryUI(q);
        app_state->status_message = "Filtered: " + q;
        };
    lib_input = Input(&lib_query_str, "Search library...", lib_input_opt);

    // Hook into Sort change
    auto sort_container = Container::Vertical({ sort_radio }) | CatchEvent([&](Event e) {
        if (e == Event::Return || e == Event::Character(' ')) {
            app_state->refreshLibraryUI(cleanInput(lib_query_str));
        }
        return false;
        });

    int lib_selected = 0;
    MenuOption menu_opt;
    menu_opt.on_enter = [&] {
        if (lib_selected < app_state->visible_sounds.size()) {
            auto& s = app_state->visible_sounds[lib_selected];
            app_state->status_message = "Playing: " + s.name;
            app_state->player.play(fs::absolute(s.file_path).string());

            app_state->current_sound = s;
        }
        };
    auto lib_menu = Menu(&app_state->menu_items, &lib_selected, menu_opt);

    auto library_component = Container::Vertical({
        lib_input,
        Container::Horizontal({
            Renderer([] { return text("Sort: ") | center; }),
            sort_container
        }),
        Renderer([] { return separator(); }),
        lib_menu
        });

    // =========================
    // TAB 2: ONLINE SEARCH
    // =========================
    auto search_component = Container::Vertical({});

    if (!app_state->offline_mode) {
        // We must keep search_query persistent so Input can bind to it
        static std::string search_query;
        static int search_selected = 0;

        InputOption s_opt;
        s_opt.on_enter = [&] {
            std::string cleaned_q = cleanInput(search_query);
            search_query = cleaned_q;
            app_state->status_message = "Searching: " + cleaned_q;

            // Launch Thread
            std::thread([app_state, &screen, cleaned_q] {
                auto res = app_state->downloader->searchSounds(cleaned_q, 15);

                // CRITICAL: Update UI Data via Task Queue
                app_state->postTask([app_state, res, cleaned_q]() {
                    app_state->search_results.clear();
                    app_state->search_menu_items.clear();

                    if (res.contains("results")) {
                        for (const auto& item : res["results"]) {
                            // Store ID and Name as before
                            app_state->search_results.push_back({ item["id"], item["name"] });

                            // FORMAT DISPLAY STRING
                            // Example: "Explosion.wav | user123 | 44.1kHz | 2.5MB"

                            std::string name = item.value("name", "Unknown");
                            std::string user = item.value("username", "Unknown");
                            int sr = item.value("samplerate", 0);
                            int size_bytes = item.value("filesize", 0);
                            double size_mb = size_bytes / (1024.0 * 1024.0);

                            std::stringstream ss;
                            ss << std::left << std::setw(30) << name.substr(0, 28) << " | "
                                << std::setw(15) << user.substr(0, 15) << " | "
                                << (sr / 1000) << "kHz | "
                                << std::fixed << std::setprecision(1) << size_mb << "MB";

                            app_state->search_menu_items.push_back(ss.str());
                        }
                        app_state->status_message = "Results for '" + cleaned_q + "': " + std::to_string(app_state->search_results.size());
                    }
                    else {
                        app_state->status_message = "No results.";
                    }
                    }, &screen);
                }).detach();
            };

        search_input = Input(&search_query, "Search Freesound...", s_opt);

        MenuOption dl_opt;
        dl_opt.on_enter = [&] {
            int idx = search_selected;
            if (idx < app_state->search_results.size()) {
                int id = app_state->search_results[idx].first;
                app_state->status_message = "Downloading ID " + std::to_string(id) + "...";

                std::thread([app_state, &screen, id] {
                    std::string path;
                    bool success = app_state->downloader->downloadSound(id, path);

                    // CRITICAL: Database write and UI update via Task Queue
                    app_state->postTask([app_state, success, id, path]() {
                        if (success) {
                            // Mock metadata - in real app, fetch it
                            json meta = app_state->downloader->getSoundMetadata(id);
                            Sound s;
                            s.id = id;
                            s.file_path = path;
                            s.name = meta.value("name", "Unknown");
                            s.username = meta.value("username", "Unknown");
                            s.duration = meta.value("duration", 0.0);

                            // Handle added_date manually if not in JSON
                            auto t = std::time(nullptr);
                            auto tm = *std::localtime(&t);
                            std::stringstream ss;
                            ss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
                            s.added_date = ss.str();

                            app_state->db.addSound(s);
                            app_state->reloadDbAndRefresh(); // Updates UI vectors safely
                            app_state->status_message = "Downloaded: " + s.name;
                        }
                        else {
                            app_state->status_message = "Download Failed.";
                        }
                        }, &screen);
                    }).detach();
            }
            };

        auto search_menu = Menu(&app_state->search_menu_items, &search_selected, dl_opt);

        search_component = Container::Vertical({
            search_input,
            Renderer([] { return separator(); }),
            search_menu
            });
    }
    else {
        search_component = Renderer([] {
            return text("OFFLINE MODE") | center | color(Color::Red);
            });
    }

    // =========================
    // TAB 3: PLAYER
    // =========================

    auto player_component = Renderer([&] {
        auto wave_data = app_state->player.getWaveformData();

        // 1. Setup Dimensions
        // Use the persistent graph_box from AppState
        int term_width = app_state->graph_box.x_max - app_state->graph_box.x_min;
        if (term_width <= 0) term_width = 120; // Fallback default

        int c_width = term_width * 2; // Double resolution for Braille (2 dots per char)
        int c_height = 40;

        Canvas c = Canvas(c_width, c_height);

        // 2. Draw Waveform with baked-in Color
        if (!wave_data.empty()) {
            int mid_y = c_height / 2;
            float step = (float)wave_data.size() / (float)c_width;

            for (int x = 0; x < c_width; ++x) {
                // --- Peak Detection ---
                int start = (int)(x * step);
                int end = (int)((x + 1) * step);

                if (start >= wave_data.size()) break;
                if (end > wave_data.size()) end = wave_data.size();

                float max_val = 0.0f;
                for (int i = start; i < end; ++i) {
                    float v = std::abs(wave_data[i]);
                    if (v > max_val) max_val = v;
                }

                // Clamp value to prevent drawing errors
                max_val = (std::min)(1.0f, max_val);

                // --- Color Calculation ---
                // Calculate color based on how "loud" this specific column is.
                // Quiet = Cyan, Loud = Red
                Color point_color = Color::Interpolate(max_val * 10.0f, Color::Cyan, Color::Red);

                int h = (int)(max_val * mid_y);

                // Draw the vertical bar for this time slice
                for (int y = mid_y - h; y <= mid_y + h; ++y) {
                    // PASS COLOR DIRECTLY TO DRAWPOINT
                    // This forces the canvas to store the color for this dot
                    c.DrawPoint(x, y, true, point_color);
                }
            }
        }

        // 3. Render
        return vbox({
            filler(),
            vbox({
                text(app_state->current_sound.name) | bold | center | color(Color::Cyan),
                text("by " + app_state->current_sound.username) | center | color(Color::White)
            }) | borderRounded | center,
            filler(),

            // Canvas Container
            hbox({
                canvas(std::move(c))
                | reflect(app_state->graph_box) // Capture size for next frame
                | flex                          // Stretch to fill width
            })
            | size(HEIGHT, EQUAL, 10)
            | border,

            filler()
            });
        });


    // =========================
    // LAYOUT & EVENT LOOP
    // =========================
    int tab_index = 0;
    std::vector<std::string> tab_names = { "Library", "Search Online", "Player"};
    auto tab_toggle = Toggle(&tab_names, &tab_index);
    auto tab_content = Container::Tab({ 
        library_component, 
        search_component , 
        player_component
        }, 
        &tab_index);

    auto main_container = Container::Vertical({
        Container::Horizontal({ tab_toggle }),
        Renderer([] { return separator(); }),
        tab_content
        });

    // Global Event Handler
    main_container |= CatchEvent([&](Event event) {
        // 1. Process Async Tasks
        if (event == Event::Custom) {
            app_state->processTasks();
            return true;
        }

        // 2. Global Hotkeys
        if (event == Event::Character(' ')) {
            // FIX: Check if user is typing in an Input box
            if (lib_input->Focused()) return false; // Let Input type the space
            if (search_input && search_input->Focused()) return false; // Let Search type the space

            // If not typing, toggle playback
            app_state->togglePlayback();
            return true;
        }

        // 3. Fix Sort Update on Arrow Keys
        if (tab_index == 0) {
            static int last_sort = -1;
            if (app_state->sort_selected != last_sort) {
                app_state->refreshLibraryUI(cleanInput(lib_query_str));
                last_sort = app_state->sort_selected;
            }
        }
        return false;
        });

    auto renderer = Renderer(main_container, [&] {
        float p = app_state->player.getProgress();
        return vbox({
            hbox({
                text(" AUDIO PLAYER ") | bold | bgcolor(Color::Blue),
                filler(),
                text(" " + app_state->status_message + " ") | color(Color::Yellow)
            }),
            separator(),
            tab_toggle->Render(),
            separator(),
            tab_content->Render() | flex,
            separator(),
            hbox({
                text(" Status: "),
                gauge(p) | flex,
                text(" " + std::to_string((int)(p * 100)) + "% ")
            }) | color(Color::Cyan)
            }) | border;
        });

    // Background ticker
    std::atomic<bool> running{ true };
    std::thread refresher([&] {
        while (running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            screen.Post(Event::Custom);
        }
        });

    screen.Loop(renderer);
    running = false;
    refresher.join();

    return 0;
}