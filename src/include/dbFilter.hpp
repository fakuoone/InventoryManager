#pragma once

#include <future>
#include <set>

#include "dataTypes.hpp"
#include "dbService.hpp"
#include "threadPool.hpp"

class DbFilter {
    using HitMap = std::unordered_map<std::string, std::set<std::size_t>>;

  private:
    Logger& logger_;
    DbService& dbService_;
    ThreadPool& pool_;

    std::shared_ptr<const CompleteDbData> dbData_;
    UI::DataStates& dataStates_;

    std::future<std::shared_ptr<const CompleteDbData>> fFilteredData_;
    std::mutex filterMtx_;

    std::shared_ptr<const CompleteDbData> filterByKeyword(const std::string& keyword) {
        std::lock_guard<std::mutex> lg(filterMtx_);
        logger_.pushLog(Log{std::format("FILTERING BY {}", keyword)});
        CompleteDbData dbData;
        if (dataStates_.dbData != UI::DataState::DATA_READY) { return std::make_shared<CompleteDbData>(dbData); }

        dbData.tables = dbData_->tables;
        dbData.headers = dbData_->headers;
        dbData.tableRows = dbData_->tableRows;

        // clean slate for filtered data
        for (auto& [table, headerData] : dbData.tableRows) {
            for (auto& [_, header] : headerData) {
                header.clear();
            }
        }

        HitMap hitMap = findHitsByKeyword(keyword);
        convertHitsToDbData(hitMap, dbData);

        dbData.maxPKeys = dbService_.calcMaxPKeys(dbData);
        auto result = std::make_shared<CompleteDbData>(dbData);
        return result;
    }

    HitMap findHitsByKeyword(const std::string& keyword) {
        HitMap hitMap;
        for (const auto& [table, headerData] : dbData_->tableRows) {
            for (const auto& [headerName, vec] : headerData) {
                std::size_t tableRowIndex = 0;
                for (const auto& value : vec) {
                    if (value.find(keyword) != std::string::npos) {
                        if (!hitMap.contains(table)) { hitMap.emplace(table, std::set<std::size_t>{}); }
                        hitMap.at(table).insert(tableRowIndex);
                    }
                    tableRowIndex++;
                }
            }
        }
        return hitMap;
    }

    void convertHitsToDbData(const HitMap& hitMap, CompleteDbData& newDbData) {
        // each hit is one db-row -> gets added here
        for (const auto& [table, hits] : hitMap) {
            ColumnDataMap& rowsNewTable = newDbData.tableRows.at(table);
            const ColumnDataMap& rowsOldTable = dbData_->tableRows.at(table);
            for (auto& [headerName, vecNew] : rowsNewTable) {
                const StringVector& rowsOldHeader = rowsOldTable.at(headerName);
                for (const std::size_t rowIndex : hits) {
                    vecNew.push_back(rowsOldHeader.at(rowIndex));
                }
            }
        }
    }

  public:
    DbFilter(DbService& cDbService, ThreadPool& cThreadPool, Logger& cLogger, UI::DataStates& cDataStates)
        : dbService_(cDbService), pool_(cThreadPool), logger_(cLogger), dataStates_(cDataStates) {}

    void setData(std::shared_ptr<const CompleteDbData> newData) {
        std::lock_guard<std::mutex> lg(filterMtx_);
        dbData_ = newData;
    }

    bool dataReady() const {
        if (fFilteredData_.valid() && fFilteredData_.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) { return true; }
        return false;
    }

    std::shared_ptr<const CompleteDbData> getFilteredData() {
        std::shared_ptr<const CompleteDbData> data;
        if (!dataReady()) { return data; }
        data = fFilteredData_.get();
        return data;
    }

    void startFilterSearch(const std::string keyword) { fFilteredData_ = pool_.submit(&DbFilter::filterByKeyword, this, keyword); }
};
