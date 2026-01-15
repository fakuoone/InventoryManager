#pragma once

#include <algorithm>
#include <concepts>
#include <condition_variable>
#include <iostream>
#include <map>
#include <mutex>
#include <string>

#include <pqxx/pqxx>

#include "change.hpp"
#include "logger.hpp"
#include "timing.hpp"

template <typename T>
concept C = std::same_as<T, const Change&> || std::same_as<T, const Change::chHashM&>;

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

    transactionData(const std::string cConnString) : conn(cConnString), tx(conn) {}

    transactionData(const transactionData&) = delete;
    transactionData& operator=(transactionData& other) = delete;
};

enum class headerType { PRIMARY_KEY, FOREIGN_KEY, DATA };

struct headerInfo {
    std::string name;
    headerType type;
    std::string referencedTable;
};

using tStringVector = std::vector<std::string>;
using tHeaderVector = std::vector<headerInfo>;
using tHeaderMap = std::map<std::string, tHeaderVector>;
using tColumnDataMap = std::map<std::string, tStringVector>;
using tRowMap = std::map<std::string, tColumnDataMap>;

struct completeDbData {
    tStringVector tables;
    tHeaderMap headers;
    tRowMap tableRows;
    std::map<std::string, std::size_t> maxPKeys;
};

struct protectedConnData {
    std::string connString;
    bool connStringValid{false};
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
            connData.connStringValid = !confString.empty();
        }
        connData.cv.notify_all();  // wake all waiting DB threads
    }

    void acquireTables() {
        try {
            // TODO: There is a UB somewhere related to getTransaction
            /*
            ~~18064~~ Error #33: UNINITIALIZED READ: reading 0x000000672effe29a-0x000000672effe2a0 6 byte(s) within 0x000000672effe290-0x000000672effe2b8
            ~~18064~~ # 0 ntdll.dll!RcContinueExit
            ~~18064~~ # 1 ntdll.dll!RtlUnwindEx
            ~~18064~~ # 2 libgcc_s_seh-1.dll!?                                                      +0x0      (0x00007ffc4bcac44f <libgcc_s_seh-1.dll+0x1c44f>)
            ~~18064~~ # 3 _ZNSt17_Function_handlerIFSt10unique_ptrINSt13__future_base12_Result_baseENS2_8_DeleterEEvENS1_12_Task_setterIS0_INS1_7_ResultIvEES3_EZNS1_11_Task_stateIZN10ThreadPool6submitIM11DbInterfaceFvvEJPSD_EEEDaOT_DpOT0_EUlvE_SaIiEFvvEE6_M_runEvEUlvE_vEEE9_M_invo
            ~~18064~~ # 4 KERNELBASE.dll!RaiseException
            ~~18064~~ # 5 DbInterface::getTransaction
            */
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

    tHeaderVector getTableHeaders(const std::string& table) {
        transactionData transaction = getTransaction();
        const std::string headerQuery = std::format("    SELECT * FROM {} WHERE 1=0", table);
        logger.pushLog(Log{headerQuery});
        pqxx::result r = transaction.tx.exec(headerQuery);
        transaction.tx.commit();

        std::vector<std::string> headers;
        headers.reserve(static_cast<std::size_t>(r.columns()));

        for (pqxx::row::size_type i = 0; i < r.columns(); ++i) {
            headers.emplace_back(r.column_name(i));
            logger.pushLog(Log{std::format("        column: {}", r.column_name(i))});
        }

        return getHeaderInfo(table, std::move(headers));
    }

    tHeaderVector getHeaderInfo(const std::string& table, std::vector<std::string> rawHeaders) {
        tHeaderVector headers;
        transactionData transaction = getTransaction();

        for (const std::string& header : rawHeaders) {
            headerInfo info{header, headerType::DATA, ""};
            // Primary key
            const std::string pkQuery = std::format(
                "SELECT 1 "
                "FROM pg_constraint c "
                "JOIN pg_attribute a "
                "  ON a.attrelid = c.conrelid "
                " AND a.attnum = ANY (c.conkey) "
                "WHERE c.contype = 'p' "
                "  AND c.conrelid = '{}'::regclass "
                "  AND a.attname = '{}'",
                table, header);

            pqxx::result pkResult = transaction.tx.exec(pkQuery);

            if (!pkResult.empty()) {
                info.type = headerType::PRIMARY_KEY;
                headers.push_back(info);
                continue;
            }

            // Foreign key
            const std::string fkQuery = std::format(
                "SELECT ccu.table_name AS referenced_table "
                "FROM information_schema.table_constraints tc "
                "JOIN information_schema.key_column_usage kcu "
                "  ON tc.constraint_name = kcu.constraint_name "
                "JOIN information_schema.constraint_column_usage ccu "
                "  ON ccu.constraint_name = tc.constraint_name "
                "WHERE tc.constraint_type = 'FOREIGN KEY' "
                "  AND kcu.table_name = '{}' "
                "  AND kcu.column_name = '{}'",
                table, header);

            pqxx::result fkResult = transaction.tx.exec(fkQuery);
            if (!fkResult.empty()) {
                info.type = headerType::FOREIGN_KEY;
                info.referencedTable = fkResult[0]["referenced_table"].c_str();
            }
            headers.push_back(info);
        }
        transaction.tx.commit();
        return headers;
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

        // get headers
        logger.pushLog(Log{"ACQUIRE TABLE CONTENT: Preparing headerquery"});
        for (const std::string& tableName : tables.data) {
            try {
                tHeaderVector headers = getTableHeaders(tableName);
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

    void acquireTableRows(const std::string& table, const tHeaderVector& cols) {
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
            const tHeaderVector& localHeaders = tableHeaders.data.at(table);
            if (std::ranges::find_if(localHeaders, [&](const headerInfo& h) { return h.name == col.name; }) == localHeaders.end()) {
                logger.pushLog(Log{std::format("ERROR: Acquiring rows: Header {} for table {} is unknown.", col.name, table)});
                return;
            }
            try {
                const std::string headerQuery = std::format("    SELECT {} FROM {}", col.name, table);
                logger.pushLog(Log{headerQuery});

                transactionData transaction = getTransaction();
                pqxx::result r = transaction.tx.exec(headerQuery);
                transaction.tx.commit();

                std::vector<std::string> cells;
                cells.reserve(static_cast<std::size_t>(r.columns()));

                for (const pqxx::row& row : r) {
                    cells.emplace_back(row[col.name].c_str());
                    logger.pushLog(Log{std::format("        {}: {}", col.name, row[col.name].c_str())});
                }
                colCellMap.emplace(col.name, cells);

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

        std::vector<std::pair<std::string, tHeaderVector>> work;
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
        return completeDbData{tables.data, tableHeaders.data, tableRows.data, std::map<std::string, std::size_t>{}};
    }

    template <typename C>
    Change::chHashV applyChanges(C change_s, sqlAction action) {
        // TODO: A change can require a nested tree of additional changes. The deepest change needs to be executed first
        Change::chHashV successfulChanges;
        if constexpr (std::same_as<C, Change>) {
            if (applySingleChange(change_s, action)) { successfulChanges.push_back(change_s.getHash()); };
        } else {
            for (const auto& [hash, change] : change_s) {
                if (applySingleChange(change, action)) { successfulChanges.push_back(hash); }
            }
        }
        return successfulChanges;
    }

    bool applySingleChange(const Change& change, sqlAction action) {
        logger.pushLog(Log{std::format("    Applying change {}", change.getHash())});
        logger.pushLog(Log{std::format("        SQL-command: {}", change.toSQLaction(action))});
        return true;
    }
};