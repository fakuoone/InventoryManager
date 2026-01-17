#pragma once

#include <unordered_set>

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

    void collectChanges(std::size_t key, std::unordered_set<std::size_t>& visited, std::vector<Change>& order) {
        if (visited.contains(key)) { return; }
        visited.insert(key);
        if (changeTracker.hasChild(key)) {
            for (std::size_t child : changeTracker.getChildren(key)) {
                collectChanges(child, visited, order);
            }
        }
        order.push_back(changeTracker.getChange(key));
    }

    std::vector<Change> collectAll(const std::vector<std::size_t>& roots) {
        std::unordered_set<std::size_t> visited;
        std::vector<Change> order;

        for (std::size_t root : roots) {
            collectChanges(root, visited, order);
        }

        return order;
    }

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

    void requestChangeApplication(std::size_t changeKey, sqlAction action) {
        std::vector<std::size_t> keys = {changeKey};
        requestChangeApplication(keys, action);
    }

    void requestChangeApplication(const std::vector<std::size_t> changeKeys, sqlAction action) {
        changeTracker.freeze();
        std::vector<Change> allChanges = collectAll(changeKeys);
        fApplyChanges = dbService.requestChangeApplication(allChanges, action);
        changeTracker.unfreeze();
    }

    ChangeExeService(DbService& cDbService, ChangeTracker& cChangeTracker, Logger& cLogger) : dbService(cDbService), changeTracker(cChangeTracker), logger(cLogger) {}
};