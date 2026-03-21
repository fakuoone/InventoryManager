#pragma once

#include "change.hpp"
#include "dataTypes.hpp"
#include "logger.hpp"

#include <algorithm>
#include <iostream>
#include <mutex>

#include <pqxx/pqxx>

struct TransactionData {
    pqxx::connection conn;
    pqxx::work tx;

    TransactionData(const std::string cConnString) : conn(cConnString), tx(conn) {}

    TransactionData(const TransactionData&) = delete;
    TransactionData& operator=(TransactionData& other) = delete;
};

struct HeaderInfo {
    std::string name;
    std::string referencedTable;
    DB::HeaderTypes type;
    DB::DataType dataType;
    std::size_t depth = 0;
    bool nullable = true;
};

using HeaderVector = std::vector<HeaderInfo>;
struct HeadersInfo {
    HeaderVector data;
    std::string pkey;     // ID
    std::string uKeyName; // NAME
    std::size_t maxDepth = 0;
};

using StringVector = std::vector<std::string>;
using HeaderMap = std::map<std::string, HeadersInfo>;
using ColumnDataMap = std::map<std::string, StringVector>;
using RowMap = std::map<std::string, ColumnDataMap>;

struct CompleteDbData {
    StringVector tables;
    HeaderMap headers;
    RowMap tableRows;
    std::map<std::string, std::size_t> maxPKeys;
};

struct ProtectedConnData {
    std::string connString;
    bool connStringValid{false};
    std::mutex mtx;
    std::condition_variable cv;
};

class DbInterface {
  private:
    DB::ProtectedData<StringVector> tables_;
    DB::ProtectedData<HeaderMap> tableHeaders_;
    DB::ProtectedData<RowMap> tableRows_;
    Logger& logger_;

    ProtectedConnData connData_;
    TransactionData getTransaction();
    HeadersInfo getTableHeaders(const std::string& table);
    HeadersInfo getHeaderInfo(const std::string& table, std::vector<std::string> rawHeaders);
    std::size_t computeDepth(HeaderInfo& header);
    void assignDependencyIndexes();
    void acquireTableRows(const std::string& table, const HeadersInfo& cols);
    bool applySingleChange(const Change& change, SqlAction action);

  public:
    DbInterface(Logger& cLogger);
    void initializeWithConfigString(const std::string& confString);
    void acquireTables();
    void acquireTableContent();
    CompleteDbData acquireAllTablesRows();
    Change::chHashV applyChanges(std::vector<Change> changes, SqlAction action);
};