#pragma once

#include "change.hpp"
#include "dbService.hpp"
#include "logger.hpp"

#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <unordered_set>

struct protectedChanges {
    std::mutex mtx;
    Change::chHashM flatData;
    Change::chHashV orderedTree;
    Change::ctPKMD pKeyMappedData;
    std::map<std::string, std::size_t> maxPKeys;
};

struct uiChangeInfo {
    Change::ctPKMD idMappedChanges;
    Change::chHashM changes;
};

class ChangeTracker {
   private:
    protectedChanges changes;
    std::atomic<bool> frozen{false};
    std::mutex freezeMtx;
    std::condition_variable freezeCv;

    DbService& dbService;
    Logger& logger;

    std::map<std::string, std::size_t> initialMaxPKeys;

    void mergeCellChanges(Change& existingChange, const Change& newChange);
    void waitIfFrozen();
    bool manageConflictL(const Change& newChange);
    void collectRequiredChanges(Change& change, std::vector<Change>& out);
    void allocateIds(std::vector<Change>& changes);
    bool addChangeInternalL(const Change& change);
    void collectAllDescendants(std::size_t key, std::unordered_set<std::size_t>& collected) const;
    void removeChangeL(std::size_t key);

   public:
    ChangeTracker(DbService& cDbService, Logger& cLogger) : dbService(cDbService), logger(cLogger) {}

    void freeze();
    void unfreeze();
    const Change getChange(const std::size_t key);
    void propagateValidity(Change& change);
    bool addChange(Change change);
    void removeChanges(const std::size_t key);
    void removeChanges(const Change::chHashV& changeHashes);
    uiChangeInfo getSnapShot();
    void setMaxPKeys(std::map<std::string, std::size_t> pk);
    std::size_t getMaxPKey(const std::string table);
    bool isChangeSelected(const std::size_t hash);
    void toggleChangeSelect(const std::size_t hash);
    bool hasChild(const std::size_t hash);
    std::vector<std::size_t> getChildren(const std::size_t key);
};