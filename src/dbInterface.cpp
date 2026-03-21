#include "dbInterface.hpp"

TransactionData DbInterface::getTransaction() {
    std::unique_lock lock(connData_.mtx);
    connData_.cv.wait(lock, [this] { return connData_.connStringValid; });
    return TransactionData(connData_.connString);
}

DbInterface::DbInterface(Logger& cLogger) : logger_(cLogger) {}

void DbInterface::initializeWithConfigString(const std::string& confString) {
    {
        std::lock_guard lock(connData_.mtx);
        connData_.connString = confString;
        connData_.connStringValid = !confString.empty();
    }
    connData_.cv.notify_all(); // wake all waiting DB threads
}

void DbInterface::acquireTables() {
    try {
        TransactionData transaction = getTransaction();
        const std::string tableQuery = "SELECT table_name FROM information_schema.tables WHERE table_schema='public'";
        auto result = transaction.tx.query<std::string>(tableQuery);
        // logger.pushLog(Log{tableQuery});
        {
            tables_.data.clear();
            std::lock_guard<std::mutex> lg{tables_.mtx};
            for (const auto& [tableName] : result) {
                tables_.data.push_back(tableName);
                // logger.pushLog(Log{std::format("    table: {}", tableName)});
            }
            transaction.tx.commit();
            tables_.ready = true;
        }
        tables_.cv.notify_one();
    } catch (std::exception const& e) {
        logger_.pushLog(Log{std::format("ERROR: {}", e.what())});
        return;
    }
}

HeadersInfo DbInterface::getTableHeaders(const std::string& table) {
    TransactionData transaction = getTransaction();
    const std::string headerQuery = std::format("    SELECT * FROM {} WHERE 1=0", table);
    // logger.pushLog(Log{headerQuery});
    pqxx::result r = transaction.tx.exec(headerQuery);
    transaction.tx.commit();

    std::vector<std::string> headers;
    headers.reserve(static_cast<std::size_t>(r.columns()));

    for (pqxx::row::size_type i = 0; i < r.columns(); ++i) {
        headers.emplace_back(r.column_name(i));
        // logger.pushLog(Log{std::format("        column: {}", r.column_name(i))});
    }

    return getHeaderInfo(table, std::move(headers));
}

HeadersInfo DbInterface::getHeaderInfo(const std::string& table, std::vector<std::string> rawHeaders) {
    HeadersInfo headers;
    TransactionData transaction = getTransaction();

    for (const std::string& header : rawHeaders) {
        HeaderInfo info{header, "", DB::HeaderTypes::DATA, DB::DataType::UNKNOWN, 0, true};

        // Nullable
        const std::string nullQuery = std::format("SELECT NOT a.attnotnull AS is_nullable "
                                                  "FROM pg_attribute a "
                                                  "JOIN pg_class c ON c.oid = a.attrelid "
                                                  "WHERE c.relname = '{}' "
                                                  "  AND a.attname = '{}' "
                                                  "  AND a.attnum > 0 "
                                                  "  AND NOT a.attisdropped",
                                                  table,
                                                  header);

        pqxx::result nullResult = transaction.tx.exec(nullQuery);
        if (!nullResult.empty()) { info.nullable = nullResult[0]["is_nullable"].as<bool>(); }

        // Primary key
        const std::string pkQuery = std::format("SELECT 1 "
                                                "FROM pg_constraint c "
                                                "JOIN pg_attribute a "
                                                "  ON a.attrelid = c.conrelid "
                                                " AND a.attnum = ANY (c.conkey) "
                                                "WHERE c.contype = 'p' "
                                                "  AND c.conrelid = '{}'::regclass "
                                                "  AND a.attname = '{}'",
                                                table,
                                                header);

        pqxx::result pkResult = transaction.tx.exec(pkQuery);

        if (!pkResult.empty()) {
            info.type = DB::HeaderTypes::PRIMARY_KEY;
            headers.data.push_back(info);
            headers.pkey = header;
            continue;
        }

        // UNIQUE (single-column only)
        const std::string uqQuery = std::format("SELECT array_length(c.conkey, 1) AS key_len "
                                                "FROM pg_constraint c "
                                                "JOIN pg_attribute a "
                                                "  ON a.attrelid = c.conrelid "
                                                " AND a.attnum = ANY (c.conkey) "
                                                "WHERE c.contype = 'u' "
                                                "  AND c.conrelid = '{}'::regclass "
                                                "  AND a.attname = '{}'",
                                                table,
                                                header);

        pqxx::result uqResult = transaction.tx.exec(uqQuery);
        if (!uqResult.empty()) {
            int keyLen = uqResult[0]["key_len"].as<int>();
            if (keyLen == 1) {
                info.type = DB::HeaderTypes::UNIQUE_KEY;
                headers.uKeyName = header;
            } else {
                logger_.pushLog(Log{std::format("WARNING: composite UNIQUE key on table '{}', column '{}' ignored", table, header)});
            }
        }

        // Foreign key
        const std::string fkQuery = std::format("SELECT "
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
                                                table,
                                                header);

        pqxx::result fkResult = transaction.tx.exec(fkQuery);
        if (!fkResult.empty()) {
            // allow foreign key that is also unique key (.referencedTable)
            if (info.type != DB::HeaderTypes::UNIQUE_KEY) { info.type = DB::HeaderTypes::FOREIGN_KEY; }
            info.referencedTable = fkResult[0]["referenced_table"].c_str();
        }

        // Column data type
        const std::string typeQuery = std::format("SELECT format_type(a.atttypid, a.atttypmod) AS data_type "
                                                  "FROM pg_attribute a "
                                                  "WHERE a.attrelid = '{}'::regclass "
                                                  "  AND a.attname = '{}' "
                                                  "  AND a.attnum > 0 "
                                                  "  AND NOT a.attisdropped",
                                                  table,
                                                  header);

        pqxx::result typeResult = transaction.tx.exec(typeQuery);

        if (!typeResult.empty()) { info.dataType = DB::toDbType(typeResult[0]["data_type"].c_str()); }

        headers.data.push_back(info);
    }
    transaction.tx.commit();
    return headers;
}

std::size_t DbInterface::computeDepth(HeaderInfo& header) {
    // Already computed
    if (header.depth != 0) return header.depth;

    // Base case
    if (header.referencedTable.empty()) {
        header.depth = 0;
        return 0;
    }

    // self reference
    HeadersInfo& referencedHeaders = tableHeaders_.data.at(header.referencedTable);
    if (std::find_if(referencedHeaders.data.begin(), referencedHeaders.data.end(), [&](HeaderInfo& tH) {
            return tH.referencedTable == header.referencedTable;
        }) != referencedHeaders.data.end()) {
        header.depth = 1;
        return 1;
    }

    // go to referenced table
    std::size_t maxReferencedDepth = 0;
    for (HeaderInfo& refHeader : referencedHeaders.data) {
        maxReferencedDepth = std::max(maxReferencedDepth, computeDepth(refHeader));
    }

    header.depth = 1 + maxReferencedDepth;
    return header.depth;
}

void DbInterface::assignDependencyIndexes() {
    for (auto& [tableName, headers] : tableHeaders_.data) {
        for (HeaderInfo& header : headers.data) {
            computeDepth(header);
        }
    }
    for (auto& [tableName, headers] : tableHeaders_.data) {
        std::size_t tableDepth = 0;
        for (const auto& header : headers.data) {
            tableDepth = std::max(tableDepth, header.depth);
        }
        tableHeaders_.data[tableName].maxDepth = tableDepth;
    }
}

void DbInterface::acquireTableContent() {
    {
        std::unique_lock<std::mutex> lockTable(tables_.mtx);
        // logger.pushLog(Log{"ACQUIRE TABLE CONTENT: Waiting for tables"});
        tables_.cv.wait(lockTable, [this] { return tables_.ready; });
    }
    {
        std::lock_guard<std::mutex> lgHeaders{tableHeaders_.mtx};
        tableHeaders_.data.clear();
    }

    // get headers
    // logger.pushLog(Log{"ACQUIRE TABLE CONTENT: Preparing headerquery"});
    for (const std::string& tableName : tables_.data) {
        try {
            HeadersInfo headers = getTableHeaders(tableName);
            {
                std::lock_guard<std::mutex> lgHeaders{tableHeaders_.mtx};
                std::lock_guard<std::mutex> lgTables{tables_.mtx};
                tableHeaders_.data.emplace(tableName, std::move(headers));
            }
        } catch (std::exception const& e) {
            logger_.pushLog(Log{std::format("ERROR: {}", e.what())});
            return;
        }
    }
    {
        std::lock_guard<std::mutex> lgHeaders{tableHeaders_.mtx};
        std::lock_guard<std::mutex> lgTables{tables_.mtx};
        assignDependencyIndexes();
        tableHeaders_.ready = true;
        tables_.ready = false;
    }
    tableHeaders_.cv.notify_one();
}

void DbInterface::acquireTableRows(const std::string& table, const HeadersInfo& cols) {
    {
        // logger.pushLog(Log{"ACQUIRE TABLE ROWS: Waiting for tableheaders"});
        std::unique_lock<std::mutex> lockTableHeaders(tableHeaders_.mtx);
        tableHeaders_.cv.wait(lockTableHeaders, [this] { return tableHeaders_.ready; });
    }
    {
        std::lock_guard<std::mutex> lgTables{tables_.mtx};
        if (std::find(tables_.data.begin(), tables_.data.end(), table) == tables_.data.end()) {
            logger_.pushLog(Log{std::format("ERROR: Acquiring rows: Table {} is unknown.", table)});
            return;
        }
    }
    std::map<std::string, std::vector<std::string>> colCellMap;
    // logger.pushLog(Log{"ACQUIRE TABLE ROWS: Preparing headerqueries"});

    std::lock_guard<std::mutex> lgTableHeaders(tableHeaders_.mtx);
    for (const auto& col : cols.data) {
        const HeaderVector& localHeaders = tableHeaders_.data.at(table).data;
        if (std::ranges::find_if(localHeaders, [&](const HeaderInfo& h) { return h.name == col.name; }) == localHeaders.end()) {
            logger_.pushLog(Log{std::format("ERROR: Acquiring rows: Header {} for table {} is unknown.", col.name, table)});
            return;
        }
        try {
            const std::string headerQuery = std::format("    SELECT {} FROM {}", col.name, table);
            // logger.pushLog(Log{headerQuery});

            TransactionData transaction = getTransaction();
            pqxx::result r = transaction.tx.exec(headerQuery);
            transaction.tx.commit();

            std::vector<std::string> cells;
            cells.reserve(static_cast<std::size_t>(r.columns()));

            for (const pqxx::row& row : r) {
                cells.emplace_back(row[col.name].c_str());
                // logger.pushLog(Log{std::format("        {}: {}", col.name, row[col.name].c_str())});
            }
            colCellMap.emplace(col.name, cells);

        } catch (std::exception const& e) {
            logger_.pushLog(Log{std::format("ERROR: {}", e.what())});
            return;
        }
    }
    {
        std::lock_guard<std::mutex> lgTableRows{tableRows_.mtx};
        tableRows_.data.insert_or_assign(table, std::move(colCellMap));
        tableRows_.ready = true;
    }
}

CompleteDbData DbInterface::acquireAllTablesRows() {
    // logger.pushLog(Log{"ACQUIRE ALL TABLE ROWS: Waiting for tableheaders"});
    {
        std::unique_lock<std::mutex> lock(tableHeaders_.mtx);
        tableHeaders_.cv.wait(lock, [this] { return tableHeaders_.ready; });
    }

    std::vector<std::pair<std::string, HeadersInfo>> work;
    {
        std::lock_guard<std::mutex> lockTables(tables_.mtx);
        std::lock_guard<std::mutex> lockTableHeaders(tableHeaders_.mtx);

        for (const auto& table : tables_.data) {
            work.emplace_back(table, tableHeaders_.data.at(table));
        }
    }
    for (const auto& [table, headers] : work) {
        acquireTableRows(table, headers);
    }
    return CompleteDbData{tables_.data, tableHeaders_.data, tableRows_.data, std::map<std::string, std::size_t>{}};
}

Change::chHashV DbInterface::applyChanges(std::vector<Change> changes, SqlAction action) {
    Change::chHashV successfulChanges;
    for (const auto& change : changes) {
        if (applySingleChange(change, action)) { successfulChanges.push_back(change.getKey()); }
    }
    return successfulChanges;
}

bool DbInterface::applySingleChange(const Change& change, SqlAction action) {
    try {
        SqlQuery changeQuery = change.toSQLaction(action);
        logger_.pushLog(Log{std::format("    Applying change {}", change.getKey())});
        logger_.pushLog(Log{changeQuery.query});
        TransactionData transaction = getTransaction();
        pqxx::params p;
        for (const auto& v : changeQuery.params) {
            p.append(v);
        }
        pqxx::result r = transaction.tx.exec(changeQuery.query, p);
        transaction.tx.commit();
        logger_.pushLog(Log{std::format("SUCCESS: Affected rows: {}", r.affected_rows())});
    } catch (std::exception const& e) {
        logger_.pushLog(Log{std::format("ERROR: {}", e.what())});
        return false;
    }

    return true;
}