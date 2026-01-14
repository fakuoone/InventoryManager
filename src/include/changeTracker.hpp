#pragma once

#include "change.hpp"
#include "dbService.hpp"
#include "logger.hpp"

#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

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
    Change::chHashV sucChanges;
    std::map<std::string, std::map<std::size_t, bool>> selection;
    bool changesBeingApplied{false};
};

class ChangeTracker {
   private:
    protectedChanges changes;
    DbService& dbService;
    Logger& logger;

    void mergeCellChanges(Change& existingChange, const Change& newChange);

    bool manageConflict(const Change& newChange, std::size_t hash);

   public:
    ChangeTracker(DbService& cDbService, Logger& cLogger) : dbService(cDbService), logger(cLogger) {}

    void addChange(const Change& change);

    void addRelatedChange(std::size_t baseHash, const Change& change);

    void removeChanges(const Change::chHashV& changeHashes);

    Change::chHashM getChanges();

    Change::ctPKMD getRowMappedData();

    void removeChange(const std::size_t hash);

    void setMaxPKeys(std::map<std::string, std::size_t> pk);

    std::size_t getMaxPKey(const std::string table);
};