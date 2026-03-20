#pragma once

#include "config.hpp"
#include "dataTypes.hpp"
#include "logger.hpp"
#include "threadPool.hpp"

#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <string>

#undef DUMMY_API

struct CurlDeleter {
    void operator()(CURL* c) const {
        if (c) { curl_easy_cleanup(c); }
    }
};

struct CurlListDeleter {
    void operator()(curl_slist* list) const {
        if (list) { curl_slist_free_all(list); }
    }
};

class PartApi {
  private:
    ThreadPool& pool_;
    Config& config_;
    Logger& logger_;

    DB::ProtectedData<ApiResponseType> responses_;

    static inline bool globalInit_ = false;
    static inline bool isInit_ = false;
    static inline const ApiConfig* apiConfig_;
    static inline std::string url_;

    static std::size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userdata) {
        auto* response = static_cast<std::string*>(userdata);
        size_t totalSize = size * nmemb;
        response->append(static_cast<char*>(contents), totalSize);
        return totalSize;
    }

    bool init() {
        if (isInit_) { return true; }
        apiConfig_ = &config_.getApiConfig();
        url_ = std::format("{}?apiKey={}", apiConfig_->address, apiConfig_->key);
        return true;
    }

    CURLcode triggerRequest(CURL* curl) {
        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            logger_.pushLog(Log{std::format("ERROR: API request failed: ", curl_easy_strerror(res))});
            return res;
        }
        return CURLE_OK;
    }

    nlohmann::json parseData(const std::string& response) {
        try {
            logger_.pushLog(Log{std::format("RESPONSE:\n{}", response)});
            return nlohmann::json::parse(response);
        } catch (const nlohmann::json::type_error& e) {
            logger_.pushLog(Log{std::format("ERROR: Could not parse api respone: {}", e.what())});
            return false;
        } catch (const nlohmann::json::parse_error& e) {
            logger_.pushLog(Log{std::format("ERROR: Could not parse api respone: {}", e.what())});
            return false;
        }
    }

    std::string formSearchPattern(const std::string& item) {
        std::string searchPattern = config_.getSearchPattern();
        size_t pos = 0;
        bool replaced = false;
        while ((pos = searchPattern.find(config_.ITEM_PLACE_HOLDER, pos)) != std::string::npos) {
            searchPattern.replace(pos, config_.ITEM_PLACE_HOLDER.length(), item);
            pos += item.length();
            replaced = true;
        }

        if (!replaced) { return std::string{}; }
        return searchPattern;
    }

    static void initGlobalCurl() {
        if (globalInit_) { return; }
        curl_global_init(CURL_GLOBAL_DEFAULT);
        globalInit_ = true;
    }

    static void cleanupGlobalCurl() {
        curl_global_cleanup();
        globalInit_ = false;
    }

  public:
    PartApi(ThreadPool& cPool, Config& cConfig, Logger& cLogger) : pool_(cPool), config_(cConfig), logger_(cLogger) {
        config_.setApiArchiveBuffer(&responses_);

        initGlobalCurl();
    }
    ~PartApi() { cleanupGlobalCurl(); }

    PartApi(const PartApi&) = delete;
    PartApi& operator=(const PartApi&) = delete;
    PartApi(PartApi&&) = delete;
    PartApi& operator=(PartApi&&) = delete;

    nlohmann::json fetchDataPoint(std::string dataPoint, bool forceRefetch = false) {
        if (!forceRefetch) {
            std::lock_guard<std::mutex> lg{responses_.mtx};
            if (responses_.data.contains(dataPoint)) { return responses_.data.at(dataPoint); }
        }
        if (!init()) { return nlohmann::json{}; }
        std::unique_ptr<CURL, CurlDeleter> curl = std::unique_ptr<CURL, CurlDeleter>(curl_easy_init());
        if (!curl) {
            logger_.pushLog(Log{"ERROR: Initializing api-connection failed."});
            return nlohmann::json{};
        }

#ifdef DUMMY_API
        return config.getDummyJson();
#endif
        // resultdata
        std::string responseString;

        // setup
        curl_easy_setopt(curl.get(), CURLOPT_URL, url_.c_str());
        curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, writeCallback);
        curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &responseString);

        curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYHOST, 2L);

        // data
        std::string searchPattern = formSearchPattern(dataPoint);
        curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE, searchPattern.size());
        curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, searchPattern.c_str());

        std::unique_ptr<curl_slist, CurlListDeleter> headers;

        curl_slist* raw = nullptr;
        raw = curl_slist_append(raw, "Content-Type: application/json");
        raw = curl_slist_append(raw, "Accept: application/json");
        headers.reset(raw);

        curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers.get());

        if (triggerRequest(curl.get()) != CURLE_OK) { return nlohmann::json{}; }
        nlohmann::json parsed = parseData(responseString);
        {
            std::lock_guard<std::mutex> lg{responses_.mtx};
            responses_.data.insert_or_assign(dataPoint, parsed);
        }
        return parsed;
    }

    void fetchExample(std::string dataPoint, UI::ApiPreviewState& state) {
        pool_.submit([this, dataPoint = std::move(dataPoint), &state]() {
            state.loading = true;
            state.fields = fetchDataPoint(dataPoint);
            state.loading = false;
            state.ready = true;
        });
    }
};