#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include "logger.hpp"

class Config {
   private:
    std::string dbString;
    Logger& logger;

    std::string databaseJsonToDbString(const nlohmann::json& j) {
        try {
            const std::string dbname = j.at("dbname").get<std::string>();
            const std::string user = j.at("user").get<std::string>();
            const std::string password = j.at("password").get<std::string>();
            std::string connectionString = std::format("dbname={} user={} password={}", dbname, user, password);
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

    std::string setConfigString(const std::string configPathName) {
        dbString = readConfigFile(std::filesystem::path{configPathName});
        return dbString;
    }
};