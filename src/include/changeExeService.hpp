#pragma once

#include "change.hpp"
#include "changeTracker.hpp"
#include "dbInterface.hpp"
#include "dbService.hpp"
#include "logger.hpp"

class ChangeExeService {
  private:
    DbService& dbService_;
    ChangeTracker& changeTracker_;
    Logger& logger_;

    std::future<Change::chHashV> fApplyChanges_;

    void collectChanges(std::size_t key, std::unordered_set<std::size_t>& visited, std::vector<Change>& order);
    std::vector<Change> collectDescendants(const std::vector<std::size_t>& roots);

  public:
    ChangeExeService(DbService& cDbService, ChangeTracker& cChangeTracker, Logger& cLogger);
    bool isChangeApplicationDone();
    Change::chHashV getSuccessfulChanges();
    void requestChangeApplication(std::size_t changeKey, SqlAction action);
    void requestChangeApplication(const std::vector<std::size_t> changeKeys, SqlAction action);
    void requestChangeApplication(SqlAction action);
};