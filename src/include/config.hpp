#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include <windows.h>

#include "dataTypes.hpp"
#include "logger.hpp"

using ApiResponseType = std::unordered_map<std::string, nlohmann::json>;

struct ApiConfig {
    std::string key;
    std::string address;
    std::string searchPattern;
    nlohmann::json dummyJson;
    std::filesystem::path responseArchive;
    DB::ProtectedData<ApiResponseType>* responses;
};

struct ReaderConfig {
    std::filesystem::path defaultPath;
    std::filesystem::path mappingArchive;
};

class Config {
  private:
    std::string quantityColumn;
    std::string dbString;
    std::string fontPath;
    ApiConfig api;
    ReaderConfig order;
    ReaderConfig bom;

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
            if (j.contains("font")) { fontPath = j.at("font").get<std::string>(); }

            // API
            api.address = j["api"]["address"].get<std::string>();
            api.key = j["api"]["key"].get<std::string>();
            if (j["api"].contains("dummyJson")) { api.dummyJson = j["api"]["dummyJson"].get<nlohmann::json>(); }
            api.searchPattern = j["api"]["search"].get<nlohmann::json>().dump();
            if (j["api"].contains("responseArchive")) {
                api.responseArchive = j["api"]["responseArchive"].get<std::filesystem::path>();
                readApiArchive();
            } else {
                logger.pushLog(
                    Log{"INFORMATION: API storage feature not specified in config. This will lead to increased api request rate."});
            }

            // DEFAULT CSV
            if (j.contains("order")) {
                order.defaultPath = j["order"]["defaultPath"].get<std::filesystem::path>();
                if (j["order"].contains("mappingArchive")) {
                    order.mappingArchive = j["order"]["mappingArchive"].get<std::filesystem::path>();
                }
            }
            if (j.contains("bom")) {
                bom.defaultPath = j["bom"]["defaultPath"].get<std::filesystem::path>();
                if (j["bom"].contains("mappingArchive")) { bom.mappingArchive = j["bom"]["mappingArchive"].get<std::filesystem::path>(); }
            }
        } catch (const nlohmann::json::parse_error& e) { logger.pushLog(Log{std::format("ERROR: Could not parse {}", e.what())}); }
    }

    void readApiArchive() {
        if (!api.responses) { return; }
        std::ifstream archive(api.responseArchive);
        if (!archive) { return; }

        nlohmann::json j;
        archive >> j;

        if (!j.is_object()) {
            logger.pushLog(Log{std::format("WARNIG: Api-Archive with path: {} is specified but contents are incorrectly formatted.",
                                           api.responseArchive.string().c_str())});
            return;
        }
        {
            std::lock_guard<std::mutex> lg{api.responses->mtx};
            api.responses->data = std::move(j.get<ApiResponseType>());
            api.responses->ready = true;
            logger.pushLog(Log{"Loaded api archive from file."});
        }
        return;
    }

    std::string readConfigFile(const std::filesystem::path& configPath) {

        std::ifstream configFile(configPath);
        if (!configFile.is_open()) {
            logger.pushLog(Log{std::format("ERROR: Could not open {}", configPath.string())});
            return std::string{};
        } else {
            logger.pushLog(Log{std::format("Reading config from {}", configPath.string())});
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

    std::vector<AutoInv::SerializableMapping> readSingleMappingFile(const std::filesystem::path& path) {
        std::vector<AutoInv::SerializableMapping> result;
        std::ifstream mappingFile(path);
        if (!mappingFile.is_open()) {
            logger.pushLog(Log{std::format("ERROR: Could not open {}", path.string())});
            return result;
        }

        nlohmann::json mappings;
        try {
            mappingFile >> mappings;
        } catch (const nlohmann::json::parse_error& e) {
            logger.pushLog(Log{std::format("ERROR: Could not parse {}", e.what())});
            return result;
        }

        for (auto& entry : mappings) {
            AutoInv::SourceType sourceType = entry["sourceType"];
            AutoInv::MappingVariant variant;
            std::string type = entry["type"];

            if (type == "CsvToDb") {
                AutoInv::MappingCsvToDb m;
                m.source = entry["source"].get<AutoInv::PreciseMapLocation>();
                m.destination = entry["destination"].get<AutoInv::PreciseMapLocation>();
                variant = m;
            } else if (type == "CsvApi") {
                AutoInv::MappingCsvApi m;
                m.source = entry["source"].get<std::string>();
                m.destination = entry["destination"].get<uint32_t>();
                variant = m;
            }

            result.emplace_back(variant, sourceType);
        }

        return result;
    }

    void saveSingleMappingToFile(const std::vector<AutoInv::MappingNumber>& mappings, const std::filesystem::path& path) {
        std::ofstream mappingFile(path);
        if (!mappingFile.is_open()) {
            logger.pushLog(Log{std::format("ERROR: Cant open mapping file: {}.", path.string())});
            return;
        }

        nlohmann::json j = nlohmann::json::array();
        for (const auto& m : mappings) {
            nlohmann::json entry;
            AutoInv::SerializableMapping serMapping = m;
            entry["sourceType"] = m.sourceType;

            std::visit(
                [&](auto&& mapping) {
                    using T = std::decay_t<decltype(mapping)>;
                    if constexpr (std::is_same_v<T, AutoInv::MappingCsvToDb>) {
                        entry["type"] = "CsvToDb";
                        entry["source"] = mapping.source;
                        entry["destination"] = mapping.destination;
                    } else if constexpr (std::is_same_v<T, AutoInv::MappingCsvApi>) {
                        entry["type"] = "CsvApi";
                        entry["source"] = mapping.source;
                        entry["destination"] = mapping.destination;
                    }
                },
                m.usableData);

            j.push_back(entry);
        }

        mappingFile << j.dump();
    }

  public:
    const std::string_view ITEM_PLACE_HOLDER = "${PART_NUMBER}";

    Config(Logger& cLogger) : logger(cLogger) {}

    std::string setConfigString(const std::filesystem::path& configPath) {
        if (configPath.empty()) {
            dbString = readConfigFile(getExeDir().parent_path() / "config/database.json");
        } else {
            dbString = readConfigFile(std::filesystem::path{configPath});
        }
        return dbString;
    }

    void saveApiArchive() {
        if (!api.responses) { return; }
        if (api.responseArchive.empty()) { return; }
        nlohmann::json j;
        {
            std::lock_guard<std::mutex> lg{api.responses->mtx};
            j = nlohmann::json(api.responses->data);
        }

        std::ofstream archive(api.responseArchive);
        if (!archive) { return; }

        archive << j.dump();
        logger.pushLog(Log{"Saved api archive to file."});
    }

    AutoInv::LoadedMappings readMappings() {
        AutoInv::LoadedMappings mappings;
        if (!order.mappingArchive.empty()) { mappings.order = readSingleMappingFile(order.mappingArchive); }
        if (!bom.mappingArchive.empty()) { mappings.bom = readSingleMappingFile(bom.mappingArchive); }
        return mappings;
    }

    void saveMappings(const std::vector<AutoInv::MappingNumber>& mappingsBom, const std::vector<AutoInv::MappingNumber>& mappingsOrder) {
        if (!order.mappingArchive.empty() && !mappingsOrder.empty()) { saveSingleMappingToFile(mappingsOrder, order.mappingArchive); }
        if (!bom.mappingArchive.empty() && !mappingsBom.empty()) { saveSingleMappingToFile(mappingsBom, bom.mappingArchive); }
    }

    std::filesystem::path getExeDir() {
        char buffer[MAX_PATH];
        GetModuleFileName(nullptr, buffer, MAX_PATH);
        return std::filesystem::path(buffer).parent_path();
    }

    const std::string& getFont() const { return fontPath; }

    const std::string& getQuantityColumn() const { return quantityColumn; }

    const ApiConfig& getApiConfig() const { return api; }

    void setApiArchiveBuffer(DB::ProtectedData<ApiResponseType>* responses) { api.responses = responses; }

    nlohmann::json getDummyJson() const { return api.dummyJson; }

    const std::string getSearchPattern() const { return api.searchPattern; }

    const std::filesystem::path getCsvPathOrder() const { return order.defaultPath; }

    const std::filesystem::path getCsvPathBom() const { return bom.defaultPath; }
};