#pragma once

#include "changeTracker.hpp"
#include "dbService.hpp"
#include "logger.hpp"
#include "partApi.hpp"
#include "threadPool.hpp"

#include <set>

namespace AutoInv {
struct TableCells {
    std::string table;
    Change::colValMap cells;
};

struct PreciseMapLocationCombined {
    // helper for differentiating api and csv
    PreciseMapLocation locations;
    SourceType source;
};

struct TargetData {
    std::vector<PreciseMapLocationCombined> dbHeaders;
};

struct ChangeConvertedMapping {
    std::vector<std::size_t> columnIndexes;                     // csv columns that actually have a mapping
    std::unordered_map<std::size_t, TargetData> preciseHeaders; // column-index -> [dbHeaders]
    std::map<std::string, TableCells> cells;                    // storage for change-cels
    std::vector<TableCells*> orderedCells;                      // ordered version for optimal change execution
};

using ApiResultType = std::vector<std::unordered_map<std::string, Change::colValMap>>;

class CsvChangeGenerator {
  protected:
    ThreadPool& pool_;
    ChangeTracker& changeTracker_;
    DbService& dbService_;
    PartApi& partApi_;
    Config& config_;
    Logger& logger_;

    std::shared_ptr<const CompleteDbData> dbData_;

    std::filesystem::path lastSuccessfulCsvPath_;

    CSV::Data csvData_;
    std::future<bool> fRead_;
    std::condition_variable cvRead_;
    std::mutex mtxRead_;

    std::future<void> fExecMappings_;

    bool dataRead_ = false;

    std::vector<MappingCsvToDb> directMappings_;
    std::vector<MappingCsvToDb> indirectApiMappings_;
    std::vector<MappingCsvApi> intermediateApiMappings_;
    std::size_t missingParam_ = 0;

    std::set<std::size_t> failedCsvApiRows_;
    std::mutex failedCsvApiMtx_;

    QuantityOperation operation_;

    CsvChangeGenerator(ThreadPool& cThreadPool,
                       ChangeTracker& cChangeTracker,
                       DbService& cDbService,
                       PartApi& cPartApi,
                       Config& cConfig,
                       Logger& cLogger,
                       QuantityOperation cOperation);
    virtual ~CsvChangeGenerator() = default;

    bool run(std::filesystem::path csv);
    void convertMappings(ChangeConvertedMapping& convertedMapping,
                         std::unordered_set<std::string>& foundTables,
                         const std::vector<MappingCsvToDb>& mappings,
                         SourceType source);
    ChangeConvertedMapping convertMapping();
    const MappingCsvApi& findApiSource(MappingIdType mappingId) const;
    std::string getJsonTarget(const nlohmann::json& j, const std::string& selectedField);
    void fetchChunk(std::span<std::vector<std::string>> chunk,
                    std::span<std::unordered_map<std::string, Change::colValMap>> resultChunk,
                    std::size_t chunkStart);
    ApiResultType fetchApiData();
    void applyMappingToRow(const std::vector<std::string>& row,
                           ChangeConvertedMapping& mapped,
                           std::unordered_map<std::string, Change::colValMap>& apiData);
    void fillInAdditional(ChangeConvertedMapping& mapped);
    void sortMappedCells(ChangeConvertedMapping& mapped);
    void addChangesFromMapping(ChangeConvertedMapping& mapped);
    bool processCell(TableCells* cells, bool onlyAddIfFound = false);
    void executeCsv();
    void writeBackFailedRows();

  public:
    std::mutex& getMutexRead();
    std::condition_variable& getCvRead();
    void setData(std::shared_ptr<const CompleteDbData> newData);
    bool dataValid(bool once);
    void read(std::filesystem::path csv);
    const std::vector<std::string>& getHeader();
    const std::vector<DB::TypeCategory>& getHeaderTypes();
    const std::vector<std::string>& getFirstRow();
    void setMappingsToDb(const std::vector<MappingNumber> mappings);
    void reqExecuteCsv();
};

class ChangeGeneratorFromBom : public CsvChangeGenerator {
  private:
  public:
    ChangeGeneratorFromBom(
        ThreadPool& cPool, ChangeTracker& cChangeTracker, DbService& cDbService, PartApi& cPartApi, Config& cConfig, Logger& cLogger)
        : CsvChangeGenerator(cPool, cChangeTracker, cDbService, cPartApi, cConfig, cLogger, QuantityOperation::SUB) {}
};

class ChangeGeneratorFromOrder : public CsvChangeGenerator {
  private:
  public:
    ChangeGeneratorFromOrder(
        ThreadPool& cPool, ChangeTracker& cChangeTracker, DbService& cDbService, PartApi& cPartApi, Config& cConfig, Logger& cLogger)
        : CsvChangeGenerator(cPool, cChangeTracker, cDbService, cPartApi, cConfig, cLogger, QuantityOperation::ADD) {}
};
}; // namespace AutoInv
