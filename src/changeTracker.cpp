#include "changeTracker.hpp"

void ChangeTracker::mergeCellChanges(Change& existingChange, const Change& newChange) {
    logger.pushLog(Log{std::format("        Merging cell changes {} and {}", existingChange.getKey(), newChange.getKey())});
    existingChange ^ newChange;
}

bool ChangeTracker::manageConflict(const Change& newChange, std::size_t key) {
    if (!changes.flatData.contains(key)) { return true; }
    Change& existingChange = changes.flatData.at(key);
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

bool ChangeTracker::addChange(const Change& change) {
    // change data
    const std::string table = change.getTable();
    const std::size_t key = change.getKey();
    // validate change
    if (!dbService.validateChange(change)) { return false; }
    std::vector<Change> requiredChanges = dbService.getRequiredChanges(change);
    for (const Change& requiredChange : requiredChanges) {
        addChange(requiredChange);
    }

    std::lock_guard<std::mutex> lgChanges(changes.mtx);
    if (!manageConflict(change, key)) { return false; }
    addChangeInternal(change);
    return true;
}

void ChangeTracker::addChangeInternal(const Change& change) {
    const std::string tableName = change.getTable();
    if (change.getType() == changeType::INSERT_ROW) {
        if (change.getRowId() > changes.maxPKeys[tableName]) {
            changes.maxPKeys[tableName] = change.getRowId();
        } else {
            changes.maxPKeys[tableName]++;
        }
    }
    // adding change
    logger.pushLog(Log{std::format("    Adding change {}", change.getKey())});
    changes.flatData.emplace(change.getKey(), change);
    if (!changes.pKeyMappedData.contains(tableName)) { changes.pKeyMappedData.emplace(tableName, Change::chHHMap{}); }
    changes.pKeyMappedData.at(tableName).emplace(change.getRowId(), change.getKey());
}

void ChangeTracker::addRelatedChange(std::size_t baseHash, const Change& change) {
    std::lock_guard<std::mutex> lgChanges(changes.mtx);
    if (!changes.flatData.contains(baseHash)) { return; }
    // TODO: Manage related changes
}

void ChangeTracker::removeChanges(const Change::chHashV& changeHashes) {
    std::lock_guard<std::mutex> lgChanges(changes.mtx);
    for (const auto& key : changeHashes) {
        removeChange(key);
    }
}

Change::chHashM ChangeTracker::getChanges() {
    std::lock_guard<std::mutex> lgChanges(changes.mtx);
    return changes.flatData;
}

Change::ctPKMD ChangeTracker::getRowMappedData() {
    std::lock_guard<std::mutex> lgChanges(changes.mtx);
    return changes.pKeyMappedData;
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
    if (!changes.flatData.contains(key)) { return false; }
    Change& change = changes.flatData.at(key);
    while (change.hasParent()) {
        change = changes.flatData.at(change.getParent());
    }
    return changes.flatData.at(change.getKey()).isSelected();
}

void ChangeTracker::toggleChangeSelect(const std::size_t key) {
    if (!changes.flatData.contains(key)) { return; }
    if (changes.flatData.at(key).hasParent()) { return; }
    return changes.flatData.at(key).toggleSelect();
}