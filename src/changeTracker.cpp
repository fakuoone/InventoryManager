#include "changeTracker.hpp"

#define WITH_DETAILED_LOG

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
    if (changes.flatData.contains(key)) {
        return changes.flatData.at(key);
    }
    return changes.flatData.at(key); // TODO: fix
}

bool ChangeTracker::isConflicting(const Change& newChange) {
    if (!newChange.hasRowId() || newChange.getType() == changeType::INSERT_ROW) {
        return false;
    }
    const std::string& table = newChange.getTable();
    const uint32_t rowId = newChange.getRowId();
    if (!changes.pKeyMappedData.contains(table)) {
        return false;
    }
    if (!changes.pKeyMappedData.at(table).contains(rowId)) {
        return false;
    }
    return true;
}

Change& ChangeTracker::manageConflictL(Change& newChange) {
    // TODO: only allow changes with the same UKEY, if it is updatecells (dont allow insertrow, if
    // its the same content)
    logDetail(std::format("Managing conflict for change {}.", newChange.getKey()));
    if (!isConflicting(newChange)) {
        return newChange;
    }
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
    logDetail(std::format("Propagating validity for change {}.", change.getKey()));
    if (change.hasChildren()) {
        bool childSum = true;
        for (const std::size_t& childKey : change.getChildren()) {
            if (!changes.flatData.contains(childKey)) {
                continue;
            }
            childSum &= changes.flatData.at(childKey).isValid();
        }
        change.setValidity(childSum);
    }
    if (change.hasParent()) {
        for (const std::size_t& parentKey : change.getParents()) {
            if (changes.flatData.contains(parentKey)) {
                propagateValidity(changes.flatData.at(parentKey));
            }
        }
    }
}

bool ChangeTracker::addChange(Change change, std::optional<uint32_t> existingRowId) {
    logDetail(std::format("Attempting to add change to table {}.", change.getTable()));
    {
        std::lock_guard<std::mutex> lg(changes.mtx);
        if (changes.uKeyMappedData.contains(change.getTable())) {
            // checks is there already exists a change, that has the same value in the ukey (name)
            // column
            const std::string ukey = dbService.getTableUKey(change.getTable());
            if (changes.uKeyMappedData.at(change.getTable()).contains(change.getCell(ukey))) {
                logger.pushLog(Log{std::format("ERROR: change with the same ukey (name): {} already exists", ukey)});
                return false;
            }
        }
    }

    if (!dbService.validateChange(change, false)) {
        return false;
    }

    std::vector<Change> allChanges;
    if (change.getType() == changeType::UPDATE_CELLS) {
        assert(existingRowId.has_value());
        change.setRowId(*existingRowId);
    }

    waitIfFrozen();
    {
        std::lock_guard<std::mutex> lg(changes.mtx);
        change = manageConflictL(change);
        collectRequiredChangesL(change, allChanges);
    }
    allocateIds(allChanges);

    std::lock_guard<std::mutex> lg(changes.mtx);
    for (Change& c : allChanges) {
        c = manageConflictL(c);
        propagateValidity(c);
        if (!addChangeInternalL(c)) {
            return false;
        }
    }

    return true;
}

void ChangeTracker::collectRequiredChangesL(Change& change, std::vector<Change>& out) {
    logDetail(std::format("Collecting required changes for change {}.", change.getKey()));
    std::vector<Change> required = dbService.getRequiredChanges(change, changes.maxPKeys);
    handleRequiredChildrenMismatch(change, required);
    for (Change& r : required) {
        if (!dbService.validateChange(r, true)) {
            return;
        }
        std::size_t existingRequiredKey = findExistingRequired(r);
        bool released = releaseDependancy(change, r);
        if (existingRequiredKey != 0) {
            Change& existingChange = changes.flatData.at(existingRequiredKey);
            if (released) {
                logDetail(std::format("Connecting change {} to existing change {}.", change.getKey(), existingChange.getKey()));
                existingChange.addParent(change.getKey());
                existingChange.setSelected(change.isSelected());
                change.pushChild(existingChange); // has to be set here, instead of getRequiredChanges, because
                // this will not get added to flatData
                changes.roots.erase(existingChange.getKey());
            }
        } else {
            change.pushChild(r);
        }
        if (existingRequiredKey != 0) {
            continue;
        }
        collectRequiredChangesL(r, out);
    }
    out.push_back(change);
}

void ChangeTracker::handleRequiredChildrenMismatch(Change& change, std::vector<Change>& rChanges) {
    if (rChanges.size() == 0) {
        releaseAllDependancies(change);
        return;
    }

    const std::vector<std::size_t>& children = change.getChildren();
    const std::size_t sizeDiff = children.size() - rChanges.size();

    // release Dependency when change now refers to existing data
    std::size_t diffsHandled = 0;
    for (const std::size_t childKey : change.getChildren()) {
        if (sizeDiff == diffsHandled) {
            return;
        }
        Change& child = changes.flatData.at(childKey);
        auto it = std::find_if(rChanges.begin(), rChanges.end(), [&](const Change& r) { return child.getTable() == r.getTable(); });

        // remove the child that is no longer in the required changes
        if (it == rChanges.end()) {
            change.removeChild(childKey);
            child.removeParent(change.getKey());
            diffsHandled++;
        }
    }
}

std::size_t ChangeTracker::findExistingRequired(const Change& rChange) {
    // finds, if a change with the same resulting table ukey-value exists
    logDetail(std::format("Finding existing change  for required change {}.", rChange.getKey()));
    const std::string& table = rChange.getTable();
    if (!changes.uKeyMappedData.contains(table)) {
        return 0;
    }
    const std::string& rChangeCellValue = rChange.getCell(dbService.getTableUKey(table));
    if (changes.uKeyMappedData.at(table).contains(rChangeCellValue)) {
        return changes.uKeyMappedData.at(table).at(rChangeCellValue);
    }
    return 0;
}

void ChangeTracker::releaseAllDependancies(Change& change) {
    for (const std::size_t& childKey : change.getChildren()) {
        Change& child = changes.flatData.at(childKey);
        change.removeChild(childKey);
        child.removeParent(change.getKey());
        // add to roots if it now has no parents
        if (!child.hasParent()) {
            changes.roots.insert(childKey);
        }
    }
}

bool ChangeTracker::releaseDependancy(Change& change, const Change& rC) {
    logDetail(std::format("Attempting to release dependency between change {} and {}.", change.getKey(), rC.getKey()));

    const std::string& rCTableName = rC.getTable();
    const std::string rCUKeyHeader = dbService.getTableUKey(rCTableName);
    const std::string& rCUKeyValue = "";
    std::string newRValue;

    // find new equivalent value corresponding to the old ukey-value of rC
    for (const auto& [col, val] : change.getCells()) {
        tHeaderInfo headerInfoChange = dbService.getTableHeaderInfo(change.getTable(), col);
        if (headerInfoChange.referencedTable == rCTableName) {
            newRValue = val;
            break;
        }
    }

    if (newRValue.empty()) {
        return false;
    }

    // find the previous requiredChange that needs to be released
    bool hadRelevantChildren = false;
    for (const std::size_t& childKey : change.getChildren()) {
        if (!changes.flatData.contains(childKey)) {
            continue;
        } // not created yet
        Change& child = changes.flatData.at(childKey);
        if (child.getTable() != rCTableName) {
            continue;
        } // wrong child
        hadRelevantChildren = true;
        if (child.getCell(rCUKeyHeader) != newRValue) { // previous requiredChange found
            change.removeChild(childKey);
            child.removeParent(change.getKey());
            // add to roots if it now has no parents
            if (!child.hasParent()) {
                changes.roots.insert(childKey);
            }
            return true;
        };
    }
    return !hadRelevantChildren;
}

void ChangeTracker::allocateIds(std::vector<Change>& allChanges) {
    for (Change& c : allChanges) {
        if (!c.hasRowId()) {
            c.setRowId(++changes.maxPKeys[c.getTable()]);
        }
    }
}

bool ChangeTracker::addChangeInternalL(const Change& change) {
    const std::string tableName = change.getTable();
    changes.flatData.insert_or_assign(change.getKey(), change);
    changes.pKeyMappedData[tableName].insert_or_assign(change.getRowId(), change.getKey());
    // store ukey value to prevent duplicates
    const std::string changeUKeyValue = change.getCell(dbService.getTableUKey(tableName));
    if (!changeUKeyValue.empty()) {
        changes.uKeyMappedData[tableName].insert_or_assign(changeUKeyValue, change.getKey());
    }
    // store as root if no parent
    if (!change.hasParent()) {
        changes.roots.insert(change.getKey());
    }
    logger.pushLog(Log{std::format("    Adding change {} to table {} at id {}", change.getKey(), change.getTable(), change.getRowId())});
    return true;
}

void ChangeTracker::collectAllDescendants(std::size_t key, std::unordered_set<std::size_t>& collected) {
    if (collected.contains(key)) {
        return;
    }
    const Change& change = changes.flatData.at(key);
    if (change.getParentCount() > 1) {
        return;
    } // dont delete, when multiple parents exist
    collected.insert(key);
    for (std::size_t childKey : change.getChildren()) {
        collectAllDescendants(childKey, collected);
        changes.flatData.at(childKey).removeParent(key);
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
    return uiChangeInfo{changes.pKeyMappedData, changes.flatData, changes.roots};
}

void ChangeTracker::removeChangeL(std::size_t key) {
    if (!changes.flatData.contains(key)) {
        return;
    };
    const Change& change = changes.flatData.at(key);
    const std::string& tableName = change.getTable();
    auto& pkeyMap = changes.pKeyMappedData.at(tableName);
    // remove ukey-entry if it exists
    if (changes.uKeyMappedData.contains(tableName)) {
        auto& ukeyMap = changes.uKeyMappedData.at(tableName);
        ukeyMap.erase(change.getCell(dbService.getTableUKey(tableName)));
    }
    pkeyMap.erase(change.getRowId());
    if (change.getRowId() == changes.maxPKeys.at(tableName)) {
        if (!pkeyMap.empty()) {
            changes.maxPKeys[tableName] = pkeyMap.rbegin()->first;
        } else {
            changes.maxPKeys[tableName] = initialMaxPKeys.at(tableName);
        }
    }

    changes.roots.erase(key);

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

    if (!changes.maxPKeys.contains(table)) {
        return 0;
    }
    return changes.maxPKeys[table];
}

bool ChangeTracker::isChangeSelected(const std::size_t key) {
    std::lock_guard<std::mutex> lgChanges(changes.mtx);
    if (!changes.flatData.contains(key)) {
        return false;
    }
    return changes.flatData.at(key).isSelected();
}

void ChangeTracker::toggleChangeSelect(const std::size_t key) {
    std::lock_guard<std::mutex> lgChanges(changes.mtx);
    if (!changes.flatData.contains(key)) {
        return;
    }
    if (changes.flatData.at(key).hasParent()) {
        return;
    }
    setChangeRecL(changes.flatData.at(key), !changes.flatData.at(key).isSelected());
    return;
}

void ChangeTracker::setChangeRecL(Change& change, bool value) {
    // if (change.getParents().size() > 1) { return; }
    change.setSelected(value);
    for (const std::size_t& childKey : change.getChildren()) {
        Change& childChange = changes.flatData.at(childKey);
        setChangeRecL(childChange, value);
    }
}

bool ChangeTracker::hasChild(const std::size_t key) {
    std::lock_guard<std::mutex> lgChanges(changes.mtx);
    if (!changes.flatData.contains(key)) {
        return false;
    }
    return changes.flatData.at(key).hasChildren();
}

std::vector<std::size_t> ChangeTracker::getChildren(const std::size_t key) {
    if (hasChild(key)) {
        std::lock_guard<std::mutex> lgChanges(changes.mtx);
        return changes.flatData.at(key).getChildren();
    }
    return std::vector<std::size_t>{};
}

std::vector<std::size_t> ChangeTracker::getCalcRoots() {
    std::lock_guard<std::mutex> lgChanges(changes.mtx);
    std::vector<std::size_t> all;
    std::size_t count = changes.flatData.size();
    all.reserve(count);

    for (auto it = changes.flatData.begin(); it != changes.flatData.end(); ++it) {
        if (!it->second.hasParent()) {
            all.push_back(it->first);
        }
    }

    return all;
}

std::unordered_set<std::size_t> ChangeTracker::getRoots() {
    std::lock_guard<std::mutex> lgChanges(changes.mtx);
    return changes.roots;
}

void ChangeTracker::logDetail(std::string content) {
#ifdef WITH_DETAILED_LOG
    logger.pushLog(Log{std::format("      INTERNAL: {}", std::move(content))});
#endif
}