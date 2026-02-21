#pragma once

#include "changeTracker.hpp"
#include "dbService.hpp"
#include "logger.hpp"
#include "partApi.hpp"
#include "threadPool.hpp"

namespace AutoInv {
using MappingIdType = uint32_t;

enum class SourceType { NONE, CSV, API };

template <typename S, typename D> struct Mapping {
    S source;
    D destination;
    bool operator==(const Mapping& other) const { return other.source == source && other.destination == destination; }
};

struct PreciseMapLocation {
    std::string outerIdentifier;
    std::string innerIdentifier;
};

struct ApiData {};

using MappingCsvToDb = Mapping<PreciseMapLocation, PreciseMapLocation>;
using MappingCsvApi = Mapping<std::string, uint32_t>;
using MappingNumberInternal = Mapping<MappingIdType, MappingIdType>;

using MappingVariant = std::variant<MappingCsvToDb, MappingCsvApi>;

struct MappingNumber {
    MappingNumberInternal uniqueData;
    MappingVariant usableData;
    SourceType sourceType;
    bool operator==(const MappingNumber& other) const noexcept {
        // no need to hash the type aswell since the ids are unique
        return uniqueData.source == other.uniqueData.source && uniqueData.destination == other.uniqueData.destination &&
               sourceType == other.sourceType;
    }
    explicit MappingNumber(MappingNumberInternal cUniqueData, MappingVariant cUsableData, SourceType cSourceType)
        : uniqueData(cUniqueData), usableData(cUsableData), sourceType(cSourceType) {};
};

struct MappingHash {
    size_t operator()(const MappingNumber& m) const noexcept {
        // no need to hash the type aswell since the ids are unique
        size_t h1 = std::hash<MappingIdType>{}(m.uniqueData.source);
        size_t h2 = std::hash<MappingIdType>{}(m.uniqueData.destination);
        return h1 ^ (h2 << 1);
    }
};

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

inline std::vector<std::vector<std::string>> readData(std::filesystem::path csv, Logger& logger) {
    std::ifstream file(csv);
    std::string line;
    std::pair<std::size_t, std::size_t> colCounts = {0, 0};
    std::size_t i = 0;
    std::vector<std::vector<std::string>> rows;

    while (std::getline(file, line)) {
        std::vector<std::string> row = parseLine(line);
        colCounts.first = row.size();
        rows.push_back(row);
        if (i > 0) {
            if (colCounts.first != colCounts.second) {
                logger.pushLog(Log{std::format("ERROR: Parsing csv failed: Row {} has different length.", i)});
                return std::vector<std::vector<std::string>>{};
            }
        }
        colCounts.second = colCounts.first;
        colCounts.first = 0;
        i++;
    }
    return rows;
}

class CsvChangeGenerator {
  protected:
    ThreadPool& threadPool;
    ChangeTracker& changeTracker;
    DbService& dbService;
    PartApi& partApi;
    Config& config;
    Logger& logger;

    std::shared_ptr<const completeDbData> dbData;

    std::vector<std::vector<std::string>> csvData;
    std::future<bool> fRead;
    std::future<void> fExecMappings;

    bool dataRead = false;

    std::vector<MappingCsvToDb> directMappings;
    std::vector<MappingCsvToDb> indirectApiMappings;
    std::vector<MappingCsvApi> intermediateApiMappings;
    std::size_t missingParam = 0;

    QuantityOperation operation;

    CsvChangeGenerator(ThreadPool& cThreadPool,
                       ChangeTracker& cChangeTracker,
                       DbService& cDbService,
                       PartApi& cPartApi,
                       Config& cConfig,
                       Logger& cLogger,
                       QuantityOperation cOperation)
        : threadPool(cThreadPool), changeTracker(cChangeTracker), dbService(cDbService), partApi(cPartApi), config(cConfig),
          logger(cLogger), operation(cOperation) {}

    virtual ~CsvChangeGenerator() = default;

    bool run(std::filesystem::path csv) {
        csvData = readData(csv, logger);
        return !csvData.empty();
    }

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

    void convertMappings(ChangeConvertedMapping& convertedMapping,
                         std::unordered_set<std::string>& foundTables,
                         const std::vector<MappingCsvToDb>& mappings,
                         SourceType source) {
        const std::vector<std::string>& csvHeader = csvData[0];

        for (const MappingCsvToDb& mapping : mappings) {
            // check legality of mapping
            const auto it = std::find_if(
                csvHeader.begin(), csvHeader.end(), [&](const std::string& col) { return mapping.source.outerIdentifier == col; });
            if (it == csvHeader.end()) {
                logger.pushLog(Log{std::format("ERROR: Converting mappings failed because {} does not match a csv column.",
                                               mapping.source.outerIdentifier)});
                return;
            }

            // store found tables to construct changes later
            std::size_t j = std::distance(csvHeader.begin(), it);
            if (!foundTables.contains(mapping.destination.outerIdentifier)) {
                foundTables.insert(mapping.destination.outerIdentifier);
            }

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

        convertMappings(convertedMapping, foundTables, directMappings, SourceType::CSV);
        convertMappings(convertedMapping, foundTables, indirectApiMappings, SourceType::API);

        return convertedMapping;
    }

    const MappingCsvApi& findApiSource(MappingIdType mappingId) const {
        auto it = std::find_if(intermediateApiMappings.begin(), intermediateApiMappings.end(), [&](const MappingCsvApi& m) {
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
            logger.pushLog(Log{std::format("ERROR: Api response doesnt cotain {}", selectedField)});
            return std::string{};
        }
    }

    void fetchChunk(std::span<std::vector<std::string>> chunk,
                    std::span<std::unordered_map<std::string, Change::colValMap>> resultChunk,
                    std::size_t i) {
        if (resultChunk.size() != chunk.size()) {
            return;
        }

        std::unordered_map<std::string, nlohmann::json> responses; // TODO: potentially class member with lock
        for (std::size_t j = 0; j < chunk.size(); ++j) {
            resultChunk[j] = std::unordered_map<std::string, Change::colValMap>{};
            const std::vector<std::string>& row = chunk[j];
            for (const MappingCsvToDb& mapping : indirectApiMappings) {
                // const MappingCsvApi& apiMapping = findApiSource(mapping.source.innerIdentifier);
                resultChunk[j].try_emplace(mapping.destination.outerIdentifier, Change::colValMap{});
                nlohmann::json& result =
                    responses.try_emplace(mapping.source.outerIdentifier, partApi.fetchDataPoint(mapping.source.outerIdentifier))
                        .first->second;

                resultChunk[j]
                    .at(mapping.destination.outerIdentifier)
                    .emplace(mapping.destination.innerIdentifier, getJsonTarget(result, mapping.source.innerIdentifier));
            }
        }
    }

    ApiResultType fetchApiData(ChangeConvertedMapping& mapped) {
        // TODO: Enter api data into mapped
        std::size_t totalRows = csvData.size();
        if (totalRows <= 1) {
            return ApiResultType{};
        }

        std::size_t threadCount = threadPool.getAvailableThreadCount();
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
            std::span<std::vector<std::string>> chunk = std::span(csvData).subspan(chunkStart == 0 ? 1 : chunkStart, currentChunkSize);
            std::span<std::unordered_map<std::string, Change::colValMap>> resultChunk =
                std::span(results).subspan(chunkStart, currentChunkSize);
            chunkStart += currentChunkSize;
            futures.push_back(threadPool.submit(&CsvChangeGenerator::fetchChunk, this, chunk, resultChunk, i));
        }

        for (auto& f : futures) {
            f.get();
        }
        return results;
    }

    void applyMappingToRow(const std::vector<std::string>& row,
                           ChangeConvertedMapping& mapped,
                           std::unordered_map<std::string, Change::colValMap>& apiData) {
        std::unordered_set<std::size_t> visited;
        for (std::size_t j = 0; j < mapped.columnIndexes.size(); j++) {
            const std::size_t mappedColumnIndex = mapped.columnIndexes[j];
            if (visited.contains(mappedColumnIndex)) {
                continue;
            }
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
                if (visitedTables.contains(preciseHeader.locations.outerIdentifier)) {
                    continue;
                }
                visitedTables.insert(preciseHeader.locations.outerIdentifier);
                for (const auto& header : dbData->headers.at(preciseHeader.locations.outerIdentifier).data) {
                    if (mapped.cells[preciseHeader.locations.outerIdentifier].cells.contains(header.name) || header.nullable ||
                        header.type == headerType::PRIMARY_KEY) {
                        continue;
                    }
                    mapped.cells[preciseHeader.locations.outerIdentifier].cells.emplace(header.name, std::format("TODO{}", missingParam++));
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
            return dbData->headers.at(a->table).maxDepth < dbData->headers.at(b->table).maxDepth;
        });
    }

    void addChangesFromMapping(ChangeConvertedMapping& mapped) {
        // TODO: decide wether insert, update, delete, add / subtract instead of set
        for (TableCells* cells : mapped.orderedCells) {
            if (!cells) {
                continue;
            }
            changeType type = changeType::INSERT_ROW;
            IndexPKeyPair foundIndexes = dbService.findIndexAndPKeyOfExisting(cells->table, cells->cells);
            if (foundIndexes.index != INVALID_ID) {
                dbService.updateChangeQuantity(cells->table, cells->cells, foundIndexes.index, operation);
                type = changeType::UPDATE_CELLS;
            };
            ChangeAddResult result =
                changeTracker.addChange(Change{cells->cells, type, dbService.getTable(cells->table)}, foundIndexes.pkey);
            if (!ChangeTracker::gotAdded(result)) {
                logger.pushLog(Log{std::format("ERROR: Adding change from mapping failed.")});
                return;
            }
            cells->cells.clear();
        }
    }

    void executeCsv() {
        std::size_t i = 0;
        ChangeConvertedMapping mapped = convertMapping();

        std::vector<Change> changes;
        ApiResultType results = fetchApiData(mapped);
        for (const auto& row : csvData) {
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
    void setData(std::shared_ptr<const completeDbData> newData) { dbData = newData; }

    bool dataValid(bool once) {
        if (!dbData) {
            return false;
        }
        if (!once) {
            return dataRead;
        }
        if (fRead.valid()) {
            if (fRead.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                dataRead = fRead.get();
                return dataRead;
            }
        }
        return false;
    }

    void read(std::filesystem::path csv) { fRead = threadPool.submit(&CsvChangeGenerator::run, this, csv); }

    const std::vector<std::string>& getHeader() { return csvData.front(); }

    const std::vector<std::string>& getFirstRow() { return *(csvData.begin() + 1); }

    void setMappingsToDb(const std::vector<MappingNumber> mappings) {
        // TODO: Get actual names instead of ids
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
            logger.pushLog(Log{std::format("MAPPINGS: MAPPED {} WITH {} TO {} OF {}",
                                           mapping.source.outerIdentifier,
                                           mapping.source.innerIdentifier,
                                           mapping.destination.innerIdentifier,
                                           mapping.destination.outerIdentifier)});
        }
        directMappings = std::move(mappingsFromCsv);
        indirectApiMappings = std::move(mappingsFromApi);
        reqExecuteCsv();
    }

    void reqExecuteCsv() { fExecMappings = threadPool.submit(&CsvChangeGenerator::executeCsv, this); }
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
