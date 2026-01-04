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
    std::map<std::size_t, Change<int>> data;
};

class ChangeTracker {
   private:
    protectedChanges changes;
    DbService& dbService;
    Logger& logger;

    void mergeCellChanges(Change<int>& existingChange, const Change<int>& newChange);

    bool manageConflict(const Change<int>& newChange, std::size_t hash);

   public:
    ChangeTracker(DbService& cDbService, Logger& cLogger) : dbService(cDbService), logger(cLogger) {}

    void addChange(const Change<int>& change);

    void removeChange(Change<int>& change);
};