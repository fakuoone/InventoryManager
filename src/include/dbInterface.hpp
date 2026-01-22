#pragma once

#include "change.hpp"
#include "logger.hpp"
#include "timing.hpp"

#include <algorithm>
#include <condition_variable>
#include <iostream>
#include <mutex>

#include <pqxx/pqxx>

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

enum class headerType { PRIMARY_KEY, FOREIGN_KEY, UNIQUE_KEY, DATA };

struct tHeaderInfo {
    std::string name;
    headerType type;
    std::string referencedTable;
    bool nullable;
};

using tHeaderVector = std::vector<tHeaderInfo>;
struct tHeadersInfo {
    tHeaderVector data;
    std::string pkey;      // ID
    std::string uKeyName;  // NAME
};

using tStringVector = std::vector<std::string>;
using tHeaderMap = std::map<std::string, tHeadersInfo>;
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

    tHeadersInfo getTableHeaders(const std::string& table) {
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

    tHeadersInfo getHeaderInfo(const std::string& table, std::vector<std::string> rawHeaders) {
        tHeadersInfo headers;
        transactionData transaction = getTransaction();

        for (const std::string& header : rawHeaders) {
            tHeaderInfo info{header, headerType::DATA, "", true};

            // Nullable
            const std::string nullQuery = std::format(
                "SELECT NOT a.attnotnull AS is_nullable "
                "FROM pg_attribute a "
                "JOIN pg_class c ON c.oid = a.attrelid "
                "WHERE c.relname = '{}' "
                "  AND a.attname = '{}' "
                "  AND a.attnum > 0 "
                "  AND NOT a.attisdropped",
                table, header);

            pqxx::result nullResult = transaction.tx.exec(nullQuery);
            if (!nullResult.empty()) { info.nullable = nullResult[0]["is_nullable"].as<bool>(); }

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
                headers.data.push_back(info);
                headers.pkey = header;
                continue;
            }

            // UNIQUE (single-column only)
            const std::string uqQuery = std::format(
                "SELECT array_length(c.conkey, 1) AS key_len "
                "FROM pg_constraint c "
                "JOIN pg_attribute a "
                "  ON a.attrelid = c.conrelid "
                " AND a.attnum = ANY (c.conkey) "
                "WHERE c.contype = 'u' "
                "  AND c.conrelid = '{}'::regclass "
                "  AND a.attname = '{}'",
                table, header);

            pqxx::result uqResult = transaction.tx.exec(uqQuery);
            if (!uqResult.empty()) {
                int keyLen = uqResult[0]["key_len"].as<int>();
                if (keyLen == 1) {
                    info.type = headerType::UNIQUE_KEY;
                    headers.uKeyName = header;
                } else {
                    logger.pushLog(Log{std::format("WARNING: composite UNIQUE key on table '{}', column '{}' ignored", table, header)});
                }

                headers.data.push_back(info);
                continue;
            }

            // Foreign key
            const std::string fkQuery = std::format(
                "SELECT "
                "  c.confrelid::regclass AS referenced_table, "
                "  af.attname           AS referenced_column "
                "FROM pg_constraint c "
                "JOIN pg_attribute a "
                "  ON a.attrelid = c.conrelid "
                " AND a.attnum = ANY (c.conkey) "
                "JOIN pg_attribute af "
                "  ON af.attrelid = c.confrelid "
                " AND af.attnum = ANY (c.confkey) "
                "WHERE c.contype = 'f' "
                "  AND c.conrelid = '{}'::regclass "
                "  AND a.attname = '{}'",
                table, header);

            pqxx::result fkResult = transaction.tx.exec(fkQuery);
            if (!fkResult.empty()) {
                info.type = headerType::FOREIGN_KEY;
                info.referencedTable = fkResult[0]["referenced_table"].c_str();
            }

            headers.data.push_back(info);
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
                tHeadersInfo headers = getTableHeaders(tableName);
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

    void acquireTableRows(const std::string& table, const tHeadersInfo& cols) {
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
        for (const auto& col : cols.data) {
            const tHeaderVector& localHeaders = tableHeaders.data.at(table).data;
            if (std::ranges::find_if(localHeaders, [&](const tHeaderInfo& h) { return h.name == col.name; }) == localHeaders.end()) {
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
            tableRows.data.insert_or_assign(table, std::move(colCellMap));
            tableRows.ready = true;
        }
    }

    completeDbData acquireAllTablesRows() {
        logger.pushLog(Log{"ACQUIRE ALL TABLE ROWS: Waiting for tableheaders"});
        {
            std::unique_lock<std::mutex> lock(tableHeaders.mtx);
            tableHeaders.cv.wait(lock, [this] { return tableHeaders.ready; });
        }

        std::vector<std::pair<std::string, tHeadersInfo>> work;
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

    Change::chHashV applyChanges(std::vector<Change> changes, sqlAction action) {
        Change::chHashV successfulChanges;
        for (const auto& change : changes) {
            if (applySingleChange(change, action)) { successfulChanges.push_back(change.getKey()); }
        }
        return successfulChanges;
    }

    bool applySingleChange(const Change& change, sqlAction action) {
        try {
            logger.pushLog(Log{std::format("    Applying change {}", change.getKey())});
            const std::string changeQuery = change.toSQLaction(action);
            logger.pushLog(Log{changeQuery});

            transactionData transaction = getTransaction();
            pqxx::result r = transaction.tx.exec(changeQuery);
            transaction.tx.commit();
            logger.pushLog(Log{std::format("SUCCESS: Affected rows: {}", r.affected_rows())});

        } catch (std::exception const& e) {
            logger.pushLog(Log{std::format("ERROR: {}", e.what())});
            return false;
        }
        return true;
    }
};