#pragma once

#include "logger.hpp"

#include <map>
#include <mutex>
#include <string>
#include <map>
#include <vector>
#include <atomic>

#include <optional>
#include <cstdint>

enum class changeType : uint8_t { NONE, INSERT_ROW, UPDATE_CELLS, DELETE_ROW };

enum class sqlAction : uint8_t { PREVIEW, EXECUTE };

struct imTable {
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
    template <class T>
    using chSimpleMap = std::map<T, std::size_t>;
    using chHHMap = chSimpleMap<std::size_t>;
    using chHashV = std::vector<std::size_t>;
    using chHashM = std::map<std::size_t, Change>;
    using ctPKMD = std::map<std::string, chHHMap>;
    using ctUKMD = std::map<std::string, chSimpleMap<std::string>>;

   private:
    static inline std::atomic<std::size_t> nextId{1};
    inline static Logger* logger = nullptr;

    std::size_t changeKey;

    colValMap changedCells;
    changeType type{changeType::UPDATE_CELLS};

    imTable tableData;
    std::optional<uint32_t> rowId;

    std::vector<std::size_t> parentKeys;
    std::vector<std::size_t> childrenKeys;

    bool selected{false};
    bool locallyValid{false};
    bool valid{false};

   public:
    Change(colValMap cCells, changeType cType, imTable cTable, std::optional<std::size_t> cRowId = std::nullopt) : changeKey(nextId++), changedCells(cCells), type(cType), tableData(cTable), rowId(cRowId) {}

    static void setLogger(Logger& l) { logger = &l; }

    [[nodiscard]] std::size_t getKey() const { return changeKey; };

    changeType getType() const { return type; }

    const std::string& getTable() const { return tableData.name; }

    bool hasRowId() const { return rowId.has_value(); }

    uint32_t getRowId() const { return rowId.value(); }

    colValMap getCells() const { return changedCells; }

    std::string getCell(const std::string& header) const {
        if (!changedCells.contains(header)) { return std::string(); }
        return changedCells.at(header);
    }

    Change(const Change& other) = default;
    Change& operator=(const Change& other) = default;
    Change(Change&& other) = default;
    Change& operator=(Change&& other) = default;

    Change& operator^(const Change& other) {
        if (this != &other) {
            for (auto const& [col, val] : other.changedCells) {
                this->changedCells[col] = val;
                logger->pushLog(Log{std::format("            change now has column: {} with cell value: {}", col, val)});
            }
            // this->parentKey = other.getParent();
            // this->selected = other.isSelected();
        }
        if (logger) { logger->pushLog(Log{std::format("^^ operator")}); }

        return *this;
    }

    std::string toSQLaction(sqlAction action) const {
        // TODO: INSERT und UPDATE sind anfällig für SQL Injektion. Beheben
        std::string sqlString;

        switch (type) {
            case changeType::DELETE_ROW:
                sqlString = std::format("DELETE FROM {} WHERE {} = {}", tableData.name, "id", rowId.value());
                break;
            case changeType::INSERT_ROW: {
                std::string columnNames;
                std::string cellValues;
                sqlString = std::format("INSERT INTO {} ({}) VALUES ({});", tableData.name, columnNames, cellValues);
                break;
            }
            case changeType::UPDATE_CELLS: {
                std::string columnValuePairs;
                sqlString = std::format("UPDATE {} SET {} WHERE id = {};", tableData.name, columnValuePairs, rowId.value());
                break;
            }
            default:
                break;
        }

        return sqlString;
    }

    void setSelected(bool value) { selected = value; }

    bool isSelected() const { return selected; }

    void addParent(std::size_t parent) { parentKeys.push_back(parent); }

    void setRowId(uint32_t aRowId) { rowId = aRowId; }

    bool hasParent() const { return parentKeys.size() != 0; }

    std::size_t getParentCount() const { return parentKeys.size(); }

    const std::vector<std::size_t>& getParents() const { return parentKeys; }

    void removeParent(const std::size_t key) {
        auto it = std::find(parentKeys.begin(), parentKeys.end(), key);
        if (it != parentKeys.end()) { parentKeys.erase(it); }
    }

    void setLocalValidity(bool validity) {
        locallyValid = validity;
        if (!hasChildren()) { setValidity(validity); }
    }

    void setValidity(bool validity) {
        if (validity) { locallyValid = validity; }
        valid = validity;
    }

    bool isLocallyValid() const { return locallyValid; }

    bool isValid() const { return valid; }

    void pushChild(const Change& change) { childrenKeys.push_back(change.getKey()); }

    void removeChild(const std::size_t key) {
        auto it = std::find(childrenKeys.begin(), childrenKeys.end(), key);
        if (it != childrenKeys.end()) { childrenKeys.erase(it); }
    }

    bool hasChildren() const { return childrenKeys.size() != 0; }

    const std::vector<std::size_t>& getChildren() const { return childrenKeys; }

    std::string getCellSummary(const uint8_t len) const {
        std::string summary;
        std::string concat = selected ? "\n" : ",";
        for (const auto& [col, val] : changedCells) {
            if (!summary.empty()) { summary += concat; }
            summary += std::format("{}={}", col, val);
            if (summary.size() >= len && !selected) {
                summary.resize(len - 3);
                summary += "...";
            }
        }
        return summary;
    }
};