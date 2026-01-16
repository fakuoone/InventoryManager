#pragma once

#include "logger.hpp"

#include <map>
#include <mutex>
#include <string>
#include <map>
#include <vector>

enum class changeType : uint8_t { INSERT_ROW, DELETE_ROW, UPDATE_CELLS };

enum class sqlAction : uint8_t { PREVIEW, EXECUTE };

struct table {
    std::string name;
    uint16_t id;
};

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
    using ctPKMD = std::map<std::string, chHHMap>;

   private:
    colValMap changedCells;
    changeType type{changeType::UPDATE_CELLS};
    table tableData;
    inline static Logger* logger = nullptr;
    uint32_t rowId{0};
    std::size_t changeKey{0};
    std::size_t parentKey{0};
    bool selected{false};

   public:
    Change(colValMap cCells, changeType cType, table cTable, std::size_t cRowId) : changedCells(cCells), type(cType), tableData(cTable), rowId(cRowId) { updateKey(); }

    static void setLogger(Logger& l) { logger = &l; }

    [[nodiscard]] std::size_t getKey() const { return (std::size_t(type) << 56) | (std::size_t(tableData.id) << 32) | std::size_t(rowId); }

    changeType getType() const { return type; }

    const std::string& getTable() const { return tableData.name; }

    std::size_t getRowId() const { return rowId; }

    colValMap getCells() const { return changedCells; }

    std::string getCell(const std::string& header) const {
        if (!changedCells.contains(header)) { return std::string(); }
        return changedCells.at(header);
    }

    void updateKey() { changeKey = getKey(); }

    Change(const Change&) = default;
    Change& operator=(const Change& other) = default;

    Change& operator^(const Change& other) {
        // merges changs if necessary
        if (this != &other) {
            for (auto const& [col, val] : other.changedCells) {
                // TODO: Validate existence of col? Validate val?
                this->changedCells[col] = val;
                logger->pushLog(Log{std::format("            change now has column: {} with cell value: {}", col, val)});
            }
            this->updateKey();
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
                sqlString = std::format("DELETE FROM {} WHERE {} = {}", tableData.name, "id", rowId);
                break;
            case changeType::INSERT_ROW: {
                // TODO: Tabellenkopf und Zellwerte aus change bauen
                std::string columnNames;
                std::string cellValues;
                sqlString = std::format("INSERT INTO {} ({}) VALUES ({});", tableData.name, columnNames, cellValues);
                break;
            }
            case changeType::UPDATE_CELLS: {
                std::string columnValuePairs;
                sqlString = std::format("UPDATE {} SET {} WHERE id = {};", tableData.name, columnValuePairs, rowId);
                break;
            }
            default:
                break;
        }

        return sqlString;
    }

    void toggleSelect() { selected = !selected; }

    bool isSelected() const { return selected; }

    void setParent(std::size_t parent) { parentKey = parent; }

    bool hasParent() const { return parentKey != 0; }

    std::size_t getParent() const { return parentKey; }
};