#pragma once

#include "changeTracker.hpp"
#include "dbService.hpp"
#include "logger.hpp"
#include "threadPool.hpp"

namespace AutoInv {
using mappingIdType = uint32_t;

enum class SourceType { NONE, CSV, API };

template <typename S, typename D> struct Mapping {
    S source;
    D destination;
    bool operator==(const Mapping& other) const { return other.source == source && other.destination == destination; }
};

struct PreciseHeader {
    std::string table;
    std::string header;
};

struct ApiData {};

using MappingToDb = Mapping<std::string, PreciseHeader>;
using MappingCsvApi = Mapping<std::string, uint32_t>;
using MappingNumberInternal = Mapping<mappingIdType, mappingIdType>;

using MappingVariant = std::variant<MappingToDb, MappingCsvApi>;

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
        size_t h1 = std::hash<mappingIdType>{}(m.uniqueData.source);
        size_t h2 = std::hash<mappingIdType>{}(m.uniqueData.destination);
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
    Config& config;
    Logger& logger;

    std::shared_ptr<const completeDbData> dbData;

    std::vector<std::vector<std::string>> csvData;
    std::future<bool> fRead;
    std::future<void> fExecMappings;

    bool dataRead = false;

    std::vector<MappingToDb> directMappings;
    std::vector<MappingToDb> indirectApiMappings;
    std::size_t missingParam = 0;

    CsvChangeGenerator(ThreadPool& cThreadPool, ChangeTracker& cChangeTracker, DbService& cDbService, Config& cConfig, Logger& cLogger)
        : threadPool(cThreadPool), changeTracker(cChangeTracker), dbService(cDbService), config(cConfig), logger(cLogger) {}

    virtual ~CsvChangeGenerator() = default;

    bool run(std::filesystem::path csv) {
        csvData = readData(csv, logger);
        return !csvData.empty();
    }

    struct TableCells {
        std::string table;
        Change::colValMap cells;
    };

    struct ChangeConvertedMapping {
        std::vector<std::size_t> columnIndexes;
        std::unordered_map<std::size_t, std::vector<PreciseHeader>> preciseHeaders;
        std::map<std::string, TableCells> cells;
        std::vector<TableCells*> orderedCells;
    };

    ChangeConvertedMapping convertMapping() {
        // gets the indexes of mapped csv-columns and an indexmapped table
        ChangeConvertedMapping convertedMapping;
        std::unordered_set<std::string> foundTables;
        const std::vector<std::string>& csvHeader = csvData[0];

        for (const MappingToDb& mapping : directMappings) {
            // check legality of mapping
            const auto it = std::find_if(csvHeader.begin(), csvHeader.end(), [&](const std::string& col) { return mapping.source == col; });
            if (it == csvHeader.end()) {
                logger.pushLog(
                    Log{std::format("ERROR: Converting mappings failed because {} does not match a csv column.", mapping.source)});
                return convertedMapping;
            }

            // store found tables to construct changes later
            std::size_t j = std::distance(csvHeader.begin(), it);
            if (!foundTables.contains(mapping.destination.table)) {
                foundTables.insert(mapping.destination.table);
            }

            // csv column references new db header
            if (!convertedMapping.preciseHeaders.contains(j)) {
                convertedMapping.cells.emplace(mapping.destination.table, TableCells{mapping.destination.table, Change::colValMap{}});
                convertedMapping.preciseHeaders.emplace(j, std::vector<PreciseHeader>{});
            }

            // add mapping-destination to the csv header (1 to n)
            convertedMapping.preciseHeaders[j].push_back(mapping.destination);
            convertedMapping.columnIndexes.push_back(j);
        }
        return convertedMapping;
    }

    void fillInApiData(const std::vector<std::vector<std::string>>& rows) {
        // TODO:takes a json of api data and precomputes cell data based on a hmi mapping selection api -> precisehader
        std::size_t i = 0;
        for (const auto& row : csvData) {
            if (i++ == 0) {
                continue;
            }
            // indirectApiMappings;
        }
    }

    void applyBasicMappingToRow(const std::vector<std::string>& row, ChangeConvertedMapping& mapped) {
        for (std::size_t j = 0; j < mapped.columnIndexes.size(); j++) {
            const std::size_t mappedColumnIndex = mapped.columnIndexes[j];
            for (const PreciseHeader& preciseHeader : mapped.preciseHeaders[mappedColumnIndex]) {
                mapped.cells[preciseHeader.table].cells.emplace(preciseHeader.header, row[mappedColumnIndex]);
            }
        }
    }

    void applyApiMappingToRow(const std::vector<std::string>& row, ChangeConvertedMapping& mapped) {
        // TODO combine api answer with csv row
    }

    void fillInAdditional(ChangeConvertedMapping& mapped) {
        // Fill in missing
        for (const auto& [_, preciseHeaders] : mapped.preciseHeaders) {
            std::unordered_set<std::string> visitedTables;
            for (const PreciseHeader& preciseHeader : preciseHeaders) {
                if (visitedTables.contains(preciseHeader.table)) {
                    continue;
                }
                visitedTables.insert(preciseHeader.table);
                for (const auto& header : dbData->headers.at(preciseHeader.table).data) {
                    if (mapped.cells[preciseHeader.table].cells.contains(header.name) || header.nullable ||
                        header.type == headerType::PRIMARY_KEY) {
                        continue;
                    }
                    mapped.cells[preciseHeader.table].cells.emplace(header.name, std::format("TODO{}", missingParam++));
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
        // fillInApiData(mapped);
        for (const auto& row : csvData) {
            if (i++ == 0) {
                continue;
            }
            applyBasicMappingToRow(row, mapped);
            applyApiMappingToRow(row, mapped);
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

    void setMappings(const std::vector<MappingNumber> mappings) {
        // TODO: Get actual names instead of ids
        std::vector<MappingToDb> mappingsFromCsv;
        std::vector<MappingToDb> mappingsFromApi;
        mappingsFromCsv.reserve(mappings.size());
        mappingsFromApi.reserve(mappings.size());
        for (const MappingNumber& mapping : mappings) {
            if (auto* mappingToDb = std::get_if<MappingToDb>(&mapping.usableData)) {
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
            }
        }

        for (const MappingToDb& mapping : mappingsFromCsv) {
            logger.pushLog(
                Log{std::format("MAPPINGS: MAPPED {} TO {} OF {}", mapping.source, mapping.destination.header, mapping.destination.table)});
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
    ChangeGeneratorFromBom(ThreadPool& cPool, ChangeTracker& cChangeTracker, DbService& cDbService, Config& cConfig, Logger& cLogger)
        : CsvChangeGenerator(cPool, cChangeTracker, cDbService, cConfig, cLogger) {}
};

class ChangeGeneratorFromOrder : public CsvChangeGenerator {
  private:
  public:
    ChangeGeneratorFromOrder(ThreadPool& cPool, ChangeTracker& cChangeTracker, DbService& cDbService, Config& cConfig, Logger& cLogger)
        : CsvChangeGenerator(cPool, cChangeTracker, cDbService, cConfig, cLogger) {}
};
}; // namespace AutoInv
