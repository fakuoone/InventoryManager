#pragma once

#include <future>
#include <expected>

#include "change.hpp"
#include "config.hpp"
#include "dbInterface.hpp"
#include "logger.hpp"
#include "threadPool.hpp"

// TODO: fix this
using cccType = int;
class DbService {
   private:
    DbInterface& dbInterface;
    ThreadPool& pool;
    Logger& logger;

    std::future<completeDbData> fCompleteDbData;
    std::shared_ptr<const completeDbData> dbData;
    bool dataAvailable{false};

    bool isDataReady() {
        if (fCompleteDbData.valid() && fCompleteDbData.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
            dbData = std::make_shared<completeDbData>(std::move(fCompleteDbData.get()));
            if (validateCompleteDbData()) { dataAvailable = true; }
        }
        return dataAvailable;
    }

   public:
    DbService(DbInterface& cDbData, ThreadPool& cPool, Logger& cLogger) : dbInterface(cDbData), pool(cPool), logger(cLogger) {}

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

    bool validateCompleteDbData() {
        // tablecount matches everywhere
        std::size_t tableCount = dbData->tables.size();
        if (tableCount != dbData->headers.size() || tableCount != dbData->tableRows.size()) {
            logger.pushLog(Log{"ERROR: Table data is mismatching in size."});
            return false;
        }
        // all tables have headers
        for (const auto& table : dbData->tables) {
            if (!dbData->headers.contains(table)) {
                logger.pushLog(Log{std::format("ERROR: Table {} has no header information.", table)});
                return false;
            }
            // columns have the same values as rows have keys
            for (const auto& header : dbData->headers.at(table)) {
                if (!dbData->tableRows.at(table).contains(header)) {
                    logger.pushLog(Log{std::format("ERROR: Table {} has header {} which has no data.", table, header)});
                    return false;
                }
            }
        }
        return true;
    }

    bool validateChange(const Change<int>& change) {
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

    std::future<std::vector<std::size_t>> requestChangeApplication(std::map<std::size_t, Change<cccType>> changes, sqlAction action) { return pool.submit(&DbInterface::applyChanges, &dbInterface, std::move(changes), action); }
};
