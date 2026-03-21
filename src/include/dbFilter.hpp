#pragma once

#include <future>
#include <set>

#include "dataTypes.hpp"
#include "dbService.hpp"
#include "threadPool.hpp"

struct Hits {
    std::set<std::size_t> hits;
    std::set<std::string> ukeyHits;
};

class DbFilter {
    using HitMap = std::unordered_map<std::string, Hits>;

  private:
    Logger& logger_;
    DbService& dbService_;
    ThreadPool& pool_;

    std::shared_ptr<const CompleteDbData> dbData_;
    UI::DataStates& dataStates_;

    std::future<std::shared_ptr<const CompleteDbData>> fFilteredData_;
    std::mutex filterMtx_;

    std::shared_ptr<const CompleteDbData> filterByKeyword(const std::string& keyword);
    HitMap findHitsByKeyword(const std::string& keyword);
    void convertHitsToDbData(const HitMap& hitMap, CompleteDbData& newDbData);

  public:
    DbFilter(DbService& cDbService, ThreadPool& cThreadPool, Logger& cLogger, UI::DataStates& cDataStates);
    void setData(std::shared_ptr<const CompleteDbData> newData);
    bool dataReady() const;
    std::shared_ptr<const CompleteDbData> getFilteredData();
    void startFilterSearch(const std::string keyword);
};
