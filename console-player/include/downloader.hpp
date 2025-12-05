#pragma once

#include <string>
#include <curl/curl.h>
#include <fstream>
#include <filesystem>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
namespace fs = std::filesystem;

inline static size_t WriteCallback(
    void* contents, size_t size, size_t nmemb,
    std::string* userp
) {
    userp->append((char*)contents, size * nmemb);
    return size * nmemb;
}

inline static size_t WriteFileCallback(
    void* contents, size_t size, size_t nmemb,
    FILE* userp
) {
    return fwrite(contents, size, nmemb, userp);
}

class Downloader {
private:
    std::string access_token;
    std::string download_dir;

public:
    Downloader(const std::string& token,
        const std::string& dir);

    // Search for sounds
    json searchSounds(const std::string& query,
        int limit = 20);

    // Download a specific sound by ID
    bool downloadSound(int sound_id,
        std::string& output_path);

    // Get sound metadata
    json getSoundMetadata(int sound_id);

    std::string getDownloadDir() { return download_dir; }
};

inline Downloader::Downloader(
    const std::string& token,
    const std::string& dir
) : access_token(token), download_dir(dir) {
    fs::create_directories(dir);
}

inline json Downloader::searchSounds(
    const std::string& query,
    int limit
) {
    CURL* curl = curl_easy_init();
    std::string response;

    if (curl) {
        std::string url = "https://freesound.org/apiv2/search/?"
            "query=" + query +
            "&page_size=" + std::to_string(limit) +
            "&fields=id,name,duration,username,filesize,samplerate";
        struct curl_slist* headers = NULL;
        std::string bearer = "Authorization: Bearer " + this->access_token;
        headers = curl_slist_append(headers, bearer.c_str());

        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

        CURLcode res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);

        if (res == CURLE_OK) {
            try {
                return json::parse(response);
            }
            catch (...) {
                std::cerr << "JSON parse error\n";
                return json::object();
            }
        }
    }

    return json::object();
}

inline json Downloader::getSoundMetadata(int sound_id) {
    CURL* curl = curl_easy_init();
    std::string response;

    if (curl) {
        std::string url = "https://freesound.org/apiv2/sounds/" +
            std::to_string(sound_id) + "/";

        std::string auth_header =
            "Authorization: Bearer " + access_token;
        struct curl_slist* headers = NULL;
        headers = curl_slist_append(headers, auth_header.c_str());

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

        curl_easy_perform(curl);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        try {
            return json::parse(response);
        }
        catch (...) {
            return json::object();
        }
    }

    return json::object();
}

inline bool Downloader::downloadSound(
    int sound_id,
    std::string& output_path
) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    std::string url = "https://freesound.org/apiv2/sounds/" +
        std::to_string(sound_id) + "/download/";

    std::string auth_header =
        "Authorization: Bearer " + access_token;
    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, auth_header.c_str());

    std::string filename = download_dir + "/" +
        std::to_string(sound_id) + ".wav";
    FILE* fp = fopen(filename.c_str(), "wb");

    if (!fp) {
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return false;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteFileCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(curl);
    fclose(fp);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    output_path = filename;
    return (res == CURLE_OK);
}