#pragma once

#include "changeTracker.hpp"
#include "dbService.hpp"
#include "logger.hpp"
#include "threadPool.hpp"

namespace AutoInv {
using mappingIdType = uint32_t;

template <typename S, typename D> struct Mapping {
    S source;
    D destination;
    bool operator==(const Mapping& other) const { return other.source == source && other.destination == destination; }
};
struct PreciseHeader {
    std::string table;
    std::string header;
};

using MappingStr = Mapping<std::string, PreciseHeader>;
using MappingNumber = Mapping<mappingIdType, mappingIdType>;

struct MappingHash {
    size_t operator()(const MappingNumber& m) const noexcept {
        size_t h1 = std::hash<mappingIdType>{}(m.source);
        size_t h2 = std::hash<mappingIdType>{}(m.destination);
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

inline std::vector<std::vector<std::string>> readData(std::filesystem::path csv) {
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

    std::vector<MappingStr> committedMappings;

    CsvChangeGenerator(ThreadPool& cThreadPool, ChangeTracker& cChangeTracker, DbService& cDbService, Config& cConfig, Logger& cLogger)
        : threadPool(cThreadPool), changeTracker(cChangeTracker), dbService(cDbService), config(cConfig), logger(cLogger) {}

    virtual ~CsvChangeGenerator() = default;

    bool run(std::filesystem::path csv) {
        csvData = readData(csv);
        return !csvData.empty();
    }

    struct ChangeConvertedMapping {
        std::vector<std::size_t> columnIndexes;
        std::unordered_map<std::size_t, std::vector<PreciseHeader>> preciseHeaders;
        std::unordered_map<std::string, Change::colValMap> cells;
    };

    ChangeConvertedMapping convertMapping() {
        // gets the indexes of mapped csv-columns and an indexmapped table
        ChangeConvertedMapping convertedMapping;
        std::unordered_set<std::string> foundTables;
        const std::vector<std::string>& csvHeader = csvData[0];

        for (const MappingStr& mapping : committedMappings) {
            const auto it = std::find_if(csvHeader.begin(), csvHeader.end(), [&](const std::string& col) { return mapping.source == col; });
            if (it == csvHeader.end()) {
                logger.pushLog(
                    Log{std::format("ERROR: Converting mappings failed because {} does not match a csv column.", mapping.source)});
                return convertedMapping;
            }

            std::size_t j = std::distance(csvHeader.begin(), it);
            if (!foundTables.contains(mapping.destination.table)) {
                foundTables.insert(mapping.destination.table);
            }
            if (!convertedMapping.preciseHeaders.contains(j)) {
                convertedMapping.cells.emplace(mapping.destination.table, Change::colValMap{});
                convertedMapping.preciseHeaders.emplace(j, std::vector<PreciseHeader>{});
            }

            convertedMapping.preciseHeaders[j].push_back(mapping.destination);
            convertedMapping.columnIndexes.push_back(j);
        }
        return convertedMapping;
    }

    void applyMappingToRow(const std::vector<std::string>& row, ChangeConvertedMapping& mapped) {
        for (std::size_t j = 0; j < mapped.columnIndexes.size(); j++) {
            const std::size_t mappedColumnIndex = mapped.columnIndexes[j];
            for (const PreciseHeader& preciseHeader : mapped.preciseHeaders[mappedColumnIndex]) {
                mapped.cells[preciseHeader.table].emplace(preciseHeader.header, row[mappedColumnIndex]);
            }
        }
    }

    void fillInAdditional(ChangeConvertedMapping& mapped) {
        // TODO: get all headers from dbData
        for (const auto& [_, mappedHeaders] : mapped.preciseHeaders) {
            for (const PreciseHeader& preciseHeader : mappedHeaders) {
                const tHeaderInfo& headerInfo = dbService.getTableHeaderInfo(preciseHeader.table, preciseHeader.header);
                if (headerInfo.nullable) {
                    continue;
                }
                mapped.cells[preciseHeader.table].emplace(headerInfo.name, "TODO");
            }
        }
    }

    void addChangesFromMapping(ChangeConvertedMapping& mapped) {
        for (const auto& [_, mappedHeaders] : mapped.preciseHeaders) {
            std::unordered_set<std::string> visitedTables;
            for (const PreciseHeader& preciseHeader : mappedHeaders) {
                if (visitedTables.contains(preciseHeader.table)) {
                    continue;
                }
                changeTracker.addChange(
                    Change{mapped.cells[preciseHeader.table], changeType::INSERT_ROW, dbService.getTable(preciseHeader.table)});
                mapped.cells[preciseHeader.table] = Change::colValMap{};
            }
        }
    }

    void executeCsv() {
        // TODO: Implement logic
        // 0. convert mapping into usable data
        // 1. validate mapping and data?
        // 2. For every data-entry: decide wether insert or update or delete
        // 3. freeze? -> create and push changes
        std::size_t i = 0;
        ChangeConvertedMapping mapped = convertMapping();

        std::vector<Change> changes;
        for (const auto& row : csvData) {
            if (i++ == 0) {
                continue;
            }
            applyMappingToRow(row, mapped);
            fillInAdditional(mapped);
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
                return true;
            }
        }
        return false;
    }

    void read(std::filesystem::path csv) { fRead = threadPool.submit(&CsvChangeGenerator::run, this, csv); }

    const std::vector<std::string>& getHeader() { return csvData.front(); }

    const std::vector<std::string>& getFirstRow() { return *(csvData.begin() + 1); }

    void setMappings(const std::vector<MappingStr> mappings) {
        // TODO: Get actual names instead of ids
        committedMappings = mappings;
        for (const MappingStr& mapping : mappings) {
            logger.pushLog(
                Log{std::format("MAPPINGS: MAPPED {} TO {} OF {}", mapping.source, mapping.destination.header, mapping.destination.table)});
        }
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
