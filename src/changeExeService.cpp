#include "changeExeService.hpp"

void ChangeExeService::collectChanges(std::size_t key, std::unordered_set<std::size_t>& visited, std::vector<Change>& order) {
    if (visited.contains(key)) { return; }
    visited.insert(key);
    if (changeTracker_.hasChild(key)) {
        for (std::size_t child : changeTracker_.getChildren(key)) {
            collectChanges(child, visited, order);
        }
    }
    auto change = changeTracker_.getChange(key);
    if (change) { order.push_back(*change); }
}

std::vector<Change> ChangeExeService::collectDescendants(const std::vector<std::size_t>& roots) {
    std::unordered_set<std::size_t> visited;
    std::vector<Change> order;

    for (std::size_t root : roots) {
        collectChanges(root, visited, order);
    }

    return order;
}

ChangeExeService::ChangeExeService(DbService& cDbService, ChangeTracker& cChangeTracker, Logger& cLogger)
    : dbService_(cDbService), changeTracker_(cChangeTracker), logger_(cLogger) {}

bool ChangeExeService::isChangeApplicationDone() {
    if (fApplyChanges_.valid() && fApplyChanges_.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) { return true; }
    return false;
}

Change::chHashV ChangeExeService::getSuccessfulChanges() {
    Change::chHashV successfulChanges{};
    if (isChangeApplicationDone()) {
        successfulChanges = fApplyChanges_.get();
        changeTracker_.removeChanges(successfulChanges);
    }
    return successfulChanges;
}

void ChangeExeService::requestChangeApplication(std::size_t changeKey, SqlAction action) {
    // reqests executiion for 1 changeKey (and its descendants)
    std::vector<std::size_t> keys = {changeKey};
    requestChangeApplication(keys, action);
}

void ChangeExeService::requestChangeApplication(const std::vector<std::size_t> changeKeys, SqlAction action) {
    // requests execution for vector of changeKey
    changeTracker_.freeze();
    std::vector<Change> allChanges = collectDescendants(changeKeys);
    fApplyChanges_ = dbService_.requestChangeApplication(allChanges, action);
    changeTracker_.unfreeze();
}

void ChangeExeService::requestChangeApplication(SqlAction action) {
    // request execution for all changes
    changeTracker_.freeze();
    std::vector<Change> allChanges = collectDescendants(changeTracker_.getCalcRoots());
    fApplyChanges_ = dbService_.requestChangeApplication(allChanges, action);
    changeTracker_.unfreeze();
}
