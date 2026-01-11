#pragma once

#include "logger.hpp"

#include <map>
#include <mutex>
#include <string>
#include <map>
#include <vector>

enum class changeType { INSERT_ROW, DELETE_ROW, UPDATE_CELLS };

enum class sqlAction { PREVIEW, EXECUTE };

class Change {
    /* 
    1. create table (not supported) 
    2. delete table (not supported) 
    3. add row to any table (how to say, which columns need to be specified?) 
    4. remove row from any table 
    5. change n cells in a row 
    */
   public:
    using colValMap = std::map<std::string, std::string>;
    using chHHMap = std::map<std::size_t, std::size_t>;
    using chHashV = std::vector<std::size_t>;
    using chHashM = std::map<std::size_t, Change>;
    using ctRMD = std::map<std::string, chHHMap>;

   private:
    colValMap changedCells;
    changeType type{changeType::UPDATE_CELLS};
    std::string table;
    Logger& logger;
    std::size_t rowId;
    std::size_t changeHash;

    static inline void combineHash(std::size_t& hash, std::size_t value) { hash ^= value + 0x9e3779b9 + (hash << 6) + (hash >> 2); }

   public:
    Change(colValMap cCells, changeType cType, std::string cTable, Logger& cLogger, std::size_t cRowId) : changedCells(cCells), type(cType), table(cTable), logger(cLogger), rowId(cRowId) { updateHash(); }

    [[nodiscard]] std::size_t getHash() const {
        //TODO: Es ist unmöglich, in einer Tabelle zwei mal iNSERT_ROw zu haben
        // was ist mit cash collisions?
        std::size_t currentHash = 0;
        combineHash(currentHash, std::hash<int>{}(static_cast<int>(type)));
        combineHash(currentHash, std::hash<std::string>{}(table));
        combineHash(currentHash, std::hash<std::size_t>{}(rowId));
        return currentHash;
    }

    changeType getType() const { return type; }

    const std::string& getTable() const { return table; }

    std::size_t getRowId() const { return rowId; }

    colValMap getCells() const { return changedCells; }

    std::string getCell(const std::string& header) const {
        if (!changedCells.contains(header)) { return std::string(); }
        return changedCells.at(header);
    }

    void updateHash() { changeHash = getHash(); }

    Change(const Change&) = default;
    Change& operator=(const Change& other) {
        // merges changs if necessary
        if (this != &other) {
            for (auto const& [col, val] : other.changedCells) {
                // TODO: Validate existence of col? Validate val?
                this->changedCells[col] = val;
                logger.pushLog(Log{std::format("            change now has column: {} with cell value: {}", col, val)});
            }
            this->updateHash();
        }
        return *this;
    }

    std::string toSQLaction(sqlAction action) const {
        // TODO: Basierend auf action entweder eine Vorschau präsentieren, oder exekutieren
        // TODO: INSERT und UPDATE sind anfällig für SQL Injektion. Beheben
        std::string sqlString;

        switch (type) {
            case changeType::DELETE_ROW:
                // TODO: Wie komme ich an den Spaltennamen von der id-Spalte (muss es sie geben?)
                sqlString = std::format("DELETE FROM {} WHERE {} = {}", table, "id", rowId);
                break;
            case changeType::INSERT_ROW: {
                // TODO: Tabellenkopf und Zellwerte aus change bauen
                std::string columnNames;
                std::string cellValues;
                sqlString = std::format("INSERT INTO {} ({}) VALUES ({});", table, columnNames, cellValues);
                break;
            }
            case changeType::UPDATE_CELLS: {
                std::string columnValuePairs;
                sqlString = std::format("UPDATE {} SET {} WHERE id = {};", table, columnValuePairs, rowId);
                break;
            }
            default:
                break;
        }

        return sqlString;
    }
};