#pragma once

#include "change.hpp"
#include "changeTracker.hpp"
#include "dbInterface.hpp"
#include "dbService.hpp"
#include "logger.hpp"

class ChangeExeService {
  private:
    DbService& dbService;
    ChangeTracker& changeTracker;
    Logger& logger;

    std::future<Change::chHashV> fApplyChanges;

    void collectChanges(std::size_t key, std::unordered_set<std::size_t>& visited, std::vector<Change>& order) {
        if (visited.contains(key)) {
            return;
        }
        visited.insert(key);
        if (changeTracker.hasChild(key)) {
            for (std::size_t child : changeTracker.getChildren(key)) {
                collectChanges(child, visited, order);
            }
        }
        order.push_back(changeTracker.getChange(key));
    }

    std::vector<Change> collectDescendants(const std::vector<std::size_t>& roots) {
        std::unordered_set<std::size_t> visited;
        std::vector<Change> order;

        for (std::size_t root : roots) {
            collectChanges(root, visited, order);
        }

        return order;
    }

  public:
    bool isChangeApplicationDone() {
        if (fApplyChanges.valid() && fApplyChanges.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
            return true;
        }
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
        // reqests executiion for 1 changeKey (and its descendants)
        std::vector<std::size_t> keys = {changeKey};
        requestChangeApplication(keys, action);
    }

    void requestChangeApplication(const std::vector<std::size_t> changeKeys, sqlAction action) {
        // requests execution for vector of changeKey
        changeTracker.freeze();
        std::vector<Change> allChanges = collectDescendants(changeKeys);
        fApplyChanges = dbService.requestChangeApplication(allChanges, action);
        changeTracker.unfreeze();
    }

    void requestChangeApplication(sqlAction action) {
        // request execution for all changes
        changeTracker.freeze();
        std::vector<Change> allChanges = collectDescendants(changeTracker.getCalcRoots());
        fApplyChanges = dbService.requestChangeApplication(allChanges, action);
        changeTracker.unfreeze();
    }

    ChangeExeService(DbService& cDbService, ChangeTracker& cChangeTracker, Logger& cLogger)
        : dbService(cDbService), changeTracker(cChangeTracker), logger(cLogger) {}
};