#pragma once

#include <future>

#include "change.hpp"
#include "config.hpp"
#include "dbInterface.hpp"
#include "logger.hpp"
#include "threadPool.hpp"

class DbService {
   private:
    DbInterface& dbInterface;
    ThreadPool& pool;
    Logger& logger;

   public:
    DbService(DbInterface& cDbData, ThreadPool& cPool, Logger& cLogger) : dbInterface(cDbData), pool(cPool), logger(cLogger) {}

    std::future<completeDbData> startUp() {
        pool.submit(&DbInterface::acquireTables, &dbInterface);
        pool.submit(&DbInterface::acquireTableContent, &dbInterface);
        auto fCompleteDbData = pool.submit(&DbInterface::acquireAllTablesRows, &dbInterface);
        return fCompleteDbData;
    }

    bool validateChange(const Change<int>& change) {
        // TODO: Implement logic
        if (false) {
            std::string reasons = "";
            logger.pushLog(Log{std::format("Change is invalid, because:\n    {}", reasons)});
        }
        return true;
    }

    void initializeDbInterface(const std::string& configString) { dbInterface.initializeWithConfigString(configString); }

    std::future<std::vector<std::size_t>> requestChangeApplication(std::map<std::size_t, Change<int>> changes) { return pool.submit(&DbInterface::applyChanges, &dbInterface, std::move(changes)); }
};
