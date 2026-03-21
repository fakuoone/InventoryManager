#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <string>

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
    static constexpr std::size_t MAX_PATH = 256;
    std::string quantityColumn_;
    std::string dbString_;
    std::string fontPath_;
    ApiConfig api_;
    ReaderConfig order_;
    ReaderConfig bom_;

    Logger& logger_;

    std::string databaseJsonToDbString(const nlohmann::json& j);
    void getAdditionalConfig(const nlohmann::json& j);
    void readApiArchive();
    std::string readConfigFile(const std::filesystem::path& configPath);
    std::vector<AutoInv::SerializableMapping> readSingleMappingFile(const std::filesystem::path& path);
    void saveSingleMappingToFile(const std::vector<AutoInv::MappingNumber>& mappings, const std::filesystem::path& path);

  public:
    const std::string_view ITEM_PLACE_HOLDER = "${PART_NUMBER}";

    Config(Logger& cLogger);
    std::string setConfigString(const std::filesystem::path& configPath);
    void saveApiArchive();
    AutoInv::LoadedMappings readMappings();
    void saveMappings(const std::vector<AutoInv::MappingNumber>& mappingsBom, const std::vector<AutoInv::MappingNumber>& mappingsOrder);
    std::filesystem::path getExeDir();
    const std::string& getFont() const;
    const std::string& getQuantityColumn() const;
    const ApiConfig& getApiConfig() const;
    void setApiArchiveBuffer(DB::ProtectedData<ApiResponseType>* responses);
    nlohmann::json getDummyJson() const;
    std::string getSearchPattern() const;
    std::filesystem::path getCsvPathOrder() const;
    std::filesystem::path getCsvPathBom() const;
};