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

void ChangeTracker::removeChanges(const std::vector<std::size_t>& changeHashes) {
    std::lock_guard<std::mutex> lgChanges(changes.mtx);
    for (const auto& hash : changeHashes) {
        if (changes.data.contains(hash)) {
            logger.pushLog(Log{std::format("    Removing change {}", hash)});
            changes.data.erase(hash);
        }
    }
}

std::map<std::size_t, Change<int>> ChangeTracker::getChanges() {
    std::lock_guard<std::mutex> lgChanges(changes.mtx);
    return changes.data;
}
