#pragma once

#include <sqlite3.h>
#include <string>
#include <vector>
#include <iostream>
#include <stdexcept>

struct Sound {
    int id = 0;
    std::string name;
    std::string username;
    std::string url;
    double duration = 0.0;
    int samplerate = 0;
    int filesize = 0;
    std::string file_path;
    std::string added_date;
};

struct Playlist {
    int id;
    std::string name;
    std::string created_date;
};

class Database {
private:
    sqlite3* db;
    std::string db_path;

public:
    Database(const std::string& path);
    ~Database();

    void initSchema();

    // Sound operations
    void addSound(const Sound& sound);
    bool soundExists(int id);
    std::vector<Sound> getAllSounds();
    std::vector<Sound> getSoundsSorted(const std::string& sort_by);

    // Playlist operations
    void createPlaylist(const std::string& name);
    int getPlaylistId(const std::string& name);
    std::vector<Playlist> getAllPlaylists();
    void addSoundToPlaylist(int playlist_id, int sound_id);
    void removeSoundFromPlaylist(int playlist_id, int sound_id);
    std::vector<Sound> getPlaylistSounds(int playlist_id);
    void deletePlaylist(int playlist_id);
};

inline Database::Database(const std::string& path)
    : db_path(path) {
    int rc = sqlite3_open(path.c_str(), &db);
    if (rc) {
        throw std::runtime_error("Cannot open database: " + path);
    }
}

inline Database::~Database() {
    if (db) {
        sqlite3_close(db);
    }
}

inline void Database::initSchema() {
    // Note: If you have an existing DB, delete it so this new schema applies
    const char* sql =
        "CREATE TABLE IF NOT EXISTS sounds ("
        "  id INTEGER PRIMARY KEY,"
        "  name TEXT NOT NULL,"
        "  username TEXT,"          // Added column
        "  url TEXT NOT NULL,"
        "  duration REAL,"
        "  samplerate INTEGER,"
        "  filesize INTEGER,"
        "  file_path TEXT,"
        "  added_date TIMESTAMP DEFAULT CURRENT_TIMESTAMP"
        ");"
        "CREATE TABLE IF NOT EXISTS playlists ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  name TEXT UNIQUE NOT NULL,"
        "  created_date TIMESTAMP DEFAULT CURRENT_TIMESTAMP"
        ");"
        "CREATE TABLE IF NOT EXISTS playlist_sounds ("
        "  playlist_id INTEGER,"
        "  sound_id INTEGER,"
        "  PRIMARY KEY(playlist_id, sound_id),"
        "  FOREIGN KEY(playlist_id) REFERENCES playlists(id),"
        "  FOREIGN KEY(sound_id) REFERENCES sounds(id)"
        ");";

    char* err = 0;
    int rc = sqlite3_exec(db, sql, 0, 0, &err);
    if (rc != SQLITE_OK) {
        std::string msg = "SQL error: " + std::string(err ? err : "unknown");
        if (err) sqlite3_free(err);
        throw std::runtime_error(msg);
    }
}

inline void Database::addSound(const Sound& sound) {
    const char* sql =
        "INSERT OR REPLACE INTO sounds "
        "(id, name, username, url, duration, samplerate, filesize, file_path) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?)";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        throw std::runtime_error("SQL prepare error");
    }

    sqlite3_bind_int(stmt, 1, sound.id);
    sqlite3_bind_text(stmt, 2, sound.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, sound.username.c_str(), -1, SQLITE_TRANSIENT); // Bind username
    sqlite3_bind_text(stmt, 4, sound.url.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 5, sound.duration);
    sqlite3_bind_int(stmt, 6, sound.samplerate);
    sqlite3_bind_int(stmt, 7, sound.filesize);
    sqlite3_bind_text(stmt, 8, sound.file_path.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        throw std::runtime_error("SQL execution error");
    }
}

inline bool Database::soundExists(int id) {
    const char* sql = "SELECT COUNT(*) FROM sounds WHERE id = ?";
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    sqlite3_bind_int(stmt, 1, id);

    bool exists = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        exists = sqlite3_column_int(stmt, 0) > 0;
    }

    sqlite3_finalize(stmt);
    return exists;
}

inline std::vector<Sound> Database::getAllSounds() {
    return getSoundsSorted("added_date");
}

inline std::vector<Sound> Database::getSoundsSorted(const std::string& sort_by) {
    std::vector<Sound> sounds;

    std::string col = sort_by;
    // Allow sorting by username as well
    if (col != "download_count" && col != "rating" &&
        col != "duration" && col != "added_date" && col != "username" && col != "name") {
        col = "added_date";
    }

    // Added 'username' to selection list (index 2)
    std::string sql =
        "SELECT id, name, username, url, duration, samplerate, filesize, "
        "file_path, added_date FROM sounds ORDER BY " + col + (col == "name" || col == "username" ? " ASC" : " DESC");

    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Sound s;
        s.id = sqlite3_column_int(stmt, 0);
        s.name = std::string((const char*)sqlite3_column_text(stmt, 1));

        // Safe check for null username
        const char* user_text = (const char*)sqlite3_column_text(stmt, 2);
        s.username = user_text ? std::string(user_text) : "Unknown";

        s.url = std::string((const char*)sqlite3_column_text(stmt, 3));
        s.duration = sqlite3_column_double(stmt, 4);
        s.samplerate = sqlite3_column_int(stmt, 5);
        s.filesize = sqlite3_column_int(stmt, 6);
        s.file_path = std::string((const char*)sqlite3_column_text(stmt, 7));
        s.added_date = std::string((const char*)sqlite3_column_text(stmt, 8));
        sounds.push_back(s);
    }

    sqlite3_finalize(stmt);
    return sounds;
}

inline void Database::createPlaylist(const std::string& name) {
    const char* sql = "INSERT INTO playlists (name) VALUES (?)";
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) throw std::runtime_error("Failed to create playlist");
}

inline int Database::getPlaylistId(const std::string& name) {
    const char* sql = "SELECT id FROM playlists WHERE name = ?";
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    int id = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) id = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return id;
}

inline std::vector<Playlist> Database::getAllPlaylists() {
    std::vector<Playlist> playlists;
    const char* sql = "SELECT id, name, created_date FROM playlists ORDER BY created_date DESC";
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Playlist p;
        p.id = sqlite3_column_int(stmt, 0);
        p.name = std::string((const char*)sqlite3_column_text(stmt, 1));
        p.created_date = std::string((const char*)sqlite3_column_text(stmt, 2));
        playlists.push_back(p);
    }
    sqlite3_finalize(stmt);
    return playlists;
}

inline void Database::addSoundToPlaylist(int playlist_id, int sound_id) {
    const char* sql = "INSERT OR IGNORE INTO playlist_sounds (playlist_id, sound_id) VALUES (?, ?)";
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    sqlite3_bind_int(stmt, 1, playlist_id);
    sqlite3_bind_int(stmt, 2, sound_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

inline void Database::removeSoundFromPlaylist(int playlist_id, int sound_id) {
    const char* sql = "DELETE FROM playlist_sounds WHERE playlist_id = ? AND sound_id = ?";
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    sqlite3_bind_int(stmt, 1, playlist_id);
    sqlite3_bind_int(stmt, 2, sound_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

inline std::vector<Sound> Database::getPlaylistSounds(int playlist_id) {
    std::vector<Sound> sounds;
    // Updated query to include username
    const char* sql =
        "SELECT s.id, s.name, s.username, s.url, s.duration, s.samplerate, "
        "s.filesize, s.file_path, s.added_date FROM sounds s "
        "INNER JOIN playlist_sounds ps ON s.id = ps.sound_id "
        "WHERE ps.playlist_id = ?";

    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    sqlite3_bind_int(stmt, 1, playlist_id);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Sound s;
        s.id = sqlite3_column_int(stmt, 0);
        s.name = std::string((const char*)sqlite3_column_text(stmt, 1));

        const char* user_text = (const char*)sqlite3_column_text(stmt, 2);
        s.username = user_text ? std::string(user_text) : "Unknown";

        s.url = std::string((const char*)sqlite3_column_text(stmt, 3));
        s.duration = sqlite3_column_double(stmt, 4);
        s.samplerate = sqlite3_column_int(stmt, 5);
        s.filesize = sqlite3_column_int(stmt, 6);
        s.file_path = std::string((const char*)sqlite3_column_text(stmt, 7));
        s.added_date = std::string((const char*)sqlite3_column_text(stmt, 8));
        sounds.push_back(s);
    }

    sqlite3_finalize(stmt);
    return sounds;
}

inline void Database::deletePlaylist(int playlist_id) {
    const char* sql1 = "DELETE FROM playlist_sounds WHERE playlist_id = ?";
    const char* sql2 = "DELETE FROM playlists WHERE id = ?";
    sqlite3_stmt* stmt;

    sqlite3_prepare_v2(db, sql1, -1, &stmt, 0);
    sqlite3_bind_int(stmt, 1, playlist_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    sqlite3_prepare_v2(db, sql2, -1, &stmt, 0);
    sqlite3_bind_int(stmt, 1, playlist_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}
