#pragma once

#include "change.hpp"
#include "dbService.hpp"
#include "logger.hpp"

#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

using cccType = int;
struct protectedChanges {
    std::mutex mtx;
    std::map<std::size_t, Change<cccType>> flatData;
    std::vector<std::size_t> orderedTree;
    std::map<std::string, std::map<cccType, std::size_t>> rowMappedData;
};

class ChangeTracker {
   private:
    protectedChanges changes;
    DbService& dbService;
    Logger& logger;

    void mergeCellChanges(Change<cccType>& existingChange, const Change<cccType>& newChange);

    bool manageConflict(const Change<cccType>& newChange, std::size_t hash);

   public:
    ChangeTracker(DbService& cDbService, Logger& cLogger) : dbService(cDbService), logger(cLogger) {}

    void addChange(const Change<cccType>& change);

    void addRelatedChange(std::size_t baseHash, const Change<cccType>& change);

    void removeChanges(const std::vector<std::size_t>& changeHashes);

    std::map<std::size_t, Change<cccType>> getChanges();

    std::map<std::string, std::map<cccType, std::size_t>> getRowMappedData();

    void removeChange(const std::size_t hash);
};