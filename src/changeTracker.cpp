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

bool ChangeTracker::manageConflictL(const Change& newChange) {
    if (!changes.flatData.contains(newChange.getKey())) { return true; }
    std::lock_guard<std::mutex> lg(changes.mtx);
    Change& existingChange = changes.flatData.at(newChange.getKey());
    switch (existingChange.getType()) {
        case changeType::INSERT_ROW:
            return false;

        case changeType::DELETE_ROW:
            return false;

        case changeType::UPDATE_CELLS:
            mergeCellChanges(existingChange, newChange);
            return true;
        default:
            break;
    }
    return false;
}

bool ChangeTracker::addChange(Change change) {
    if (!dbService.validateChange(change, false)) { return false; }

    std::vector<Change> allChanges;
    waitIfFrozen();
    collectRequiredChanges(change, allChanges);
    std::lock_guard<std::mutex> lg(changes.mtx);

    for (const Change& c : allChanges) {
        if (!manageConflictL(c)) { return false; }
        addChangeInternalL(c);
    }

    return true;
}

void ChangeTracker::collectRequiredChanges(Change& change, std::vector<Change>& out) {
    std::vector<Change> required = dbService.getRequiredChanges(change, changes.maxPKeys);
    for (Change& r : required) {
        if (!dbService.validateChange(r, true)) { return; }
        change.pushChild(r);
        collectRequiredChanges(r, out);
    }
    out.push_back(change);
}

void ChangeTracker::addChangeInternalL(const Change& change) {
    const std::string tableName = change.getTable();
    if (change.getType() == changeType::INSERT_ROW) {
        if (change.getRowId() > changes.maxPKeys[tableName]) {
            changes.maxPKeys[tableName] = change.getRowId();
        } else {
            changes.maxPKeys[tableName]++;
        }
    }
    // adding change
    auto [it, inserted] = changes.flatData.emplace(change.getKey(), change);
    if (!inserted) {
        logger.pushLog(Log{std::format("DUPLICATE KEY {}", change.getKey())});
        return;
    }

    if (inserted) { changes.pKeyMappedData[tableName].emplace(change.getRowId(), change.getKey()); }
    logger.pushLog(Log{std::format("    Adding change {} to table {} at id {}", change.getKey(), change.getTable(), change.getRowId())});
}

void ChangeTracker::collectAllDescendants(std::size_t key, std::unordered_set<std::size_t>& collected) const {
    if (collected.contains(key)) return;

    collected.insert(key);

    // Will throw std::out_of_range if key does not exist
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
    return changes.maxPKeys.at(table);
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