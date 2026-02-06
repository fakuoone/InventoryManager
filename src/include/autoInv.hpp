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

    std::vector<std::vector<std::string>> data;
    std::future<bool> fRead;
    std::future<void> fExecMappings;

    bool dataRead = false;

    std::vector<MappingStr> committedMappings;

    CsvChangeGenerator(ThreadPool& cThreadPool, ChangeTracker& cChangeTracker, DbService& cDbService, Config& cConfig, Logger& cLogger)
        : threadPool(cThreadPool), changeTracker(cChangeTracker), dbService(cDbService), config(cConfig), logger(cLogger) {}

    virtual ~CsvChangeGenerator() = default;

    bool run(std::filesystem::path csv) {
        data = readData(csv);
        return !data.empty();
    }

    PreciseHeader getHeaderOfMapping(const std::string& csvCol) {
        // TODO: implement logic
        return PreciseHeader("", "");
    }

    void executeCsv() {
        // TODO: Implement logic
        // 0. convert mapping into usable data
        // 1. validate mapping and data?
        // 2. For every data-entry: decide wether insert or update or delete
        // 3. freeze? -> create and push changes
        std::size_t i = 0;
        std::unordered_set<std::size_t> mappedColumns;
        for (std::size_t j = 0; j < data[0].size(); j++) {
            if (std::find_if(committedMappings.begin(), committedMappings.end(), [&](const MappingStr& mapping) {
                    return mapping.source == data[0][j];
                }) != committedMappings.end()) {
                mappedColumns.insert(j);
            }
        }

        for (const auto& row : data) {
            if (i == 0) {
                continue;
            }
            // TODO: a single row can have multiple changes depending on the mapping
            Change::colValMap cells;
            for (std::size_t j = 0; j < row.size(); j++) {
                if (!mappedColumns.contains(j)) {
                    continue;
                }
                PreciseHeader header = getHeaderOfMapping(data[0][j]);
                cells.emplace(header.header, row[j]);
            }

            changeTracker.addChange(Change{cells, changeType::INSERT_ROW, dbService.getTable("TODO")});
        }
    }

  public:
    bool dataValid(bool once) {
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

    const std::vector<std::string>& getHeader() { return data.front(); }

    const std::vector<std::string>& getFirstRow() { return *(data.begin() + 1); }

    void setMappings(const std::vector<MappingStr> mappings) {
        // TODO: Get actual names instead of ids
        committedMappings = mappings;
        for (const MappingStr& mapping : mappings) {
            logger.pushLog(
                Log{std::format("MAPPINGS: MAPPED {} TO {} OF {}", mapping.source, mapping.destination.header, mapping.destination.table)});
        }
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
