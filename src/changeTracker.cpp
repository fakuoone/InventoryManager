#include "changeTracker.hpp"

void ChangeTracker::mergeCellChanges(Change<cccType>& existingChange, const Change<cccType>& newChange) {
    // assignment operator overloaded
    logger.pushLog(Log{std::format("        Merging cell changes {} and {}", existingChange.getHash(), newChange.getHash())});
    existingChange = newChange;
}

bool ChangeTracker::manageConflict(const Change<cccType>& newChange, std::size_t hash) {
    if (!changes.flatData.contains(hash)) { return true; }
    Change<cccType>& existingChange = changes.flatData.at(hash);
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

void ChangeTracker::addChange(const Change<cccType>& change) {
    // The only function that is allowed to lock changes
    std::lock_guard<std::mutex> lgChanges(changes.mtx);
    if (!dbService.validateChange(change)) { return; }
    const std::size_t hash = change.getHash();
    logger.pushLog(Log{std::format("    Adding change {}", change.getHash())});
    if (manageConflict(change, hash)) {
        changes.flatData.emplace(hash, change);
        const std::string table = change.getTable();
        if (!changes.rowMappedData.contains(table)) { changes.rowMappedData.emplace(table, std::map<cccType, std::size_t>{}); }
        changes.rowMappedData.at(table).emplace(change.getRowId(), hash);
    }
}

void ChangeTracker::addRelatedChange(std::size_t baseHash, const Change<cccType>& change) {
    std::lock_guard<std::mutex> lgChanges(changes.mtx);
    if (!changes.flatData.contains(baseHash)) { return; }
    // TODO: Manage related changes
}

void ChangeTracker::removeChanges(const std::vector<std::size_t>& changeHashes) {
    std::lock_guard<std::mutex> lgChanges(changes.mtx);
    for (const auto& hash : changeHashes) {
        removeChange(hash);
    }
}

std::map<std::size_t, Change<cccType>> ChangeTracker::getChanges() {
    std::lock_guard<std::mutex> lgChanges(changes.mtx);
    return changes.flatData;
}

std::map<std::string, std::map<cccType, std::size_t>> ChangeTracker::getRowMappedData() {
    std::lock_guard<std::mutex> lgChanges(changes.mtx);
    return changes.rowMappedData;
}

void ChangeTracker::removeChange(const std::size_t hash) {
    if (changes.flatData.contains(hash)) {
        // TODO: ERror handling
        const Change<cccType>& change = changes.flatData.at(hash);
        changes.rowMappedData.at(change.getTable()).erase(change.getRowId());
        logger.pushLog(Log{std::format("    Removing change {}", hash)});
        changes.flatData.erase(hash);
    }
}