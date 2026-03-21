#pragma once

#include "logger.hpp"

#include <atomic>
#include <map>
#include <mutex>
#include <unordered_set>

constexpr const std::size_t INVALID_ID = std::numeric_limits<std::size_t>::max();

enum class ChangeType : uint8_t { NONE, INSERT_ROW, UPDATE_CELLS, DELETE_ROW };
enum class SqlAction : uint8_t { PREVIEW, EXECUTE };

struct ImTable {
    std::string name;
    uint16_t id;
};

struct SqlQuery {
    std::string query;
    std::vector<std::string> params;
};

class Change {
  public:
    using colValMap = std::map<std::string, std::string>;
    template <class T> using chSimpleMap = std::map<T, std::size_t>;
    using chHHMap = chSimpleMap<std::size_t>;
    using chHashV = std::vector<std::size_t>;
    using chHashM = std::map<std::size_t, Change>;
    using ctPKMD = std::map<std::string, chHHMap>;
    using ctUKMD = std::map<std::string, chSimpleMap<std::string>>;

  private:
    static inline std::atomic<std::size_t> nextId_{1};
    inline static Logger* logger_ = nullptr;

    std::size_t changeKey_;

    colValMap changedCells_;
    ChangeType type_{ChangeType::UPDATE_CELLS};

    ImTable tableData_;
    std::optional<uint32_t> rowId_;

    std::vector<std::size_t> parentKeys_;
    std::vector<std::size_t> childrenKeys_;

    bool selected_{false};
    bool locallyValid_{false};
    bool valid_{false};

  public:
    Change(colValMap cCells, ChangeType cType, ImTable cTable, std::optional<std::size_t> cRowId = std::nullopt);
    static void setLogger(Logger& l);
    std::size_t getKey() const;
    ChangeType getType() const;
    const std::string& getTable() const;
    bool hasRowId() const;
    uint32_t getRowId() const;
    colValMap getCells() const;
    std::string getCell(const std::string& header) const;

    Change(const Change&) = default;
    Change& operator=(const Change&) = default;
    Change(Change&&) = default;
    Change& operator=(Change&&) = default;
    Change& operator^(const Change& other); // just for fun

    SqlQuery toSQLaction(SqlAction action) const;
    void setSelected(bool value);
    bool isSelected() const;
    void addParent(std::size_t parent);
    void setRowId(uint32_t aRowId);
    bool hasParent() const;
    std::size_t getParentCount() const;
    const std::vector<std::size_t>& getParents() const;
    void removeParent(const std::size_t key);
    void setLocalValidity(bool validity);
    void setValidity(bool validity);
    bool isLocallyValid() const;
    bool isValid() const;
    void pushChild(const Change& change);
    void removeChild(const std::size_t key);
    bool hasChildren() const;
    const std::vector<std::size_t>& getChildren() const;
    std::string getCellSummary(const uint8_t len) const;
};

struct uiChangeInfo {
    Change::ctPKMD idMappedChanges;
    Change::chHashM changes;
    std::unordered_set<std::size_t> roots;
};

namespace ChangeHelpers {
std::unique_ptr<Change> getChangeOfRow(const std::shared_ptr<uiChangeInfo>& uiChanges, const std::string& table, const std::size_t id);
} // namespace ChangeHelpers