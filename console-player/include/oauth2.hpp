#pragma once

#include <string>
#include <curl/curl.h>
#include <sstream>
#include <nlohmann/json.hpp>

namespace OAuth2 {

    // Callback for CURL to write response data
    inline static size_t WriteCallback(
        void* contents, size_t size, size_t nmemb, std::string* userp
    ) {
        userp->append((char*)contents, size * nmemb);
        return size * nmemb;
    }

    // Build authorization URL
    inline std::string buildAuthorizationUrl(
        const std::string& client_id,
        const std::string& redirect_uri
    ) {
        return "https://freesound.org/apiv2/oauth2/authorize/"
            "?response_type=code"
            "&client_id=" + client_id;
    }

    // Exchange authorization code for access token
    inline std::string exchangeCodeForToken(
        const std::string& client_id,
        const std::string& client_secret,
        const std::string& auth_code
    ) {
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