#include "dbFilter.hpp"

std::shared_ptr<const CompleteDbData> DbFilter::filterByKeyword(const std::string& keyword, float similarityThreshhold) {
    similarityThreshhold = std::min(std::max(0.0f, similarityThreshhold), 1.0f);
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

    HitMap hitMap = findHitsByKeyword(keyword, similarityThreshhold);
    convertHitsToDbData(hitMap, dbData);

    dbData.maxPKeys = dbService_.calcMaxPKeys(dbData);
    auto result = std::make_shared<CompleteDbData>(dbData);
    return result;
}

DbFilter::HitMap DbFilter::findHitsByKeyword(const std::string& keyword, float similarityThreshhold) {
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
                if (isHit(value, keyword, similarityThreshhold)) {
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

DbFilter::Ngram DbFilter::generateNgram(const std::string& value) {
    Ngram array;
    std::size_t len;
    if (value.size() < ngramLength_) {
        len = value.size() - 1;
    } else {
        len = value.size() - ngramLength_ + 1;
    }

    for (size_t i = 0; i < len; i++) {
        array.push_back(value.substr(i, ngramLength_));
    }
    return array;
}

float DbFilter::ngramDifference(const Ngram& a, const Ngram& b) {
    if (a.size() == 0 || b.size() == 0) { return 0.0f; }
    std::size_t count{0};
    std::vector<std::size_t> ngramEqualities;
    for (std::size_t i = 0; i < a.size(); i++) {
        for (std::size_t j = 0; j < b.size(); j++) {
            if (a[i] == b[j]) {
                ngramEqualities.push_back(j);
                count++;
                continue;
            }
        }
    }

    if (count == 0) { return 0; }

    uint8_t maxDensity{0};
    uint8_t density{0};
    static constexpr uint8_t windowSize = 4;
    for (std::size_t i = 0; i < ngramEqualities.size() - 1; i++) {
        if (i % windowSize == 0) { density = 0; }
        if (ngramEqualities[i + 1] - ngramEqualities[i] == 1) { density++; }
        if (density > maxDensity) { maxDensity = density; }
    }

    return static_cast<float>(count) / std::max(a.size(), b.size()) + static_cast<float>(maxDensity) / windowSize;
}

bool DbFilter::isHit(const std::string& value, const std::string& keyword, float similarityThreshhold) {
    Ngram valueNgram = DbFilter::generateNgram(value);
    Ngram keywordNgram = DbFilter::generateNgram(keyword);

    if (value.find(keyword) != std::string::npos) { // Direct find#8°
        return true;
    }

    float ngramDiff = DbFilter::ngramDifference(valueNgram, keywordNgram);
    if (ngramDiff > 0) { logger_.pushLog(Log{std::format("DATA {} WITH SIMILARITY: {}", value, ngramDiff)}); }
    return ngramDiff > similarityThreshhold;
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

void DbFilter::startFilterSearch(const std::string keyword, float similarityThreshhold) {
    fFilteredData_ = pool_.submit(&DbFilter::filterByKeyword, this, keyword, similarityThreshhold);
}
