#include "changeTracker.hpp"

void ChangeTracker::mergeCellChanges(Change& existingChange, const Change& newChange) {
    logger.pushLog(Log{std::format("        Merging cell changes {} and {}", existingChange.getKey(), newChange.getKey())});
    existingChange ^ newChange;
}

bool ChangeTracker::manageConflictL(const Change& newChange) {
    if (!changes.flatData.contains(newChange.getKey())) { return true; }
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
    logger.pushLog(Log{"ADDCHANGE CALLED"});
    if (!dbService.validateChange(change)) { return false; }

    std::vector<Change> allChanges;
    collectRequiredChanges(change, allChanges);

    std::lock_guard<std::mutex> lg(changes.mtx);

    for (const Change& c : allChanges) {
        if (!manageConflictL(c)) { return false; }
        addChangeInternalL(c);
    }

    return true;
}

void ChangeTracker::collectRequiredChanges(Change& change, std::vector<Change>& out) {
    out.push_back(change);
    auto required = dbService.getRequiredChanges(change, changes.maxPKeys);
    change.pushChildren(required);
    for (Change& r : required) {
        collectRequiredChanges(r, out);
    }
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

void ChangeTracker::removeChanges(const Change::chHashV& changeHashes) {
    std::lock_guard<std::mutex> lgChanges(changes.mtx);
    for (const auto& key : changeHashes) {
        removeChange(key);
    }
}

uiChangeInfo ChangeTracker::getSnapShot() {
    std::lock_guard<std::mutex> lgChanges(changes.mtx);
    return uiChangeInfo{changes.pKeyMappedData, changes.flatData};
}

void ChangeTracker::removeChange(const std::size_t key) {
    std::lock_guard<std::mutex> lgChanges(changes.mtx);
    if (changes.flatData.contains(key)) {
        // TODO: ERror handling
        const Change& change = changes.flatData.at(key);
        changes.pKeyMappedData.at(change.getTable()).erase(change.getRowId());
        if (change.getRowId() == changes.maxPKeys.at(change.getTable())) {
            const Change::chHHMap& pkmd = changes.pKeyMappedData.at(change.getTable());
            if (pkmd.size() != 0) {
                changes.maxPKeys[change.getTable()] = pkmd.rbegin()->first;
            } else {
                changes.maxPKeys[change.getTable()] = initialMaxPKeys.at(change.getTable());
            }
        }
        logger.pushLog(Log{std::format("    Removing change {}", key)});
        changes.flatData.erase(key);
    }
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
    auto it = changes.flatData.find(key);
    if (it == changes.flatData.end()) { return false; }

    const Change* change = &it->second;
    while (change->hasParent()) {
        change = &changes.flatData.at(change->getParent());
    }

    return change->isSelected();
}

void ChangeTracker::toggleChangeSelect(const std::size_t key) {
    if (!changes.flatData.contains(key)) { return; }
    if (changes.flatData.at(key).hasParent()) { return; }
    return changes.flatData.at(key).toggleSelect();
}