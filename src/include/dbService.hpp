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
            const tStringVector& keyVector = data.tableRows.at(table).at(config.getPrimaryKey());
            auto it = std::max_element(keyVector.begin(), keyVector.end(), [](const std::string& key1, const std::string& key2) { return std::stoll(key1) < std::stoll(key2); });
            std::size_t maxKey = 0;
            if (it != keyVector.end()) { maxKey = static_cast<std::size_t>(std::stoll(*it)); }
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
            for (const auto& header : data.headers.at(table)) {
                if (header == config.getPrimaryKey()) { pKeyFound = true; };
                if (!data.tableRows.at(table).contains(header)) {
                    logger.pushLog(Log{std::format("ERROR: Table {} has header {} which has no data.", table, header)});
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

    bool validateChange(const Change& change) {
        // TODO: Implement logic
        // 1.
        const tStringVector& tables = dbData->tables;
        if (std::find(tables.begin(), tables.end(), change.getTable()) == tables.end()) { return false; }
        switch (change.getType()) {
            case changeType::DELETE_ROW:
                /* code */
                break;
            case changeType::INSERT_ROW:
                break;
            case changeType::UPDATE_CELLS:
                break;
            default:
                break;
        }
        if (false) {
            std::string reasons = "";
            logger.pushLog(Log{std::format("Change is invalid, because:\n    {}", reasons)});
        }
        return true;
    }

    void initializeDbInterface(const std::string& configString) { dbInterface.initializeWithConfigString(configString); }

    template <typename T>
    std::future<Change::chHashV> requestChangeApplication(T change_s, sqlAction action) {
        return pool.submit([this](auto change, sqlAction act) { return dbInterface.applyChanges(std::move(change), act); }, std::move(change_s), action);
        //      return pool.submit(&DbInterface::applyChanges, &dbInterface, std::move(change_s), action);
    }
};
