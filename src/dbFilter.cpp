#include "dbFilter.hpp"

std::shared_ptr<const CompleteDbData> DbFilter::filterByKeyword(const std::string& keyword) {
    CompleteDbData dbData;
    if (keyword.empty()) { return std::make_shared<CompleteDbData>(dbData); }
    if (dataStates_.dbData != UI::DataState::DATA_READY) { return std::make_shared<CompleteDbData>(dbData); }
    std::lock_guard<std::mutex> lg(filterMtx_);
    logger_.pushLog(Log{std::format("FILTERING BY {}", keyword)});

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

DbFilter::HitMap DbFilter::findHitsByKeyword(const std::string& keyword) {
    HitMap hitMap;

    // sort by depth to prevent multiple iterations
    std::vector<std::string> depthOrder;
    depthOrder.reserve(dbData_->headers.size());

    for (const auto& [key, value] : dbData_->headers) {
        depthOrder.push_back(key);
    }
    std::sort(depthOrder.begin(), depthOrder.end(), [&](const std::string& a, const std::string& b) {
        return dbData_->headers.at(a).maxDepth < dbData_->headers.at(b).maxDepth;
    });

    for (const std::string& tableName : depthOrder) {
        const ColumnDataMap& rowData = dbData_->tableRows.at(tableName);
        std::size_t headerIndex = 0;
        for (const auto& [headerName, vec] : rowData) {
            std::size_t tableRowIndex = 0;
            for (const auto& value : vec) {
                if (!hitMap.contains(tableName)) { hitMap.emplace(tableName, Hits{}); }
                if (value.empty()) {
                    tableRowIndex++;
                    continue;
                }
                const std::string& ukey = dbData_->headers.at(tableName).uKeyName;
                if (value.find(keyword) != std::string::npos) { // Direct find
                    hitMap.at(tableName).hits.insert(tableRowIndex);
                    // add unique key as hit so that dependant data gets affected aswell
                    hitMap.at(tableName).ukeyHits.insert(rowData.at(ukey).at(tableRowIndex));
                }
                const std::string& referencedTable = dbData_->headers.at(tableName).data.at(headerIndex).referencedTable;
                if (!referencedTable.empty()) {
                    if (hitMap.at(referencedTable).ukeyHits.contains(value)) { // Dependency
                        hitMap.at(tableName).hits.insert(tableRowIndex);
                        // add unique key as hit so that dependant data gets affected aswell
                        hitMap.at(tableName).ukeyHits.insert(rowData.at(ukey).at(tableRowIndex));
                    }
                }
                tableRowIndex++;
            }
            headerIndex++;
        }
    }
    return hitMap;
}

void DbFilter::convertHitsToDbData(const HitMap& hitMap, CompleteDbData& newDbData) {
    // each hit is one db-row -> gets added here
    for (const auto& [table, hits] : hitMap) {
        ColumnDataMap& rowsNewTable = newDbData.tableRows.at(table);
        const ColumnDataMap& rowsOldTable = dbData_->tableRows.at(table);
        for (auto& [headerName, vecNew] : rowsNewTable) {
            const StringVector& rowsOldHeader = rowsOldTable.at(headerName);
            for (const std::size_t rowIndex : hits.hits) {
                vecNew.push_back(rowsOldHeader.at(rowIndex));
            }
        }
    }
}

DbFilter::DbFilter(DbService& cDbService, ThreadPool& cThreadPool, Logger& cLogger, UI::DataStates& cDataStates)
    : dbService_(cDbService), pool_(cThreadPool), logger_(cLogger), dataStates_(cDataStates) {}

void DbFilter::setData(std::shared_ptr<const CompleteDbData> newData) {
    std::lock_guard<std::mutex> lg(filterMtx_);
    dbData_ = newData;
}

bool DbFilter::dataReady() const {
    if (fFilteredData_.valid() && fFilteredData_.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) { return true; }
    return false;
}

std::shared_ptr<const CompleteDbData> DbFilter::getFilteredData() {
    std::shared_ptr<const CompleteDbData> data;
    if (!dataReady()) { return data; }
    data = fFilteredData_.get();
    return data;
}

void DbFilter::startFilterSearch(const std::string keyword) {
    fFilteredData_ = pool_.submit(&DbFilter::filterByKeyword, this, keyword);
}
