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

    std::map<std::string, std::size_t> calcMaxPKeys(CompleteDbData data) {
        std::map<std::string, std::size_t> maxPKeys;
        for (const auto& table : data.tables) {
            // Get max index of pkeys
            const StringVector& keyVector = data.tableRows.at(table).at(data.headers.at(table).pkey);
            auto it2 = std::max_element(keyVector.begin(), keyVector.end(), [](const std::string& key1, const std::string& key2) {
                return std::stoll(key1) < std::stoll(key2);
            });
            std::size_t maxKey = 0;
            if (it2 != keyVector.end()) { maxKey = static_cast<std::size_t>(std::stoll(*it2)); }
            maxPKeys[table] = maxKey;
        }
        return maxPKeys;
    }

    bool isDataReady() {
        if (!pendingData_ && fCompleteDbData_.valid() &&
            fCompleteDbData_.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
            auto data = fCompleteDbData_.get();
            if (!validateCompleteDbData(data)) { return false; }
            pendingData_ = std::make_unique<CompleteDbData>(std::move(data));
            fMaxPKeys_ = pool_.submit(&DbService::calcMaxPKeys, this, std::cref(*pendingData_));
        }

        if (pendingData_ && fMaxPKeys_.valid() && fMaxPKeys_.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
            pendingData_->maxPKeys = fMaxPKeys_.get();
            dbData_ = std::move(pendingData_);
            dataAvailable_ = true;
        }

        return dataAvailable_;
    }

  public:
    DbService(DbInterface& cDbData, ThreadPool& cPool, Config& cConfig, Logger& cLogger)
        : dbInterface_(cDbData), pool_(cPool), config_(cConfig), logger_(cLogger) {}

    void startUp() {
        pendingData_.reset();
        dataAvailable_.store(false, std::memory_order_release);
        pool_.submit(&DbInterface::acquireTables, &dbInterface_);
        pool_.submit(&DbInterface::acquireTableContent, &dbInterface_);
        fCompleteDbData_ = pool_.submit(&DbInterface::acquireAllTablesRows, &dbInterface_);
    }

    void refetch() {
        pendingData_.reset();
        dataAvailable_.store(false, std::memory_order_release);
        fCompleteDbData_ = pool_.submit(&DbInterface::acquireAllTablesRows, &dbInterface_);
    }

    std::expected<std::shared_ptr<const CompleteDbData>, bool> getCompleteData() {
        if (isDataReady()) {
            return dbData_;
        } else {
            return std::unexpected(false);
        }
    }

    IndexPKeyPair findIndexAndPKeyOfExisting(const std::string& table, const Change::colValMap& cells) const {
        const HeadersInfo& headers = dbData_->headers.at(table);
        const std::string& uKeyName = headers.uKeyName;
        const ColumnDataMap& row = dbData_->tableRows.at(table);
        const StringVector& uKeyColumn = row.at(uKeyName);
        IndexPKeyPair result{INVALID_ID, INVALID_ID};

        if (!cells.contains(uKeyName)) { return result; }

        auto itExisting = std::find(uKeyColumn.begin(), uKeyColumn.end(), cells.at(uKeyName));
        if (itExisting != uKeyColumn.end()) {
            logger_.pushLog(Log{std::format("INFO: Table {} with unique key {}: {} already exists.", table, uKeyName, cells.at(uKeyName))});
            result.index = itExisting - uKeyColumn.begin();
            const std::string& rowId = row.at(headers.pkey).at(result.index);
            result.pkey = static_cast<std::size_t>(std::stoi(rowId));
        }
        return result;
    }

    bool hasQuantityColumn(const std::string& table) const {
        const HeadersInfo& headers = dbData_->headers.at(table);
        const std::string& quantityColumn = config_.getQuantityColumn();
        auto itHasQuantityHeader =
            std::find_if(headers.data.begin(), headers.data.end(), [&](const HeaderInfo& h) { return h.name == quantityColumn; });
        return itHasQuantityHeader != headers.data.end();
    }

    void
    updateChangeQuantity(const std::string& table, Change::colValMap& cells, const std::size_t index, QuantityOperation operation) const {
        const std::string& quantityColumn = config_.getQuantityColumn();
        if (!hasQuantityColumn(table)) { return; }
        try {
            const auto& tableRows = dbData_->tableRows.at(table);
            const auto& column = tableRows.at(quantityColumn);
            if (index >= column.size()) {
                logger_.pushLog(
                    Log{std::format("ERROR: Quantity index {} out of range for table '{}' column '{}'.", index, table, quantityColumn)});
                return;
            }

            std::size_t quantityDb{};
            try {
                quantityDb = static_cast<std::size_t>(std::stoll(column[index]));
            } catch (...) {
                logger_.pushLog(Log{std::format("ERROR: Invalid quantity value '{}' in DB for table '{}' column '{}' row {}.",
                                                column[index],
                                                table,
                                                quantityColumn,
                                                index)});
                return;
            }

            auto [cell, inserted] = cells.try_emplace(quantityColumn, std::to_string(quantityDb));
            std::size_t changeValue{};
            try {
                changeValue = static_cast<std::size_t>(std::stoll(cell->second));
            } catch (...) {
                logger_.pushLog(
                    Log{std::format("ERROR: Invalid quantity change value '{}' for column '{}'.", cell->second, quantityColumn)});
                return;
            }

            logger_.pushLog(
                Log{std::format("EXISTING QUANTITY IS: {}. Change-quantity will operate with value {}.", quantityDb, changeValue)});

            switch (operation) {
            case QuantityOperation::SET:
                cells.at(quantityColumn) = std::to_string(changeValue);
                break;
            case QuantityOperation::ADD:
                cells.at(quantityColumn) = std::to_string(quantityDb + changeValue);
                break;
            case QuantityOperation::SUB:
                if (changeValue > quantityDb) {
                    logger_.pushLog(Log{std::format("ERROR: Quantity subtraction would underflow: {} - {}.", quantityDb, changeValue)});
                    return;
                }
                cells.at(quantityColumn) = std::to_string(quantityDb - changeValue);
                break;
            }
        } catch (const std::exception& e) {
            logger_.pushLog(Log{std::format("ERROR: Failed to update quantity for table '{}': {}", table, e.what())});
        }
    }

    bool validateCompleteDbData(const CompleteDbData& data) const {
        // tablecount matches everywhere
        std::size_t tableCount = data.tables.size();
        if (tableCount != data.headers.size() || tableCount != data.tableRows.size()) {
            logger_.pushLog(Log{"ERROR: Table data is mismatching in size."});
            return false;
        }
        // all tables have headers
        for (const auto& table : data.tables) {
            if (!data.headers.contains(table)) {
                logger_.pushLog(Log{std::format("ERROR: Table {} has no header information.", table)});
                return false;
            }
            // columns have the same values as rows have keys
            bool pKeyFound{false};
            for (const auto& header : data.headers.at(table).data) {
                if (header.type == DB::HeaderTypes::PRIMARY_KEY) { pKeyFound = true; };
                if (!data.tableRows.at(table).contains(header.name)) {
                    logger_.pushLog(Log{std::format("ERROR: Table {} has header {} which has no data.", table, header.name)});
                    return false;
                }
            }
            if (!pKeyFound) {
                logger_.pushLog(Log{std::format("ERROR: Table {} has no primary key.", table)});
                return false;
            }
        }
        return true;
    }

    bool validateChange(Change& change, bool fromGeneration) const {
        const StringVector& tables = dbData_->tables;
        auto it = std::find(tables.begin(), tables.end(), change.getTable());
        if (it == tables.end()) { return false; }
        bool setValidity = true;
        bool allowInvalidChange = change.hasParent() && fromGeneration;

        switch (change.getType()) {
        case ChangeType::DELETE_ROW:
            break;
        case ChangeType::INSERT_ROW:
            if (findIndexAndPKeyOfExisting(change.getTable(), change.getCells()).index != INVALID_ID) { return false; }
            [[fallthrough]];
        case ChangeType::UPDATE_CELLS: {
            const Change::colValMap& cells = change.getCells();
            const HeadersInfo& headers = dbData_->headers.at(change.getTable());
            // check non-nullable column count
            std::size_t reqColumnCount =
                std::count_if(headers.data.begin(), headers.data.end(), [](const HeaderInfo& h) { return !h.nullable; }) - 1;
            if (reqColumnCount > cells.size() && change.getType() == ChangeType::INSERT_ROW) { setValidity = false; }
            if (cells.size() > (headers.data.size() - 1) && !allowInvalidChange) {
                logger_.pushLog(Log{std::format("ERROR: Change is invalid because not enough columns were "
                                                "supplied to satisfy the non-null table columns.")});
                change.setLocalValidity(false);
                return false;
            }

            for (const auto& header : headers.data) {
                if (header.type == DB::HeaderTypes::PRIMARY_KEY) {
                    if (cells.contains(header.name)) {
                        logger_.pushLog(Log{std::format("ERROR: Change is not allowed to provide the primary key.")});
                        change.setLocalValidity(false);
                        return false;
                    }
                    continue;
                } else if (!header.nullable) {
                    // non-nullable column is null
                    if (!cells.contains(header.name)) {
                        if (change.getType() == ChangeType::INSERT_ROW) {
                            if (!allowInvalidChange) {
                                logger_.pushLog(
                                    Log{std::format("ERROR: Header {} is not nullable and no value was provided.", header.name)});
                                change.setLocalValidity(false);
                                return false;
                            }
                            setValidity = false;
                        }
                    } else {
                        if (cells.at(header.name).empty()) {
                            if (!allowInvalidChange) {
                                logger_.pushLog(Log{std::format("ERROR: Header {} is not nullable "
                                                                "but empty value was provided.",
                                                                header.name)});
                                return false;
                            }
                            setValidity = false;
                        }
                    }
                }
            }
        } break;
        default:
            break;
        }
        change.setLocalValidity(setValidity);
        return true;
    }

    std::vector<Change> getRequiredChanges(const Change& change, const std::map<std::string, std::size_t>& ids) const {
        const std::string& table = change.getTable();
        std::vector<Change> changes;
        const HeadersInfo& headers = dbData_->headers.at(table);
        const Change::colValMap& cells = change.getCells();

        // early return if the thing already exists in its entirety -> TODO: might tank performance?,
        // maybe its wiser to prevent this situation from elsewhere
        if (cells.contains(headers.uKeyName)) {
            if (findIndexAndPKeyOfExisting(table, cells).index != INVALID_ID) { return changes; }
        }

        for (const auto& [col, val] : cells) {
            // find foreign key thats required
            auto it1 = std::ranges::find_if(headers.data, [&](const HeaderInfo& h) {
                return h.name == col && (h.type == DB::HeaderTypes::FOREIGN_KEY ||
                                         !h.referencedTable.empty()); // not very clean. If there are more cases where the enum is no
                                                                      // sufficient, the internal enum design needs to be refactored
            });
            if (it1 != headers.data.end()) { // && it1->referencedTable != table) {
                bool alreadyExists = it1->referencedTable == table ? checkReferencedPKeyValue(it1->referencedTable, val)
                                                                   : checkReferencedUKeyValue(it1->referencedTable, it1->nullable, val);
                if (!alreadyExists) {
                    Change::colValMap requiredCells;
                    if (it1->referencedTable == table) {
                        requiredCells.emplace(dbData_->headers.at(it1->referencedTable).pkey, val);
                    } else {
                        requiredCells.emplace(dbData_->headers.at(it1->referencedTable).uKeyName, val);
                    }
                    Change reqChange{requiredCells, ChangeType::INSERT_ROW, getTable(it1->referencedTable)};
                    reqChange.addParent(change.getKey());
                    changes.emplace_back(reqChange);
                }
            }
        }
        return changes;
    }

    bool checkReferencedPKeyValue(const std::string& ref, const std::string& val) const {
        // does pkey-value already exist
        if (val.empty()) { return true; }
        std::string pKey = dbData_->headers.at(ref).pkey;
        auto it1 = std::ranges::find_if(dbData_->tableRows.at(ref).at(pKey), [&](const std::string& h) { return h == val; });
        if (it1 != dbData_->tableRows.at(ref).at(pKey).end()) { return true; }
        return false;
    }

    bool checkReferencedUKeyValue(const std::string& ref, bool nullable, const std::string& val) const {
        // does ukey-value already exist
        if (val.empty() && nullable) { return true; }
        std::string uKey = dbData_->headers.at(ref).uKeyName;
        auto it1 = std::ranges::find_if(dbData_->tableRows.at(ref).at(uKey), [&](const std::string& h) { return h == val; });
        if (it1 != dbData_->tableRows.at(ref).at(uKey).end()) { return true; }
        return false;
    }

    void initializeDbInterface(const std::string& configString) const { dbInterface_.initializeWithConfigString(configString); }

    std::future<Change::chHashV> requestChangeApplication(std::vector<Change> changes, SqlAction action) const {
        return pool_.submit(
            [this](auto change, SqlAction act) { return dbInterface_.applyChanges(std::move(change), act); }, std::move(changes), action);
        //      return pool.submit(&DbInterface::applyChanges, &dbInterface, std::move(change_s),
        //      action);
    }

    ImTable getTable(const std::string& tableName) const {
        auto it = std::find(dbData_->tables.begin(), dbData_->tables.end(), tableName);
        ImTable tableData{tableName, 0};
        if (it != dbData_->tables.end()) { tableData.id = static_cast<uint16_t>(std::distance(dbData_->tables.begin(), it)); }
        return tableData;
    }

    std::string getTableUKey(const std::string& table) const { return dbData_->headers.at(table).uKeyName; }

    HeaderInfo getTableHeaderInfo(const std::string& table, const std::string& header) const {
        const HeaderVector& headers = dbData_->headers.at(table).data;
        auto it = std::find_if(headers.begin(), headers.end(), [&](const HeaderInfo& h) { return h.name == header; });
        return *it;
    }
};
