#pragma once

#include "changeTracker.hpp"
#include "dbService.hpp"
#include "logger.hpp"
#include "partApi.hpp"
#include "threadPool.hpp"

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
inline std::vector<std::string> parseLine(const std::string& line) {
    // https://stackoverflow.com/users/25450/sastanin
    enum class csvState { UNQUOTED_FIELD, QUOTED_FIELD, QUOTED_QUOTE };

    csvState state = csvState::UNQUOTED_FIELD;
    std::vector<std::string> fields{""};
    size_t i = 0; // index of the current field
    for (char c : line) {
        switch (state) {
        case csvState::UNQUOTED_FIELD:
            switch (c) {
            case ',': // end of field
                fields.push_back("");
                i++;
                break;
            case '"':
                state = csvState::QUOTED_FIELD;
                break;
            default:
                fields[i].push_back(c);
                break;
            }
            break;
        case csvState::QUOTED_FIELD:
            switch (c) {
            case '"':
                state = csvState::QUOTED_QUOTE;
                break;
            default:
                fields[i].push_back(c);
                break;
            }
            break;
        case csvState::QUOTED_QUOTE:
            switch (c) {
            case ',': // , after closing quote
                fields.push_back("");
                i++;
                state = csvState::UNQUOTED_FIELD;
                break;
            case '"': // "" -> "
                fields[i].push_back('"');
                state = csvState::QUOTED_FIELD;
                break;
            default: // end of quote
                state = csvState::UNQUOTED_FIELD;
                break;
            }
            break;
        }
    }
    return fields;
}

inline CSV::Data readData(std::filesystem::path csv, Logger& logger) {
    std::ifstream file(csv);
    if (!file.is_open()) {
        logger.pushLog(Log{std::format("ERROR: Failed to open CSV file: {}", csv.string())});
        return CSV::Data{};
    }
    std::string line;
    std::size_t prevCols = 0;
    std::size_t i = 0;
    std::vector<std::vector<std::string>> rows;

    while (std::getline(file, line)) {
        std::vector<std::string> row = parseLine(line);
        std::size_t cols = row.size();
        if (cols == 0) {
            logger.pushLog(Log{std::format("ERROR: CSV parsing failed: Row {} is empty.", i)});
            return CSV::Data{};
        }
        if (i > 0 && cols != prevCols) {
            logger.pushLog(Log{std::format("ERROR: CSV parsing failed: Row {} has different length ({} vs {}).", i, cols, prevCols)});
            return CSV::Data{};
        }
        prevCols = cols;
        rows.push_back(std::move(row));
        ++i;
    }
    if (rows.empty()) {
        logger.pushLog(Log{std::format("ERROR: CSV file '{}' is empty.", csv.string())});
        return CSV::Data{};
    }
    std::vector<DB::TypeCategory> types = CSV::determineTypes(rows);
    if (types.empty()) {
        logger.pushLog(Log{"ERROR: Failed to determine CSV column types."});
        return CSV::Data{};
    }
    return CSV::Data{std::move(rows), std::move(types)};
}

class CsvChangeGenerator {
  protected:
    ThreadPool& pool_;
    ChangeTracker& changeTracker_;
    DbService& dbService_;
    PartApi& partApi_;
    Config& config_;
    Logger& logger_;

    std::shared_ptr<const CompleteDbData> dbData_;

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

    QuantityOperation operation_;

    CsvChangeGenerator(ThreadPool& cThreadPool,
                       ChangeTracker& cChangeTracker,
                       DbService& cDbService,
                       PartApi& cPartApi,
                       Config& cConfig,
                       Logger& cLogger,
                       QuantityOperation cOperation)
        : pool_(cThreadPool), changeTracker_(cChangeTracker), dbService_(cDbService), partApi_(cPartApi), config_(cConfig),
          logger_(cLogger), operation_(cOperation) {}

    virtual ~CsvChangeGenerator() { config_.saveApiArchive(); }

    bool run(std::filesystem::path csv) {
        csvData_ = readData(csv, logger_);
        return !csvData_.rows.empty();
    }

    void convertMappings(ChangeConvertedMapping& convertedMapping,
                         std::unordered_set<std::string>& foundTables,
                         const std::vector<MappingCsvToDb>& mappings,
                         SourceType source) {
        const std::vector<std::string>& csvHeader = csvData_.rows[0];

        for (const MappingCsvToDb& mapping : mappings) {
            // check legality of mapping
            const auto it = std::find_if(
                csvHeader.begin(), csvHeader.end(), [&](const std::string& col) { return mapping.source.outerIdentifier == col; });
            if (it == csvHeader.end()) {
                logger_.pushLog(Log{std::format("ERROR: Converting mappings failed because {} does not match a csv column.",
                                                mapping.source.outerIdentifier)});
                return;
            }

            // store found tables to construct changes later
            std::size_t j = std::distance(csvHeader.begin(), it);
            if (!foundTables.contains(mapping.destination.outerIdentifier)) { foundTables.insert(mapping.destination.outerIdentifier); }

            // csv column references new db header
            convertedMapping.preciseHeaders.try_emplace(j, TargetData{std::vector<PreciseMapLocationCombined>{}});

            // prepare cells for later use
            convertedMapping.cells.try_emplace(mapping.destination.outerIdentifier,
                                               TableCells{mapping.destination.outerIdentifier, Change::colValMap{}});

            // add mapping-destination to the csv header (1 to n)
            convertedMapping.preciseHeaders[j].dbHeaders.push_back(PreciseMapLocationCombined(mapping.destination, source));
            convertedMapping.columnIndexes.push_back(j);
        }
    }

    ChangeConvertedMapping convertMapping() {
        // gets the indexes of mapped csv-columns and an indexmapped table
        ChangeConvertedMapping convertedMapping;
        std::unordered_set<std::string> foundTables;

        convertMappings(convertedMapping, foundTables, directMappings_, SourceType::CSV);
        convertMappings(convertedMapping, foundTables, indirectApiMappings_, SourceType::API);

        return convertedMapping;
    }

    const MappingCsvApi& findApiSource(MappingIdType mappingId) const {
        auto it = std::find_if(intermediateApiMappings_.begin(), intermediateApiMappings_.end(), [&](const MappingCsvApi& m) {
            return m.destination == mappingId;
        });
        // if this doesnt exist, might as well crash
        return *it;
    }

    std::string getJsonTarget(const nlohmann::json& j, const std::string& selectedField) {
        std::string pointer = "/" + std::string(selectedField);
        std::replace(pointer.begin(), pointer.end(), '/', '/');

        try {
            // logger.pushLog(Log{std::format("INFO: Checking api response:  \n{}", j.dump())});
            auto& value = j.at(nlohmann::json::json_pointer(pointer));
            if (value.is_string()) {
                return value.get<std::string>();
            } else {
                return value.dump();
            }
        } catch (nlohmann::json::exception& e) {
            // handle error
            logger_.pushLog(Log{std::format("ERROR: Api response doesnt cotain {}", selectedField)});
            return std::string{};
        }
    }

    void fetchChunk(std::span<std::vector<std::string>> chunk,
                    std::span<std::unordered_map<std::string, Change::colValMap>> resultChunk,
                    std::size_t i) {
        if (resultChunk.size() != chunk.size()) { return; }
        for (std::size_t j = 0; j < chunk.size(); ++j) {
            resultChunk[j] = std::unordered_map<std::string, Change::colValMap>{};
            const std::vector<std::string>& row = chunk[j];
            for (const MappingCsvToDb& mapping : indirectApiMappings_) {
                // gets index of column to search with
                auto itHeaderIndex = std::find(csvData_.rows[0].begin(), csvData_.rows[0].end(), mapping.source.outerIdentifier);
                if (itHeaderIndex == csvData_.rows[0].end()) { return; }
                std::size_t itCsvIndex = itHeaderIndex - csvData_.rows[0].begin();
                resultChunk[j].try_emplace(mapping.destination.outerIdentifier, Change::colValMap{});

                resultChunk[j]
                    .at(mapping.destination.outerIdentifier)
                    .emplace(mapping.destination.innerIdentifier,
                             getJsonTarget(partApi_.fetchDataPoint(chunk[j][itCsvIndex]), mapping.source.innerIdentifier));
            }
        }
    }

    ApiResultType fetchApiData() {
        std::size_t totalRows = csvData_.rows.size();
        if (totalRows <= 1) { return ApiResultType{}; }

        std::size_t threadCount = pool_.getAvailableThreadCount();
        std::size_t dataRows = totalRows - 1; // skip header

        threadCount = std::max(std::size_t(1), std::min(threadCount, dataRows / 10));

        std::size_t baseChunkSize = dataRows / threadCount;
        std::size_t remainder = dataRows % threadCount;
        std::size_t chunkStart = 0;

        ApiResultType results;
        std::vector<std::future<void>> futures;
        results.resize(dataRows);

        for (std::size_t i = 0; i < threadCount; ++i) {
            std::size_t currentChunkSize = baseChunkSize + (i < remainder ? 1 : 0);
            std::span<std::vector<std::string>> chunk =
                std::span(csvData_.rows).subspan(chunkStart == 0 ? 1 : chunkStart, currentChunkSize);
            std::span<std::unordered_map<std::string, Change::colValMap>> resultChunk =
                std::span(results).subspan(chunkStart, currentChunkSize);
            chunkStart += currentChunkSize;
            futures.push_back(pool_.submit(&CsvChangeGenerator::fetchChunk, this, chunk, resultChunk, i));
        }

        for (auto& f : futures) {
            f.get();
        }
        config_.saveApiArchive();
        return results;
    }

    void applyMappingToRow(const std::vector<std::string>& row,
                           ChangeConvertedMapping& mapped,
                           std::unordered_map<std::string, Change::colValMap>& apiData) {
        std::unordered_set<std::size_t> visited;
        for (std::size_t j = 0; j < mapped.columnIndexes.size(); j++) {
            const std::size_t mappedColumnIndex = mapped.columnIndexes[j];
            if (visited.contains(mappedColumnIndex)) { continue; }
            visited.emplace(mappedColumnIndex);
            const TargetData& mappedHeaders = mapped.preciseHeaders.at(mappedColumnIndex);
            for (const PreciseMapLocationCombined& preciseHeader : mappedHeaders.dbHeaders) {
                Change::colValMap& tableCells = mapped.cells.at(preciseHeader.locations.outerIdentifier).cells;
                switch (preciseHeader.source) {
                case SourceType::CSV:
                    tableCells.emplace(preciseHeader.locations.innerIdentifier, row.at(mappedColumnIndex));
                    break;
                case SourceType::API:
                    tableCells.emplace(preciseHeader.locations.innerIdentifier,
                                       apiData.at(preciseHeader.locations.outerIdentifier).at(preciseHeader.locations.innerIdentifier));
                    break;
                default:
                    break;
                }
            }
        }
    }

    void fillInAdditional(ChangeConvertedMapping& mapped) {
        // Fill in missing
        for (const auto& [_, preciseHeaders] : mapped.preciseHeaders) {
            std::unordered_set<std::string> visitedTables;
            for (const PreciseMapLocationCombined& preciseHeader : preciseHeaders.dbHeaders) {
                if (visitedTables.contains(preciseHeader.locations.outerIdentifier)) { continue; }
                visitedTables.insert(preciseHeader.locations.outerIdentifier);
                for (const auto& header : dbData_->headers.at(preciseHeader.locations.outerIdentifier).data) {
                    if (mapped.cells[preciseHeader.locations.outerIdentifier].cells.contains(header.name) || header.nullable ||
                        header.type == DB::HeaderTypes::PRIMARY_KEY) {
                        continue;
                    }
                    mapped.cells[preciseHeader.locations.outerIdentifier].cells.emplace(header.name,
                                                                                        std::format("TODO{}", missingParam_++));
                }
            }
        }
    }

    void sortMappedCells(ChangeConvertedMapping& mapped) {
        mapped.orderedCells.clear();
        mapped.orderedCells.reserve(mapped.cells.size());

        for (auto& [tableName, tableCells] : mapped.cells) {
            mapped.orderedCells.push_back(&tableCells);
        }

        std::sort(mapped.orderedCells.begin(), mapped.orderedCells.end(), [&](const TableCells* a, const TableCells* b) {
            return dbData_->headers.at(a->table).maxDepth < dbData_->headers.at(b->table).maxDepth;
        });
    }

    void addChangesFromMapping(ChangeConvertedMapping& mapped) {
        // This assumes that all changes caused by this row are children of the deepest mapping (the one which has the highest
        // db-relation-depth) This is incorrect in the general case, but a correct simplification in the use case here for example part has
        // manufacturer, therefore, if part exists, manufacturer doesnt need to be added
        if (mapped.orderedCells.empty()) { return; }
        TableCells* deepestCell = mapped.orderedCells.back();
        if (processCell(deepestCell, true)) { return; }

        for (TableCells* cells : mapped.orderedCells) {
            if (!processCell(cells)) { break; }
        }
    }

    bool processCell(TableCells* cells, bool onlyAddIfFound = false) {
        // returns wether to continue
        if (!cells) { return true; }
        ChangeType type = ChangeType::INSERT_ROW;
        IndexPKeyPair foundIndexes = dbService_.findIndexAndPKeyOfExisting(cells->table, cells->cells);
        bool found = false;
        if (foundIndexes.index != INVALID_ID) {
            if (dbService_.hasQuantityColumn(cells->table)) {
                dbService_.updateChangeQuantity(cells->table, cells->cells, foundIndexes.index, operation_);
                found = true;
                type = ChangeType::UPDATE_CELLS;
            } else {
                return true;
            }
        };

        if (!found && onlyAddIfFound) { return false; }

        ChangeAddResult result = changeTracker_.addChange(Change{cells->cells, type, dbService_.getTable(cells->table)}, foundIndexes.pkey);
        if (!ChangeTracker::gotAdded(result)) {
            logger_.pushLog(Log{std::format("ERROR: Adding change from mapping failed.")});
            return false;
        }
        cells->cells.clear();
        return true;
    }

    void executeCsv() {
        std::size_t i = 0;
        ChangeConvertedMapping mapped = convertMapping();

        std::vector<Change> changes;
        ApiResultType results = fetchApiData();
        for (const auto& row : csvData_.rows) {
            if (i == 0) {
                i++;
                continue;
            }
            applyMappingToRow(row, mapped, results.at(i - 1));
            fillInAdditional(mapped);
            sortMappedCells(mapped);
            addChangesFromMapping(mapped);
            i++;
        }
    }

  public:
    std::mutex& getMutexRead() { return mtxRead_; }

    std::condition_variable& getCvRead() { return cvRead_; }

    void setData(std::shared_ptr<const CompleteDbData> newData) { dbData_ = newData; }

    bool dataValid(bool once) {
        if (!dbData_) { return false; }
        if (!once) { return dataRead_; }
        if (fRead_.valid()) {
            if (fRead_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                {
                    std::lock_guard<std::mutex> lock(mtxRead_);
                    dataRead_ = fRead_.get();
                }
                cvRead_.notify_all();
                return dataRead_;
            }
        }
        return false;
    }

    void read(std::filesystem::path csv) { fRead_ = pool_.submit(&CsvChangeGenerator::run, this, csv); }

    const std::vector<std::string>& getHeader() { return csvData_.rows.front(); }

    const std::vector<DB::TypeCategory>& getHeaderTypes() { return csvData_.columnTypes; }

    const std::vector<std::string>& getFirstRow() { return *(csvData_.rows.begin() + 1); }

    void setMappingsToDb(const std::vector<MappingNumber> mappings) {
        std::vector<MappingCsvToDb> mappingsFromCsv;
        std::vector<MappingCsvToDb> mappingsFromApi;
        mappingsFromCsv.reserve(mappings.size());
        mappingsFromApi.reserve(mappings.size());
        for (const MappingNumber& mapping : mappings) {
            if (auto* mappingToDb = std::get_if<MappingCsvToDb>(&mapping.usableData)) {
                switch (mapping.sourceType) {
                case SourceType::API:
                    mappingsFromApi.push_back(*mappingToDb);
                    break;
                case SourceType::CSV:
                    mappingsFromCsv.push_back(*mappingToDb);
                    break;
                default:
                    break;
                }
            } else {
                // auto* mappingToApi = std::get<MappingToApi>(&mapping.usableData);
                // mappingsToApi.push_back(*mappingsToApi);
            }
        }

        for (const MappingCsvToDb& mapping : mappingsFromCsv) {
            logger_.pushLog(Log{std::format("MAPPINGS: MAPPED {} WITH {} TO {} OF {}",
                                            mapping.source.outerIdentifier,
                                            mapping.source.innerIdentifier,
                                            mapping.destination.innerIdentifier,
                                            mapping.destination.outerIdentifier)});
        }
        directMappings_ = std::move(mappingsFromCsv);
        indirectApiMappings_ = std::move(mappingsFromApi);
        reqExecuteCsv();
    }

    void reqExecuteCsv() { fExecMappings_ = pool_.submit(&CsvChangeGenerator::executeCsv, this); }
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
