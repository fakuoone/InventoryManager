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
    bool isChangeApplicationDone() {
        if (fApplyChanges.valid() && fApplyChanges.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) { return true; }
        return false;
    }

    Change::chHashV getSuccessfulChanges() {
        Change::chHashV successfulChanges{};
        if (isChangeApplicationDone()) {
            successfulChanges = fApplyChanges.get();
            changeTracker.removeChanges(successfulChanges);
        }
        return successfulChanges;
    }

    template <typename T>
    void requestChangeApplication(T change_s, sqlAction action) {
        fApplyChanges = dbService.requestChangeApplication(change_s, action);
    }

    ChangeExeService(DbService& cDbService, ChangeTracker& cChangeTracker, Logger& cLogger) : dbService(cDbService), changeTracker(cChangeTracker), logger(cLogger) {}
};