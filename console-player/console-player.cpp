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
    // TAB 3: PLAYER (UPDATED)
    // =========================

    // Create clickable button handlers
    auto player_component = Renderer([&] {
        auto wave_data = app_state->player.getWaveformData();
        int current_frame = app_state->player.getCurrentFrame();
        int total_frames = app_state->player.getTotalFrames();
        auto state = app_state->player.getState();

        // 1. Setup Canvas Dimensions
        int term_width = app_state->graph_box.x_max - app_state->graph_box.x_min;
        if (term_width <= 0) term_width = 120;

        int c_width = term_width * 2;
        int c_height = 40;

        Canvas c = Canvas(c_width, c_height);

        // 2. Draw Waveform
        if (!wave_data.empty()) {
            int mid_y = c_height / 2;
            float step = (float)wave_data.size() / (float)c_width;

            for (int x = 0; x < c_width; ++x) {
                int start = (int)(x * step);
                int end = (int)((x + 1) * step);

                if (start >= wave_data.size()) break;
                if (end > wave_data.size()) end = wave_data.size();

                float max_val = 0.0f;
                for (int i = start; i < end; ++i) {
                    float v = std::abs(wave_data[i]);
                    if (v > max_val) max_val = v;
                }

                max_val = (std::min)(1.0f, max_val);
                Color point_color = Color::Interpolate(max_val * 10.0f, Color::Cyan, Color::Red);

                int h = (int)(max_val * mid_y);

                for (int y = mid_y - h; y <= mid_y + h; ++y) {
                    c.DrawPoint(x, y, true, point_color);
                }
            }
        }

        // 3. Calculate time strings
        double current_seconds = (total_frames > 0) ? (current_frame / (double)SAMPLE_RATE) : 0.0;
        double total_seconds = (total_frames > 0) ? (total_frames / (double)SAMPLE_RATE) : 0.0;
        std::string current_time = formatDuration(current_seconds);
        std::string total_time = formatDuration(total_seconds);

        // 4. Playback state indicator
        std::string state_str = "Paused.";
        Color state_color = Color::Yellow;
        if (state == PlaybackState::Playing) {
            state_str = "Playing.";
            state_color = Color::Green;
        }
        else if (state == PlaybackState::Stopped) {
            state_str = "Stopped.";
            state_color = Color::Red;
        }

        // 7. Assemble full player view
        return vbox({
            // Title and Artist
            filler(),
            vbox({
                text(app_state->current_sound.name) | bold | center | color(Color::Cyan),
                text("by " + app_state->current_sound.username) | center | color(Color::White)
            }) | borderRounded | center,
            filler(),

            // Waveform Canvas
            hbox({
                canvas(std::move(c))
                    | reflect(app_state->graph_box)
                    | flex
            })
            | size(HEIGHT, EQUAL, 10)
            | border,

            // Time Display
            hbox({
                text(" " + current_time) | color(Color::Cyan),
                filler(),
                text(total_time + " ") | color(Color::Cyan)
            }) | size(HEIGHT, EQUAL, 1),

            filler(),

            // Playback State
            text(state_str) | center | color(state_color),

            filler(),

            // Keyboard shortcuts help
            vbox({
                text("Keyboard: [Space]=Play/Pause | [←/→]=Seek | [Ctrl+←/→]=Tabs")
                    | center | dim | color(Color::GrayLight)
			})
            });
        });

    // =========================
    // LAYOUT & EVENT LOOP
    // =========================
    int tab_index = 0;
    std::vector<std::string> tab_names = { "Library", "Search Online", "Player" };
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

        // ============================================================
        // 2. TAB NAVIGATION (Ctrl+Left/Right)
        // ============================================================
        if (event == Event::ArrowLeftCtrl || event.input() == "<C-left>") {
            tab_index = (tab_index - 1 + 3) % 3;
            return true;
        }
        if (event == Event::ArrowRightCtrl || event.input() == "<C-right>") {
            tab_index = (tab_index + 1) % 3;
            return true;
        }

        // ============================================================
        // 3. GLOBAL HOTKEYS (Space for Play/Pause)
        // ============================================================
        if (event == Event::Character(' ')) {
            if (lib_input && lib_input->Focused()) return false;
            if (search_input && search_input->Focused()) return false;

            app_state->togglePlayback();
            return true;
        }

        // ============================================================
        // 4. PLAYER TAB: Seek Operations & Additional Controls
        // ============================================================
        if (tab_index == 2) {
            // Seek left
            if (event == Event::ArrowLeft) {
                app_state->player.seekBackward(5);
                return true;
            }
            // Seek right
            if (event == Event::ArrowRight) {
                app_state->player.seekForward(5);
                return true;
            }
            // Stop playback
            if (event == Event::Character('s') || event == Event::Character('S')) {
                app_state->player.stop();
                app_state->status_message = "Stopped.";
                return true;
            }
            // Volume/other controls can be added here
        }

        // ============================================================
        // 5. MENU NAVIGATION (Up/Down arrows - LIBRARY & SEARCH TABS)
        // ============================================================
        if (tab_index == 0 || tab_index == 1) {
            if (event == Event::ArrowUp || event == Event::ArrowDown) {
                return false;
            }
        }

        // ============================================================
        // 6. SORT UPDATE (Library Tab)
        // ============================================================
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
                text(" Playback: "),
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
