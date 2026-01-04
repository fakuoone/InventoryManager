#include "include/changeTracker.hpp"

void ChangeTracker::mergeCellChanges(Change<int>& existingChange, const Change<int>& newChange) {
    // assignment operator overloaded
    logger.pushLog(Log{std::format("        Merging cell changes {} and {}", existingChange.getHash(), newChange.getHash())});
    existingChange = newChange;
}

bool ChangeTracker::manageConflict(const Change<int>& newChange, std::size_t hash) {
    if (!changes.data.contains(hash)) {
        return true;
    }
    Change<int>& existingChange = changes.data.at(hash);
    switch (existingChange.getType()) {
        case changeType::InsertRow:
            return false;

        case changeType::DeleteRow:
            return false;

        case changeType::UpdateCells:
            mergeCellChanges(existingChange, newChange);
            return true;
        default:
            break;
    }
    return false;
}

void ChangeTracker::addChange(const Change<int>& change) {
    // The only function that is allowed to lock changes
    if (!dbService.validateChange(change)) {
        return;
    }
    const std::size_t hash = change.getHash();
    std::lock_guard<std::mutex> lgChanges(changes.mtx);
    logger.pushLog(Log{std::format("    Adding change {}", change.getHash())});
    if (manageConflict(change, hash)) {
        changes.data.emplace(hash, change);
    }
}

void ChangeTracker::removeChange(Change<int>& change) {
    const std::size_t hash = change.getHash();
    std::lock_guard<std::mutex> lgChanges(changes.mtx);
    if (changes.data.contains(hash)) {
        changes.data.erase(hash);
    }
}