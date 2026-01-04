#pragma once

#include "logger.hpp"

#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

enum class changeType { InsertRow, DeleteRow, UpdateCells };

template <typename rowIdType = int>
class Change {
    /* 
    1. create table (not supported) 
    2. delete table (not supported) 
    3. add row to any table (how to say, which columns need to be specified?) 
    4. remove row from any table 
    5. change n cells in a row 
    */
   private:
    changeType type{changeType::UpdateCells};
    std::string table;
    rowIdType rowId{};
    std::unordered_map<std::string, std::string> changedCells;
    std::size_t changeHash;
    Logger& logger;

    static inline void combineHash(std::size_t& hash, std::size_t value) { hash ^= value + 0x9e3779b9 + (hash << 6) + (hash >> 2); }

   public:
    Change(changeType cType, std::string cTable, rowIdType cRowId, std::unordered_map<std::string, std::string> cCells, Logger& cLogger) : type(cType), table(cTable), rowId(cRowId), changedCells(cCells), logger(cLogger) { updateHash(); }

    [[nodiscard]] std::size_t getHash() const {
        std::size_t currentHash = 0;
        combineHash(currentHash, std::hash<int>{}(static_cast<int>(type)));
        combineHash(currentHash, std::hash<std::string>{}(table));
        combineHash(currentHash, std::hash<rowIdType>{}(rowId));
        return currentHash;
    }

    changeType getType() const { return type; }

    void updateHash() { changeHash = getHash(); }

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
};