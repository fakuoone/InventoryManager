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
    using Ngram = std::vector<std::string>;

  private:
    Logger& logger_;
    DbService& dbService_;
    ThreadPool& pool_;
    static constexpr uint8_t ngramLength_ = 3;

    std::shared_ptr<const CompleteDbData> dbData_;
    UI::DataStates& dataStates_;

    std::future<std::shared_ptr<const CompleteDbData>> fFilteredData_;
    std::mutex filterMtx_;

    std::shared_ptr<const CompleteDbData> filterByKeyword(const std::string& keyword, float similarityThreshhold);
    HitMap findHitsByKeyword(const std::string& keyword, float similarityThreshhold);
    void convertHitsToDbData(const HitMap& hitMap, CompleteDbData& newDbData);
    bool isHit(const std::string& value, const std::string& keyword, float similarityThreshhold);
    Ngram generateNgram(const std::string& value);
    float ngramDifference(const Ngram& a, const Ngram& b);

  public:
    DbFilter(DbService& cDbService, ThreadPool& cThreadPool, Logger& cLogger, UI::DataStates& cDataStates);
    void setData(std::shared_ptr<const CompleteDbData> newData);
    bool dataReady() const;
    std::shared_ptr<const CompleteDbData> getFilteredData();
    void startFilterSearch(const std::string keyword, float similarityThreshhold);
};
