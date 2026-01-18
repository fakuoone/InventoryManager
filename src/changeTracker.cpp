#include "changeTracker.hpp"

void ChangeTracker::mergeCellChanges(Change& existingChange, const Change& newChange) {
    logger.pushLog(Log{std::format("        Merging cell changes {} and {}", existingChange.getKey(), newChange.getKey())});
    existingChange ^ newChange;
}

void ChangeTracker::freeze() {
    std::unique_lock lock(freezeMtx);
    frozen.store(true, std::memory_order_release);
}

void ChangeTracker::unfreeze() {
    {
        std::unique_lock lock(freezeMtx);
        frozen.store(false, std::memory_order_release);
    }
    freezeCv.notify_all();
}

void ChangeTracker::waitIfFrozen() {
    std::unique_lock lock(freezeMtx);
    freezeCv.wait(lock, [this] { return !frozen.load(std::memory_order_acquire); });
}

const Change ChangeTracker::getChange(const std::size_t key) {
    std::lock_guard<std::mutex> lg(changes.mtx);
    assert(changes.flatData.contains(key));
    if (changes.flatData.contains(key)) { return changes.flatData.at(key); }
    return changes.flatData.at(key);  // TODO: fix
}

bool ChangeTracker::isConflicting(const Change& newChange) {
    // This function assumes, that the changes are already in pKeyMappedData and flatData. Is this true?
    if (!newChange.hasRowId() || newChange.getType() == changeType::INSERT_ROW) { return false; }
    const std::string& table = newChange.getTable();
    const uint32_t rowId = newChange.getRowId();
    if (!changes.pKeyMappedData.contains(table)) { return false; }
    if (!changes.pKeyMappedData.at(table).contains(rowId)) { return false; }
    return true;
}

Change& ChangeTracker::manageConflictL(Change& newChange) {
    // TODO: fix after switching to unique keys
    // This function assumes, that the changes are already in pKeyMappedData and flatData. Is this true?
    if (!isConflicting(newChange)) { return newChange; }
    const std::string& table = newChange.getTable();
    const uint32_t rowId = newChange.getRowId();
    Change& existingChange = changes.flatData.at(changes.pKeyMappedData.at(table).at(rowId));
    switch (existingChange.getType()) {
        case changeType::DELETE_ROW:
            return existingChange;
        case changeType::INSERT_ROW:
        case changeType::UPDATE_CELLS:
            mergeCellChanges(existingChange, newChange);
            dbService.validateChange(existingChange, false);
            return existingChange;
        default:
            break;
    }
    return existingChange;
}

void ChangeTracker::propagateValidity(Change& change) {
    if (change.hasChildren()) {
        bool childSum = true;
        for (const std::size_t& childKey : change.getChildren()) {
            childSum &= changes.flatData.at(childKey).isValid();
        }
        change.setValidity(childSum);
    }
    if (change.hasParent()) {
        if (changes.flatData.contains(change.getParent())) { propagateValidity(changes.flatData.at(change.getParent())); }
    }
}

bool ChangeTracker::addChange(Change change, std::optional<uint32_t> existingRowId) {
    if (!dbService.validateChange(change, false)) { return false; }

    std::vector<Change> allChanges;
    if (change.getType() == changeType::UPDATE_CELLS) {
        assert(existingRowId.has_value());
        change.setRowId(*existingRowId);
    }

    waitIfFrozen();
    {
        std::lock_guard<std::mutex> lg(changes.mtx);
        change = manageConflictL(change);
        collectRequiredChanges(change, allChanges);
    }
    allocateIds(allChanges);

    std::lock_guard<std::mutex> lg(changes.mtx);
    for (Change& c : allChanges) {
        c = manageConflictL(c);
        propagateValidity(c);
        if (!addChangeInternalL(c)) { return false; }
    }

    return true;
}

void ChangeTracker::collectRequiredChanges(Change& change, std::vector<Change>& out) {
    std::vector<Change> required = dbService.getRequiredChanges(change, changes.maxPKeys);
    if (required.size() == 0) { releaseAllDependancies(change); }
    for (Change& r : required) {
        if (!dbService.validateChange(r, true)) { return; }
        if (findRequiredAlreadyExists(r)) { continue; }
        releaseDependancy(change, r);
        change.pushChild(r);
        collectRequiredChanges(r, out);
    }
    out.push_back(change);
}

bool ChangeTracker::findRequiredAlreadyExists(const Change& rChange) {
    // finds, if a change with the same resulting table ukey-value exists (by looping over all changes of a table, which i didnt want)
    if (!changes.pKeyMappedData.contains(rChange.getTable())) { return false; }
    const std::string uKeyOfTable = dbService.getTableUKey(rChange.getTable());
    const Change::colValMap& rChanges = rChange.getCells();
    for (const auto& [_, changeKey] : changes.pKeyMappedData.at(rChange.getTable())) {
        const Change& change = changes.flatData.at(changeKey);
        for (const auto& [col, val] : change.getCells()) {
            if (col != uKeyOfTable) {
                continue;
            } else {
                if (!rChanges.contains(col)) { return false; }
                if (rChanges.at(col) == val) { return true; }
            }
        }
    }
    return false;
}

void ChangeTracker::releaseAllDependancies(Change& change) {
    for (const std::size_t& childKey : change.getChildren()) {
        Change& child = changes.flatData.at(childKey);
        change.removeChild(childKey);
        child.resetParent();
    }
}

void ChangeTracker::releaseDependancy(Change& change, const Change& rC) {
    for (const auto& [col, val] : change.getCells()) {
        tHeaderInfo headerInfoChange = dbService.getTableHeaderInfo(change.getTable(), col);
        for (const std::size_t& childKey : change.getChildren()) {
            if (!changes.flatData.contains(childKey)) { continue; }  // not created yet
            Change& child = changes.flatData.at(childKey);
            const std::string childTableUKey = dbService.getTableUKey(child.getTable());
            if (headerInfoChange.referencedTable == child.getTable()) {
                for (const auto& [colC, valC] : child.getCells()) {
                    if (val != valC && colC == childTableUKey) {
                        change.removeChild(childKey);
                        child.resetParent();
                        break;
                    }
                }
            }
        }
    }
}

void ChangeTracker::allocateIds(std::vector<Change>& allChanges) {
    for (Change& c : allChanges) {
        if (!c.hasRowId()) { c.setRowId(++changes.maxPKeys[c.getTable()]); }
    }
}

bool ChangeTracker::addChangeInternalL(const Change& change) {
    const std::string tableName = change.getTable();
    changes.flatData.insert_or_assign(change.getKey(), change);
    changes.pKeyMappedData[tableName].insert_or_assign(change.getRowId(), change.getKey());
    logger.pushLog(Log{std::format("    Adding change {} to table {} at id {}", change.getKey(), change.getTable(), change.getRowId())});
    return true;
}

void ChangeTracker::collectAllDescendants(std::size_t key, std::unordered_set<std::size_t>& collected) const {
    if (collected.contains(key)) return;
    collected.insert(key);
    const Change& change = changes.flatData.at(key);
    for (std::size_t childKey : change.getChildren()) {
        collectAllDescendants(childKey, collected);
    }
}

void ChangeTracker::removeChanges(const std::size_t changeKey) {
    waitIfFrozen();
    std::unordered_set<std::size_t> toRemove;
    std::lock_guard<std::mutex> lg(changes.mtx);
    collectAllDescendants(changeKey, toRemove);
    for (std::size_t key : toRemove) {
        removeChangeL(key);
    }
}

void ChangeTracker::removeChanges(const Change::chHashV& changeHashes) {
    waitIfFrozen();
    std::unordered_set<std::size_t> toRemove;
    std::lock_guard<std::mutex> lg(changes.mtx);
    for (std::size_t key : changeHashes) {
        collectAllDescendants(key, toRemove);
    }
    for (std::size_t key : toRemove) {
        removeChangeL(key);
    }
}

uiChangeInfo ChangeTracker::getSnapShot() {
    std::lock_guard<std::mutex> lgChanges(changes.mtx);
    return uiChangeInfo{changes.pKeyMappedData, changes.flatData};
}

void ChangeTracker::removeChangeL(std::size_t key) {
    if (!changes.flatData.contains(key)) return;
    const Change& change = changes.flatData.at(key);
    auto& tableMap = changes.pKeyMappedData.at(change.getTable());
    tableMap.erase(change.getRowId());
    if (change.getRowId() == changes.maxPKeys.at(change.getTable())) {
        if (!tableMap.empty()) {
            changes.maxPKeys[change.getTable()] = tableMap.rbegin()->first;
        } else {
            changes.maxPKeys[change.getTable()] = initialMaxPKeys.at(change.getTable());
        }
    }

    logger.pushLog(Log{std::format("    Removing change {}", key)});
    changes.flatData.erase(key);
}

void ChangeTracker::setMaxPKeys(std::map<std::string, std::size_t> pk) {
    std::lock_guard<std::mutex> lgChanges(changes.mtx);
    changes.maxPKeys = pk;
    initialMaxPKeys = pk;
}

std::size_t ChangeTracker::getMaxPKey(const std::string table) {
    std::lock_guard<std::mutex> lgChanges(changes.mtx);

    if (!changes.maxPKeys.contains(table)) { return 0; }
    return changes.maxPKeys[table];
}

bool ChangeTracker::isChangeSelected(const std::size_t key) {
    std::lock_guard<std::mutex> lgChanges(changes.mtx);
    auto it = changes.flatData.find(key);
    if (it == changes.flatData.end()) { return false; }

    const Change* change = &it->second;
    while (change->hasParent()) {
        change = &changes.flatData.at(change->getParent());
    }

    return change->isSelected();
}

void ChangeTracker::toggleChangeSelect(const std::size_t key) {
    std::lock_guard<std::mutex> lgChanges(changes.mtx);
    if (!changes.flatData.contains(key)) { return; }
    if (changes.flatData.at(key).hasParent()) { return; }
    return changes.flatData.at(key).toggleSelect();
}

bool ChangeTracker::hasChild(const std::size_t key) {
    std::lock_guard<std::mutex> lgChanges(changes.mtx);
    if (!changes.flatData.contains(key)) { return false; }
    return changes.flatData.at(key).hasChildren();
}

std::vector<std::size_t> ChangeTracker::getChildren(const std::size_t key) {
    if (hasChild(key)) {
        std::lock_guard<std::mutex> lgChanges(changes.mtx);
        return changes.flatData.at(key).getChildren();
    }
    return std::vector<std::size_t>{};
}