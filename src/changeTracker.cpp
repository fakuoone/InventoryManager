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
    std::lock_guard<std::mutex> lgChanges(changes.mtx);
    if (!dbService.validateChange(change)) { return; }
    const std::size_t hash = change.getHash();
    logger.pushLog(Log{std::format("    Adding change {}", change.getHash())});
    if (manageConflict(change, hash)) {
        changes.flatData.emplace(hash, change);
        const std::string table = change.getTable();
        if (!changes.rowMappedData.contains(table)) { changes.rowMappedData.emplace(table, Change::chHHMap{}); }
        changes.rowMappedData.at(table).emplace(change.getRowId(), hash);
    }
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

Change::ctRMD ChangeTracker::getRowMappedData() {
    std::lock_guard<std::mutex> lgChanges(changes.mtx);
    return changes.rowMappedData;
}

void ChangeTracker::removeChange(const std::size_t hash) {
    if (changes.flatData.contains(hash)) {
        // TODO: ERror handling
        const Change& change = changes.flatData.at(hash);
        changes.rowMappedData.at(change.getTable()).erase(change.getRowId());
        logger.pushLog(Log{std::format("    Removing change {}", hash)});
        changes.flatData.erase(hash);
    }
}