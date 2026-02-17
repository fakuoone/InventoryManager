#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include "logger.hpp"

#include <windows.h>

struct ApiConfig {
    std::string key;
    std::string address;
    std::string searchPattern;
    nlohmann::json dummyJson;
};

class Config {
  private:
    std::string quantityColumn;
    std::string dbString;
    std::string fontPath;
    ApiConfig api;

    Logger& logger;

    std::string databaseJsonToDbString(const nlohmann::json& j) {
        try {
            const std::string dbname = j.at("dbname").get<std::string>();
            const std::string user = j.at("user").get<std::string>();
            const std::string password = j.at("password").get<std::string>();
            std::string connectionString = std::format("dbname={} user={} password={}", dbname, user, password);
            return connectionString;
        } catch (const nlohmann::json::parse_error& e) {
            logger.pushLog(Log{std::format("ERROR: Could not parse {}", e.what())});
            return std::string{};
        }
    }

    void getAdditionalConfig(const nlohmann::json& j) {
        try {
            quantityColumn = j.at("quantity-column").get<std::string>();
            if (j.contains("font")) {
                fontPath = j.at("font").get<std::string>();
            }
            api.address = j["api"]["address"].get<std::string>();
            api.key = j["api"]["key"].get<std::string>();
            if (j["api"].contains("dummyJson")) {
                api.dummyJson = j["api"]["dummyJson"].get<nlohmann::json>();
            }
            api.searchPattern = j["api"]["search"].get<nlohmann::json>().dump();
        } catch (const nlohmann::json::parse_error& e) {
            logger.pushLog(Log{std::format("ERROR: Could not parse {}", e.what())});
        }
    }

    std::string readConfigFile(const std::filesystem::path& configPath) {
        logger.pushLog(Log{std::format("Reading config from {}", configPath.string())});

        std::ifstream configFile(configPath);
        if (!configFile.is_open()) {
            logger.pushLog(Log{std::format("ERROR: Could not open {}", configPath.string())});
            return std::string{};
        }

        nlohmann::json config;
        try {
            configFile >> config;
        } catch (const nlohmann::json::parse_error& e) {
            logger.pushLog(Log{std::format("ERROR: Could not parse {}", e.what())});
            return std::string{};
        }

        getAdditionalConfig(config);
        return databaseJsonToDbString(config);
    }

  public:
    const std::string_view ITEM_PLACE_HOLDER = "ITEM";

    Config(Logger& cLogger) : logger(cLogger) {}

    std::string setConfigString(const std::filesystem::path& configPath) {
        if (configPath.empty()) {
            dbString = readConfigFile(getExeDir().parent_path() / "config/database.json");
        } else {
            dbString = readConfigFile(std::filesystem::path{configPath});
        }
        return dbString;
    }

    std::filesystem::path getExeDir() {
        char buffer[MAX_PATH];
        GetModuleFileName(nullptr, buffer, MAX_PATH);
        return std::filesystem::path(buffer).parent_path();
    }

    const std::string& getFont() const { return fontPath; }

    const std::string& getQuantityColumn() const { return quantityColumn; }

    const ApiConfig& getApiConfig() const { return api; }

    const std::string getDummyJson() const { return api.dummyJson.dump(); }

    const std::string getSearchPattern() const { return api.searchPattern; }
};