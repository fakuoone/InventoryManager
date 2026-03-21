#include "change.hpp"

Change::Change(Change::colValMap cCells, ChangeType cType, ImTable cTable, std::optional<std::size_t> cRowId)
    : changeKey_(nextId_++), changedCells_(cCells), type_(cType), tableData_(cTable), rowId_(cRowId) {}

void Change::setLogger(Logger& l) {
    logger_ = &l;
}

std::size_t Change::getKey() const {
    return changeKey_;
};

ChangeType Change::getType() const {
    return type_;
}

const std::string& Change::getTable() const {
    return tableData_.name;
}

bool Change::hasRowId() const {
    return rowId_.has_value();
}

uint32_t Change::getRowId() const {
    return rowId_.value();
}

Change::colValMap Change::getCells() const {
    return changedCells_;
}

std::string Change::getCell(const std::string& header) const {
    if (!changedCells_.contains(header)) { return std::string(); }
    return changedCells_.at(header);
}

Change& Change::operator^(const Change& other) {
    if (this != &other) {
        for (auto const& [col, val] : other.changedCells_) {
            this->changedCells_[col] = val;
            logger_->pushLog(Log{std::format("            change now has column: {} with cell value: {}", col, val)});
        }
    }
    if (logger_) { logger_->pushLog(Log{std::format("^^ operator")}); }

    return *this;
}

SqlQuery Change::toSQLaction(SqlAction action) const {
    SqlQuery result;
    switch (type_) {
    case ChangeType::DELETE_ROW:
        result.query = std::format("DELETE FROM {} WHERE id = $1", tableData_.name);
        result.params.push_back(std::to_string(rowId_.value()));
        break;

    case ChangeType::INSERT_ROW: {
        std::string columnNames;
        std::string placeholders;
        bool first = true;
        int paramIndex = 1;

        for (const auto& [col, val] : changedCells_) {
            if (col.empty() || val.empty()) continue;
            if (!first) {
                columnNames += ", ";
                placeholders += ", ";
            }

            first = false;
            columnNames += col;
            placeholders += std::format("${}", paramIndex++);
            result.params.push_back(val);
        }

        result.query = std::format("INSERT INTO {} ({}) VALUES ({});", tableData_.name, columnNames, placeholders);
        break;
    }

    case ChangeType::UPDATE_CELLS: {
        std::string pairs;
        bool first = true;
        int paramIndex = 1;
        for (const auto& [col, val] : changedCells_) {
            if (!first) pairs += ", ";
            first = false;
            pairs += std::format("{} = ${}", col, paramIndex++);
            result.params.push_back(val);
        }

        result.query = std::format("UPDATE {} SET {} WHERE id = ${};", tableData_.name, pairs, paramIndex);
        result.params.push_back(std::to_string(rowId_.value()));
        break;
    }
    default:
        break;
    }

    return result;
}

void Change::setSelected(bool value) {
    selected_ = value;
}

bool Change::isSelected() const {
    return selected_;
}

void Change::addParent(std::size_t parent) {
    parentKeys_.push_back(parent);
}

void Change::setRowId(uint32_t aRowId) {
    rowId_ = aRowId;
}

bool Change::hasParent() const {
    return parentKeys_.size() != 0;
}

std::size_t Change::getParentCount() const {
    return parentKeys_.size();
}

const std::vector<std::size_t>& Change::getParents() const {
    return parentKeys_;
}

void Change::removeParent(const std::size_t key) {
    auto it = std::find(parentKeys_.begin(), parentKeys_.end(), key);
    if (it != parentKeys_.end()) { parentKeys_.erase(it); }
}

void Change::setLocalValidity(bool validity) {
    locallyValid_ = validity;
    if (!hasChildren()) { setValidity(validity); }
}

void Change::setValidity(bool validity) {
    if (validity) { locallyValid_ = validity; }
    valid_ = validity;
}

bool Change::isLocallyValid() const {
    return locallyValid_;
}

bool Change::isValid() const {
    return valid_;
}

void Change::pushChild(const Change& change) {
    childrenKeys_.push_back(change.getKey());
}

void Change::removeChild(const std::size_t key) {
    auto it = std::find(childrenKeys_.begin(), childrenKeys_.end(), key);
    if (it != childrenKeys_.end()) { childrenKeys_.erase(it); }
}

bool Change::hasChildren() const {
    return childrenKeys_.size() != 0;
}

const std::vector<std::size_t>& Change::getChildren() const {
    return childrenKeys_;
}

std::string Change::getCellSummary(const uint8_t len) const {
    std::string summary;
    std::string concat = selected_ ? "\n" : ",";
    for (const auto& [col, val] : changedCells_) {
        if (!summary.empty()) { summary += concat; }
        summary += std::format("{}={}", col, val);
        if (summary.size() >= len && !selected_) {
            summary.resize(len - 3);
            summary += "...";
        }
    }
    return summary;
}

std::unique_ptr<Change>
ChangeHelpers::getChangeOfRow(const std::shared_ptr<uiChangeInfo>& uiChanges, const std::string& table, const std::size_t id) {
    if (!uiChanges->idMappedChanges.contains(table)) { return nullptr; }
    if (id == INVALID_ID) { return nullptr; }
    if (uiChanges->idMappedChanges.at(table).contains(id)) {
        const std::size_t changeKey = uiChanges->idMappedChanges.at(table).at(id);
        return std::make_unique<Change>(uiChanges->changes.at(changeKey));
    }
    return nullptr;
}
