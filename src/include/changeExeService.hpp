#pragma once

#include "change.hpp"
#include "logger.hpp"
#include "changeTracker.hpp"
#include "dbService.hpp"
#include "dbInterface.hpp"

class ChangeExeService {
   private:
    DbService& dbService;
    ChangeTracker& changeTracker;
    Logger& logger;

    std::future<Change::chHashV> fApplyChanges;

   public:
    bool waitForChangeApplication() {
        if (fApplyChanges.valid() && fApplyChanges.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) { return true; }
        return false;
    }

    std::future<Change::chHashV> requestChangeApplication(Change::chHashM changes, sqlAction action) { return dbService.requestChangeApplication(changes, action); }

    ChangeExeService(DbService& cDbService, ChangeTracker& cChangeTracker, Logger& cLogger) : dbService(cDbService), changeTracker(cChangeTracker), logger(cLogger) {}
};