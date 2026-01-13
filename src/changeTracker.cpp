#include "changeTracker.hpp"

void ChangeTracker::mergeCellChanges(Change& existingChange, const Change& newChange) {
    // assignment operator overloaded
    logger.pushLog(Log{std::format("        Merging cell changes {} and {}", existingChange.getHash(), newChange.getHash())});
    existingChange = newChange;
}

bool ChangeTracker::manageConflict(const Change& newChange, std::size_t hash) {
    if (!changes.flatData.contains(hash)) { return true; }
    Change& existingChange = changes.flatData.at(hash);
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

void ChangeTracker::addChange(const Change& change) {
    // The only function that is allowed to lock changes
    const std::string table = change.getTable();
    const std::size_t hash = change.getHash();
    if (!dbService.validateChange(change)) { return; }
    std::lock_guard<std::mutex> lgChanges(changes.mtx);
    if (manageConflict(change, hash)) {
        if (change.getType() == changeType::INSERT_ROW) {
            if (change.getRowId() > changes.maxPKeys[table]) {
                changes.maxPKeys[table] = change.getRowId();
            } else {
                changes.maxPKeys[table]++;
            }
        }
        changes.flatData.emplace(hash, change);
        if (!changes.pKeyMappedData.contains(table)) { changes.pKeyMappedData.emplace(table, Change::chHHMap{}); }
        changes.pKeyMappedData.at(table).emplace(change.getRowId(), hash);
    }
    logger.pushLog(Log{std::format("    Adding change {}", hash)});
}

void ChangeTracker::addRelatedChange(std::size_t baseHash, const Change& change) {
    std::lock_guard<std::mutex> lgChanges(changes.mtx);
    if (!changes.flatData.contains(baseHash)) { return; }
    // TODO: Manage related changes
}

void ChangeTracker::removeChanges(const Change::chHashV& changeHashes) {
    std::lock_guard<std::mutex> lgChanges(changes.mtx);
    for (const auto& hash : changeHashes) {
        removeChange(hash);
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

void ChangeTracker::removeChange(const std::size_t hash) {
    std::lock_guard<std::mutex> lgChanges(changes.mtx);
    if (changes.flatData.contains(hash)) {
        // TODO: ERror handling
        const Change& change = changes.flatData.at(hash);
        // TODO: Bedingung ist nicht ausreichend für runterzählen
        if (change.getRowId() == changes.maxPKeys.at(change.getTable()) && change.getRowId() > 0) { changes.maxPKeys[change.getTable()]--; }
        changes.pKeyMappedData.at(change.getTable()).erase(change.getRowId());
        logger.pushLog(Log{std::format("    Removing change {}", hash)});
        changes.flatData.erase(hash);
    }
}

void ChangeTracker::setMaxPKeys(std::map<std::string, std::size_t> pk) {
    std::lock_guard<std::mutex> lgChanges(changes.mtx);
    changes.maxPKeys = pk;
}

std::size_t ChangeTracker::getMaxPKey(const std::string table) {
    std::lock_guard<std::mutex> lgChanges(changes.mtx);

    if (!changes.maxPKeys.contains(table)) { return 0; }
    return changes.maxPKeys.at(table);
}