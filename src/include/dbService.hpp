#pragma once

#include "change.hpp"
#include "config.hpp"
#include "dbInterface.hpp"
#include "logger.hpp"
#include "threadPool.hpp"

#include <expected>
#include <future>

enum class QuantityOperation { ADD, SUB, SET };

struct IndexPKeyPair {
    std::size_t index;
    std::size_t pkey;
};

class DbService {
  private:
    DbInterface& dbInterface_;
    ThreadPool& pool_;
    Config& config_;
    Logger& logger_;

    std::future<CompleteDbData> fCompleteDbData_;
    std::shared_ptr<const CompleteDbData> dbData_;
    std::unique_ptr<CompleteDbData> pendingData_;
    std::future<std::map<std::string, std::size_t>> fMaxPKeys_;

    std::atomic<bool> dataAvailable_{false};

    bool isDataReady();

  public:
    DbService(DbInterface& cDbData, ThreadPool& cPool, Config& cConfig, Logger& cLogger);
    void startUp();
    void refetch();
    std::expected<std::shared_ptr<const CompleteDbData>, bool> getCompleteData();
    std::map<std::string, std::size_t> calcMaxPKeys(const CompleteDbData& data) const;
    IndexPKeyPair findIndexAndPKeyOfExisting(const std::string& table, const Change::colValMap& cells) const;
    bool hasQuantityColumn(const std::string& table) const;
    void
    updateChangeQuantity(const std::string& table, Change::colValMap& cells, const std::size_t index, QuantityOperation operation) const;
    bool validateCompleteDbData(const CompleteDbData& data) const;
    bool validateChange(Change& change, bool fromGeneration) const;
    std::vector<Change> getRequiredChanges(const Change& change, const std::map<std::string, std::size_t>& ids) const;
    bool checkReferencedPKeyValue(const std::string& ref, const std::string& val) const;
    bool checkReferencedUKeyValue(const std::string& ref, bool nullable, const std::string& val) const;
    void initializeDbInterface(const std::string& configString) const;
    std::future<Change::chHashV> requestChangeApplication(std::vector<Change> changes, SqlAction action) const;
    ImTable getTable(const std::string& tableName) const;
    std::string getTableUKey(const std::string& table) const;
    HeaderInfo getTableHeaderInfo(const std::string& table, const std::string& header) const;
};
