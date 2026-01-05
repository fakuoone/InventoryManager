#pragma once

#include <algorithm>
#include <condition_variable>
#include <iostream>
#include <map>
#include <mutex>
#include <pqxx/pqxx>
#include <string>

#include "change.hpp"
#include "logger.hpp"
#include "timing.hpp"

template <typename T>
struct protectedData {
    T data;
    std::mutex mtx;
    std::condition_variable cv;
    bool ready{false};
};

struct transactionData {
    pqxx::connection conn;
    pqxx::work tx;

    transactionData(std::string cConnString) : conn(cConnString), tx(conn) {}

    transactionData(const transactionData&) = delete;
    transactionData& operator=(transactionData& other) = delete;
};

using tStringVector = std::vector<std::string>;
using tHeaderMap = std::map<std::string, tStringVector>;
using tRowMap = std::map<std::string, std::map<std::string, tStringVector>>;

struct completeDbData {
    tStringVector tables;
    tHeaderMap headers;
    tRowMap tableRows;
};

struct protectedConnData {
    std::string connString;
    bool connStringValid;
    std::mutex mtx;
    std::condition_variable cv;
};

class DbInterface {
   private:
    protectedData<tStringVector> tables;
    protectedData<tHeaderMap> tableHeaders;
    protectedData<tRowMap> tableRows;
    Logger& logger;

    protectedConnData connData;

    [[nodiscard]] transactionData getTransaction() {
        std::unique_lock lock(connData.mtx);
        connData.cv.wait(lock, [this] { return connData.connStringValid; });
        return transactionData(connData.connString);
    }

   public:
    DbInterface(Logger& cLogger) : logger(cLogger) {}

    void initializeWithConfigString(const std::string& confString) {
        {
            std::lock_guard lock(connData.mtx);
            connData.connString = confString;
            connData.connStringValid = true;
        }
        connData.cv.notify_all();  // wake all waiting DB threads
    }

    void acquireTables() {
        try {
            transactionData transaction = getTransaction();
            const std::string tableQuery = "SELECT table_name FROM information_schema.tables WHERE table_schema='public'";
            auto result = transaction.tx.query<std::string>(tableQuery);
            logger.pushLog(Log{tableQuery});
            {
                tables.data.clear();
                std::lock_guard<std::mutex> lg{tables.mtx};
                for (const auto& [tableName] : result) {
                    tables.data.push_back(tableName);
                    logger.pushLog(Log{std::format("    table: {}", tableName)});
                }
                transaction.tx.commit();
                tables.ready = true;
            }
            tables.cv.notify_one();
        } catch (std::exception const& e) {
            logger.pushLog(Log{std::format("ERROR: {}", e.what())});
            return;
        }
    }

    void acquireTableContent() {
        {
            std::unique_lock<std::mutex> lockTable(tables.mtx);
            logger.pushLog(Log{"ACQUIRE TABLE CONTENT: Waiting for tables"});
            tables.cv.wait(lockTable, [this] { return tables.ready; });
        }
        {
            std::lock_guard<std::mutex> lgHeaders{tableHeaders.mtx};
            tableHeaders.data.clear();
        }

        logger.pushLog(Log{"ACQUIRE TABLE CONTENT: Preparing headerquery"});
        for (const std::string& tableName : tables.data) {
            try {
                transactionData transaction = getTransaction();
                const std::string headerQuery = std::format("    SELECT * FROM {} WHERE 1=0", tableName);
                logger.pushLog(Log{headerQuery});
                pqxx::result r = transaction.tx.exec(headerQuery);
                transaction.tx.commit();

                std::vector<std::string> headers;
                headers.reserve(static_cast<std::size_t>(r.columns()));

                for (pqxx::row::size_type i = 0; i < r.columns(); ++i) {
                    headers.emplace_back(r.column_name(i));
                    logger.pushLog(Log{std::format("        column: {}", r.column_name(i))});
                }
                {
                    std::lock_guard<std::mutex> lgHeaders{tableHeaders.mtx};
                    std::lock_guard<std::mutex> lgTables{tables.mtx};
                    tableHeaders.data.emplace(tableName, std::move(headers));
                }
            } catch (std::exception const& e) {
                logger.pushLog(Log{std::format("ERROR: {}", e.what())});
                return;
            }
        }
        {
            std::lock_guard<std::mutex> lgHeaders{tableHeaders.mtx};
            std::lock_guard<std::mutex> lgTables{tables.mtx};
            tableHeaders.ready = true;
            tables.ready = false;
        }
        tableHeaders.cv.notify_one();
    }

    void acquireTableRows(const std::string& table, const std::vector<std::string>& cols) {
        {
            logger.pushLog(Log{"ACQUIRE TABLE ROWS: Waiting for tableheaders"});
            std::unique_lock<std::mutex> lockTableHeaders(tableHeaders.mtx);
            tableHeaders.cv.wait(lockTableHeaders, [this] { return tableHeaders.ready; });
        }
        {
            std::lock_guard<std::mutex> lgTables{tables.mtx};
            if (std::find(tables.data.begin(), tables.data.end(), table) == tables.data.end()) {
                logger.pushLog(Log{std::format("ERROR: Acquiring rows: Table {} is unknown.", table)});
                return;
            }
        }
        std::map<std::string, std::vector<std::string>> colCellMap;
        logger.pushLog(Log{"ACQUIRE TABLE ROWS: Preparing headerqueries"});

        std::lock_guard<std::mutex> lgTableHeaders(tableHeaders.mtx);
        for (const auto& col : cols) {
            const std::vector<std::string>& localHeaders = tableHeaders.data.at(table);
            if (std::find(localHeaders.begin(), localHeaders.end(), col) == localHeaders.end()) {
                logger.pushLog(Log{std::format("ERROR: Acquiring rows: Header {} for table {} is unknown.", col, table)});
                return;
            }
            try {
                const std::string headerQuery = std::format("    SELECT {} FROM {}", col, table);
                logger.pushLog(Log{headerQuery});

                transactionData transaction = getTransaction();
                pqxx::result r = transaction.tx.exec(headerQuery);
                transaction.tx.commit();

                std::vector<std::string> cells;
                cells.reserve(static_cast<std::size_t>(r.columns()));

                for (const pqxx::row& row : r) {
                    cells.emplace_back(row[col].c_str());
                    logger.pushLog(Log{std::format("        {}: {}", col, row[col].c_str())});
                }
                colCellMap.emplace(col, cells);

            } catch (std::exception const& e) {
                logger.pushLog(Log{std::format("ERROR: {}", e.what())});
                return;
            }
        }
        {
            std::lock_guard<std::mutex> lgTableRows{tableRows.mtx};
            tableRows.data.emplace(table, std::move(colCellMap));
            tableRows.ready = true;
        }
    }

    completeDbData acquireAllTablesRows() {
        logger.pushLog(Log{"ACQUIRE ALL TABLE ROWS: Waiting for tableheaders"});
        {
            std::unique_lock<std::mutex> lock(tableHeaders.mtx);
            tableHeaders.cv.wait(lock, [this] { return tableHeaders.ready; });
        }

        std::vector<std::pair<std::string, tStringVector>> work;
        {
            std::lock_guard<std::mutex> lockTables(tables.mtx);
            std::lock_guard<std::mutex> lockTableHeaders(tableHeaders.mtx);

            for (const auto& table : tables.data) {
                work.emplace_back(table, tableHeaders.data.at(table));
            }
        }
        for (const auto& [table, headers] : work) {
            acquireTableRows(table, headers);
        }
        return completeDbData{tables.data, tableHeaders.data, tableRows.data};
    }

    std::vector<std::size_t> applyChanges(std::map<std::size_t, Change<int>> changes, sqlAction action) {
        // TODO: Implement logic
        // TODO: Reconsider the map argument. Vector might be more suitable?
        // TODO: A change can require a nested tree of additional changes. The deepest change needs to be executed first
        std::vector<std::size_t> successfulChanges;
        for (const auto& [hash, change] : changes) {
            logger.pushLog(Log{std::format("    Applying change {}", hash)});
            logger.pushLog(Log{std::format("        SQL-command: {}", change.toSQLaction(action))});
            successfulChanges.push_back(hash);
        }
        return successfulChanges;
    }
};