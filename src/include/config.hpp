#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include "logger.hpp"

#include <windows.h>

class Config {
   private:
    std::string dbString;
    std::string primaryKey;
    Logger& logger;

    std::string databaseJsonToDbString(const nlohmann::json& j) {
        try {
            const std::string dbname = j.at("dbname").get<std::string>();
            const std::string user = j.at("user").get<std::string>();
            const std::string password = j.at("password").get<std::string>();
            std::string connectionString = std::format("dbname={} user={} password={}", dbname, user, password);
            primaryKey = j.at("primary_key").get<std::string>();
            return connectionString;
        } catch (const nlohmann::json::exception& e) {
            logger.pushLog(Log{std::format("ERROR PARSING CONFIG: ", e.what())});
            return std::string{};
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
        return databaseJsonToDbString(config);
    }

   public:
    Config(Logger& cLogger) : logger(cLogger) {}

    std::string setConfigString(const std::filesystem::path& configPath) {
        if (configPath.empty()) {
            dbString = readConfigFile(getExeDir().parent_path() / "config/database.json");
        } else {
            dbString = readConfigFile(std::filesystem::path{configPath});
        }
        return dbString;
    }

    std::string getPrimaryKey() {
        if (primaryKey.empty()) { return std::string{"id"}; }
        return primaryKey;
    }

    std::filesystem::path getExeDir() {
        char buffer[MAX_PATH];
        GetModuleFileName(nullptr, buffer, MAX_PATH);
        return std::filesystem::path(buffer).parent_path();
    }
};