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

    CsvChangeGenerator(
        ThreadPool& cThreadPool, ChangeTracker& cChangeTracker, DbService& cDbService, PartApi& cPartApi, Config& cConfig, Logger& cLogger)
        : threadPool(cThreadPool), changeTracker(cChangeTracker), dbService(cDbService), partApi(cPartApi), config(cConfig),
          logger(cLogger) {}

    virtual ~CsvChangeGenerator() = default;

    bool run(std::filesystem::path csv) {
        csvData = readData(csv, logger);
        return !csvData.empty();
    }

    struct TableCells {
        std::string table;
        Change::colValMap cells;
    };

    struct TargetData {
        std::vector<PreciseMapLocation> dbHeaders;
        std::optional<std::string> apiResultPath;
    };

    struct ChangeConvertedMapping {
        std::vector<std::size_t> columnIndexes;
        std::unordered_map<std::size_t, TargetData> preciseHeaders;
        std::map<std::string, TableCells> cells;
        std::vector<TableCells*> orderedCells;
    };

    void convertMappings(ChangeConvertedMapping& convertedMapping,
                         std::unordered_set<std::string>& foundTables,
                         const std::vector<MappingCsvToDb>& mappings) {
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
            if (!convertedMapping.preciseHeaders.contains(j)) {
                convertedMapping.cells.emplace(mapping.destination.outerIdentifier,
                                               TableCells{mapping.destination.outerIdentifier, Change::colValMap{}});
                convertedMapping.preciseHeaders.emplace(j, TargetData{std::vector<PreciseMapLocation>{}, mapping.source.innerIdentifier});
            }

            // add mapping-destination to the csv header (1 to n)
            convertedMapping.preciseHeaders[j].dbHeaders.push_back(mapping.destination);
            convertedMapping.columnIndexes.push_back(j);
        }
    }

    ChangeConvertedMapping convertMapping() {
        // gets the indexes of mapped csv-columns and an indexmapped table
        ChangeConvertedMapping convertedMapping;
        std::unordered_set<std::string> foundTables;

        convertMappings(convertedMapping, foundTables, directMappings);
        convertMappings(convertedMapping, foundTables, indirectApiMappings);

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
        std::string_view path = selectedField;
        std::size_t start = 0;
        std::size_t end = 0;
        nlohmann::json const* nextLevel = &j;
        while ((end = path.find('/', start)) != std::string_view::npos) {
            // TODO: Handle array
            std::string_view pathEntry = path.substr(start, end - start);
            if (nextLevel->contains(pathEntry)) {
                nextLevel = &(*nextLevel)[pathEntry];
            } else {
                nextLevel = nullptr;
                break;
            }
            start = end + 1;
        }

        if (start < path.size()) {
            std::string_view token = path.substr(start);
        }

        assert(nextLevel); // TODO
        return nextLevel->get<std::string>();
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

    void fetchApiData(ChangeConvertedMapping& mapped) {
        // TODO: Enter api data into mapped
        std::size_t totalRows = csvData.size();
        if (totalRows <= 1) {
            return;
        }

        std::size_t threadCount = threadPool.getAvailableThreadCount();
        std::size_t dataRows = totalRows - 1; // skip header

        threadCount = std::min(threadCount, dataRows / 10);

        std::size_t baseChunkSize = dataRows / threadCount;
        std::size_t remainder = dataRows % threadCount;
        std::size_t chunkStart = 0;

        std::vector<std::unordered_map<std::string, Change::colValMap>> results;
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
    }

    void applyMappingToRow(const std::vector<std::string>& row,
                           ChangeConvertedMapping& mapped,
                           const std::unordered_map<std::string, Change::colValMap>& apiData) {
        for (std::size_t j = 0; j < mapped.columnIndexes.size(); j++) {
            const std::size_t mappedColumnIndex = mapped.columnIndexes[j];
            for (const PreciseMapLocation& preciseHeader : mapped.preciseHeaders.at(mappedColumnIndex).dbHeaders) {
                mapped.cells.at(preciseHeader.outerIdentifier).cells.emplace(preciseHeader.innerIdentifier, row.at(mappedColumnIndex));
            }
            for (const auto& [table, cells] : apiData) {
                TableCells& tableCells = mapped.cells.at(table);
                for (const auto& [col, val] : cells) {
                    if (tableCells.cells.contains(col)) {
                        logger.pushLog(
                            Log{std::format("ERROR: Column {} already got mapped from raw csv data. Api cannot override.", col)});
                        return;
                    }
                    tableCells.cells.emplace(col, val);
                }
            }
            // TODO: involve api data
        }
    }

    void fillInAdditional(ChangeConvertedMapping& mapped) {
        // Fill in missing
        for (const auto& [_, preciseHeaders] : mapped.preciseHeaders) {
            std::unordered_set<std::string> visitedTables;
            for (const PreciseMapLocation& preciseHeader : preciseHeaders.dbHeaders) {
                if (visitedTables.contains(preciseHeader.outerIdentifier)) {
                    continue;
                }
                visitedTables.insert(preciseHeader.outerIdentifier);
                for (const auto& header : dbData->headers.at(preciseHeader.outerIdentifier).data) {
                    if (mapped.cells[preciseHeader.outerIdentifier].cells.contains(header.name) || header.nullable ||
                        header.type == headerType::PRIMARY_KEY) {
                        continue;
                    }
                    mapped.cells[preciseHeader.outerIdentifier].cells.emplace(header.name, std::format("TODO{}", missingParam++));
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
            if (!changeTracker.addChange(Change{cells->cells, changeType::INSERT_ROW, dbService.getTable(cells->table)})) {
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
        fetchApiData(mapped);
        for (const auto& row : csvData) {
            if (i++ == 0) {
                continue;
            }
            fillInAdditional(mapped);
            sortMappedCells(mapped);
            addChangesFromMapping(mapped);
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
        : CsvChangeGenerator(cPool, cChangeTracker, cDbService, cPartApi, cConfig, cLogger) {}
};

class ChangeGeneratorFromOrder : public CsvChangeGenerator {
  private:
  public:
    ChangeGeneratorFromOrder(
        ThreadPool& cPool, ChangeTracker& cChangeTracker, DbService& cDbService, PartApi& cPartApi, Config& cConfig, Logger& cLogger)
        : CsvChangeGenerator(cPool, cChangeTracker, cDbService, cPartApi, cConfig, cLogger) {}
};
}; // namespace AutoInv
