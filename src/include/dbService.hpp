#pragma once

#include <future>
#include <expected>

#include "change.hpp"
#include "config.hpp"
#include "dbInterface.hpp"
#include "logger.hpp"
#include "threadPool.hpp"

class DbService {
   private:
    DbInterface& dbInterface;
    ThreadPool& pool;
    Config& config;
    Logger& logger;

    std::future<completeDbData> fCompleteDbData;
    std::shared_ptr<const completeDbData> dbData;
    std::shared_ptr<completeDbData> pendingData;
    std::future<std::map<std::string, std::size_t>> fMaxPKeys;

    std::atomic<bool> dataAvailable{false};

    std::map<std::string, std::size_t> calcMaxPKeys(completeDbData data) {
        std::map<std::string, std::size_t> maxPKeys;
        for (const auto& table : data.tables) {
            // Get max index of pkeys
            const tStringVector& keyVector = data.tableRows.at(table).at(data.headers.at(table).pkey);
            auto it2 = std::max_element(keyVector.begin(), keyVector.end(), [](const std::string& key1, const std::string& key2) { return std::stoll(key1) < std::stoll(key2); });
            std::size_t maxKey = 0;
            if (it2 != keyVector.end()) { maxKey = static_cast<std::size_t>(std::stoll(*it2)); }
            maxPKeys[table] = maxKey;
        }
        return maxPKeys;
    }

    bool isDataReady() {
        if (!pendingData && fCompleteDbData.valid() && fCompleteDbData.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
            auto data = fCompleteDbData.get();
            if (!validateCompleteDbData(data)) { return false; }
            pendingData = std::make_shared<completeDbData>(std::move(data));
            fMaxPKeys = pool.submit(&DbService::calcMaxPKeys, this, std::cref(*pendingData));
        }

        if (pendingData && fMaxPKeys.valid() && fMaxPKeys.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
            pendingData->maxPKeys = fMaxPKeys.get();
            dbData = std::move(pendingData);
            dataAvailable = true;
        }

        return dataAvailable;
    }

   public:
    DbService(DbInterface& cDbData, ThreadPool& cPool, Config& cConfig, Logger& cLogger) : dbInterface(cDbData), pool(cPool), config(cConfig), logger(cLogger) {}

    void startUp() {
        pool.submit(&DbInterface::acquireTables, &dbInterface);
        pool.submit(&DbInterface::acquireTableContent, &dbInterface);
        fCompleteDbData = pool.submit(&DbInterface::acquireAllTablesRows, &dbInterface);
    }

    std::expected<std::shared_ptr<const completeDbData>, bool> getCompleteData() {
        if (isDataReady()) {
            return dbData;
        } else {
            return std::unexpected(false);
        }
    }

    bool validateCompleteDbData(const completeDbData& data) {
        // tablecount matches everywhere
        std::size_t tableCount = data.tables.size();
        if (tableCount != data.headers.size() || tableCount != data.tableRows.size()) {
            logger.pushLog(Log{"ERROR: Table data is mismatching in size."});
            return false;
        }
        // all tables have headers
        for (const auto& table : data.tables) {
            if (!data.headers.contains(table)) {
                logger.pushLog(Log{std::format("ERROR: Table {} has no header information.", table)});
                return false;
            }
            // columns have the same values as rows have keys
            bool pKeyFound{false};
            for (const auto& header : data.headers.at(table).data) {
                if (header.type == headerType::PRIMARY_KEY) { pKeyFound = true; };
                if (!data.tableRows.at(table).contains(header.name)) {
                    logger.pushLog(Log{std::format("ERROR: Table {} has header {} which has no data.", table, header.name)});
                    return false;
                }
            }
            if (!pKeyFound) {
                logger.pushLog(Log{std::format("ERROR: Table {} has no primary key {}.", table, config.getPrimaryKey())});
                return false;
            }
        }
        return true;
    }

    bool validateChange(Change& change, bool fromGeneration) {
        const tStringVector& tables = dbData->tables;
        if (std::find(tables.begin(), tables.end(), change.getTable()) == tables.end()) { return false; }
        bool setValidity = true;
        bool allowInvalidChange = change.hasParent() && fromGeneration;

        switch (change.getType()) {
            case changeType::DELETE_ROW:
                break;
            case changeType::INSERT_ROW:
            case changeType::UPDATE_CELLS: {
                const Change::colValMap& cells = change.getCells();
                const tHeadersInfo& headers = dbData->headers.at(change.getTable());
                // check non-nullable column count
                std::size_t reqColumnCount = std::count_if(headers.data.begin(), headers.data.end(), [](const tHeaderInfo& h) { return !h.nullable; }) - 1;
                if (reqColumnCount > cells.size() && change.getType() == changeType::INSERT_ROW) { setValidity = false; }
                if (cells.size() > (headers.data.size() - 1) && !allowInvalidChange) {
                    logger.pushLog(Log{std::format("ERROR: Change is invalid because not enough columns were supplied to satisfy the non-null table columns.")});
                    change.setLocalValidity(false);
                    return false;
                }

                for (const auto& header : headers.data) {
                    if (header.type == headerType::PRIMARY_KEY) {
                        if (cells.contains(header.name)) {
                            logger.pushLog(Log{std::format("ERROR: Change is not allowed to provide the primary key.")});
                            change.setLocalValidity(false);
                            return false;
                        }
                        continue;
                    } else if (!header.nullable) {
                        // non-nullable column is null
                        if (!cells.contains(header.name)) {
                            if (change.getType() == changeType::INSERT_ROW) {
                                if (!allowInvalidChange) {
                                    logger.pushLog(Log{std::format("ERROR: Header {} is not nullable and no value was provided.", header.name)});
                                    change.setLocalValidity(false);
                                    return false;
                                }
                                setValidity = false;
                            }
                        } else {
                            if (cells.at(header.name).empty()) {
                                if (!allowInvalidChange) {
                                    logger.pushLog(Log{std::format("ERROR: Header {} is not nullable but empty value was provided.", header.name)});
                                    return false;
                                }
                                setValidity = false;
                            }
                        }
                    }
                }
            } break;
            default:
                break;
        }
        change.setLocalValidity(setValidity);
        return true;
    }

    std::vector<Change> getRequiredChanges(const Change& change, const std::map<std::string, std::size_t>& ids) {
        const std::string& table = change.getTable();
        std::vector<Change> changes;
        const tHeadersInfo& headers = dbData->headers.at(table);
        const Change::colValMap& cells = change.getCells();

        for (const auto& [col, val] : cells) {
            // find foreign key thats required
            auto it1 = std::ranges::find_if(headers.data, [&](const tHeaderInfo& h) { return h.name == col && h.type == headerType::FOREIGN_KEY; });
            if (it1 != headers.data.end()) {  // && it1->referencedTable != table) {
                if (!checkReferencedPKeyValue(it1->referencedTable, it1->nullable, val)) {
                    Change::colValMap requiredCells;
                    requiredCells.emplace(dbData->headers.at(it1->referencedTable).uKeyName, val);
                    Change reqChange{requiredCells, changeType::INSERT_ROW, getTable(it1->referencedTable)};
                    reqChange.setParent(change.getKey());
                    changes.emplace_back(reqChange);
                }
            }
        }
        return changes;
    }

    bool checkReferencedPKeyValue(const std::string& ref, bool nullable, const std::string& val) {
        // does pkey-value already exist
        if (val.empty() && nullable) { return true; }
        std::string pKey = dbData->headers.at(ref).pkey;
        auto it1 = std::ranges::find_if(dbData->tableRows.at(ref).at(pKey), [&](const std::string& h) { return h == val; });
        if (it1 != dbData->tableRows.at(ref).at(pKey).end()) { return true; }
        return false;
    }

    void initializeDbInterface(const std::string& configString) { dbInterface.initializeWithConfigString(configString); }

    std::future<Change::chHashV> requestChangeApplication(std::vector<Change> changes, sqlAction action) {
        return pool.submit([this](auto change, sqlAction act) { return dbInterface.applyChanges(std::move(change), act); }, std::move(changes), action);
        //      return pool.submit(&DbInterface::applyChanges, &dbInterface, std::move(change_s), action);
    }

    imTable getTable(const std::string& tableName) {
        auto it = std::find(dbData->tables.begin(), dbData->tables.end(), tableName);
        imTable tableData{tableName, 0};
        if (it != dbData->tables.end()) { tableData.id = static_cast<uint16_t>(std::distance(dbData->tables.begin(), it)); }
        return tableData;
    }

    std::string getTableUKey(const std::string& table) { return dbData->headers.at(table).uKeyName; }

    tHeaderInfo getTableHeaderInfo(const std::string& table, const std::string& header) {
        const tHeaderVector& headers = dbData->headers.at(table).data;
        auto it = std::find_if(headers.begin(), headers.end(), [&](const tHeaderInfo& h) { return h.name == header; });
        return *it;
    }
};
