#pragma once

#include "config.hpp"
#include "dataTypes.hpp"
#include "logger.hpp"
#include "threadPool.hpp"

#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <string>

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

    static std::size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userdata);
    bool init();
    CURLcode triggerRequest(CURL* curl);
    nlohmann::json parseData(const std::string& response);
    std::string formSearchPattern(const std::string& item);
    static void initGlobalCurl();
    static void cleanupGlobalCurl();

  public:
    PartApi(ThreadPool& cPool, Config& cConfig, Logger& cLogger);
    ~PartApi();

    PartApi(const PartApi&) = delete;
    PartApi& operator=(const PartApi&) = delete;
    PartApi(PartApi&&) = delete;
    PartApi& operator=(PartApi&&) = delete;

    nlohmann::json fetchDataPoint(std::string dataPoint, bool forceRefetch = false);
    void fetchExample(std::string dataPoint, UI::ApiPreviewState& state);
};