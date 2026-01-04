#pragma once

#include <nlohmann/json.hpp>
#include <string>

class Config {
   private:
    std::string databaseJsonToString(const nlohmann::json& j) {
        std::string connectionString;
        connectionString += "dbname=" + j["dbname"].get<std::string>();
        connectionString += " user=" + j["user"].get<std::string>();
        connectionString += " password=" + j["password"].get<std::string>();
        return connectionString;
    }

    std::string readConfigFile() {
        std::string configString;
        return configString;
    }

   public:
    std::string getDatabaseString() {
        nlohmann::json jConfig = nlohmann::json::parse(readConfigFile());
        return databaseJsonToString(jConfig);
    }
};