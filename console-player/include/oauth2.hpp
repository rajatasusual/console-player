#pragma once

#include <string>
#include <curl/curl.h>
#include <sstream>
#include <nlohmann/json.hpp>
#include <fstream>
#include <stdlib.h>

namespace OAuth2 {

    // to read environmental variables
    std::string getEnvVar(const std::string& key) {
        const char* val = std::getenv(key.c_str());
        return val == nullptr ? "" : std::string(val);
    }

    inline static std::string getCredential(std::string& credKey) {
        std::string cred = getEnvVar(credKey);

        if (cred.empty()) {
            try {
                std::ifstream f("config.json");
                if (!f.is_open()) {
                    std::cerr << "Error: config.json not found in execution directory.\n";
                    return "";
                }

                json config = json::parse(f);
                cred = config.value(credKey, "");

                // Check if the user actually updated the placeholders
                if (cred.empty()) {
                    std::cerr << "Error: Please update config.json with your Credentials.\n";
                    return "";
                }

                return cred;
            }
            catch (const json::parse_error& e) {
                std::cerr << "Error parsing config.json: " << e.what() << "\n";
                return "";
            }
        }
    }
    
    // Callback for CURL to write response data
    inline static size_t WriteCallback(
        void* contents, size_t size, size_t nmemb, std::string* userp
    ) {
        userp->append((char*)contents, size * nmemb);
        return size * nmemb;
    }

    // Build authorization URL
    inline std::string buildAuthorizationUrl() {
        std::string client_id_key = "FREESOUND_CLIENT_ID";
        std::string client_id = getCredential(client_id_key);
        if (client_id.empty()) {
            return NULL;
        }
        return "https://freesound.org/apiv2/oauth2/authorize/"
            "?response_type=code"
            "&client_id=" + client_id;
    }

    // Exchange authorization code for access token
    inline std::string exchangeCodeForToken(
        const std::string& auth_code
    ) {
        std::string client_id_key = "FREESOUND_CLIENT_ID";
        std::string client_id = getCredential(client_id_key);
        std::string client_secret_key = "FREESOUND_CLIENT_SECRET";
        std::string client_secret = getCredential(client_secret_key);
        
        if (client_id.empty() || client_secret.empty()) {
            return NULL;
        }

        CURL* curl = curl_easy_init();
        std::string response;

        if (curl) {
            // Build POST data
            std::string post_data =
                "client_id=" + client_id +
                "&client_secret=" + client_secret +
                "&code=" + auth_code +
                "&grant_type=authorization_code&";

            curl_easy_setopt(curl, CURLOPT_URL,
                "https://freesound.org/apiv2/oauth2/access_token/");
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data.c_str());
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

            // Perform request
            CURLcode res = curl_easy_perform(curl);
            if (res != CURLE_OK) {
                std::cerr << "CURL Error: "
                    << curl_easy_strerror(res) << std::endl;
            }

            curl_easy_cleanup(curl);
        }

        return response;
    }

    // Extract access token from JSON response (simple parsing)
    inline std::string extractAccessToken(const std::string& json_response) {
        try {
            nlohmann::json data = nlohmann::json::parse(json_response);
            if (data.contains("access_token") && data["access_token"].is_string()) {
                return data["access_token"];
            }
            else {
                return "";
            }
        }
        catch (const std::exception& e) {
            std::cerr << "JSON parse error: " << e.what() << std::endl;
            return "";
        }
    }

} // namespace OAuth2