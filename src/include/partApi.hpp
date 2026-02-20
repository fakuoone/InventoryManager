#pragma once

#include "config.hpp"
#include "logger.hpp"
#include "threadPool.hpp"
#include "userInterface/uiTypes.hpp"

#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <string>

#define DUMMY_API

struct CurlDeleter {
    void operator()(CURL* c) const {
        if (c) {
            curl_easy_cleanup(c);
        }
    }
};

struct CurlListDeleter {
    void operator()(curl_slist* list) const {
        if (list) {
            curl_slist_free_all(list);
        }
    }
};

class PartApi {
  private:
    ThreadPool& pool;
    Config& config;
    Logger& logger;

    std::unique_ptr<CURL, CurlDeleter> curl; // TODO: create 1 handle per call for threadsafety
    std::unique_ptr<curl_slist, CurlListDeleter> headers;

    std::string responseString;
    nlohmann::json responseJson;

    static std::size_t writeCallback(void* contents, size_t size, size_t nmemb, void* instance) {
        PartApi* self = static_cast<PartApi*>(instance);
        size_t totalSize = size * nmemb;
        self->responseString.append(static_cast<char*>(contents), totalSize);
        return totalSize;
    }

    bool init() {
        if (curl) {
            return true;
        }
        logger.pushLog(Log{"API: Initializing connection."});
        curl.reset(curl_easy_init());
        if (!curl) {
            logger.pushLog(Log{"ERROR: Initializing api-connection failed."});
            return false;
        }
        const ApiConfig& apiConfig = config.getApiConfig();
        const std::string url = std::format("{}?apiKey={}", apiConfig.address, apiConfig.key);

        curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, writeCallback);
        curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, this);

        curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYHOST, 2L);

        return true;
    }

    CURLcode triggerRequest() {
#ifdef DUMMY_API
        responseString = config.getDummyJson();
        return CURLE_OK;
#else
        responseString.clear();
        CURLcode res = curl_easy_perform(curl.get());
        if (res != CURLE_OK) {
            logger.pushLog(Log{std::format("ERROR: API request failed: ", curl_easy_strerror(res))});
            return res;
        }
        return CURLE_OK;
#endif
    }

    bool parseData() {
        try {
            responseJson = nlohmann::json::parse(responseString);
            return true;
        } catch (const nlohmann::json::parse_error& e) {
            logger.pushLog(Log{std::format("ERROR: Could not parse api respone: {}", e.what())});
            return false;
        }
    }

    std::string formSearchPattern(const std::string& item) {
        std::string searchPattern = config.getSearchPattern();
        size_t pos = 0;
        while ((pos = searchPattern.find(config.ITEM_PLACE_HOLDER, pos)) != std::string::npos) {
            searchPattern.replace(pos, config.ITEM_PLACE_HOLDER.length(), item);
            pos += item.length();
        }
        return searchPattern;
    }

  public:
    PartApi(ThreadPool& cPool, Config& cConfig, Logger& cLogger) : pool(cPool), config(cConfig), logger(cLogger) {}
    ~PartApi() {}

    PartApi(const PartApi&) = delete;
    PartApi& operator=(const PartApi&) = delete;
    PartApi(PartApi&&) = delete;
    PartApi& operator=(PartApi&&) = delete;

    static void initGlobalCurl() { curl_global_init(CURL_GLOBAL_DEFAULT); }

    static void cleanupGlobalCurl() { curl_global_cleanup(); }

    nlohmann::json fetchDataPoint(const std::string& dataPoint) {
        // TODO: Proper function
        return config.getApiConfig().dummyJson;
    }

    void fetchExample(const std::string& dataPoint, ApiPreviewState& state) {
        // TODO: make a threaded wrapper around a "fetchInternal-function"
        if (!init()) {
            return;
        }
        state.fields.clear();
        responseString.clear();

        std::string searchPattern = formSearchPattern(dataPoint);
        curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE, searchPattern.size());
        curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, searchPattern.c_str());

        headers.reset();
        curl_slist* raw = nullptr;
        raw = curl_slist_append(raw, "Content-Type: application/json");
        raw = curl_slist_append(raw, "Accept: application/json");
        headers.reset(raw);

        curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers.get());

        if (triggerRequest() != CURLE_OK) {
            state.loading = false;
            return;
        }

        if (parseData()) {
            state.fields = responseJson;
            state.ready = true;
            state.loading = false;
        }
    }

    void disconnect() {}
};