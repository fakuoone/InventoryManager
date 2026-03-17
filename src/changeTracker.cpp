#include "changeTracker.hpp"

#define WITH_DETAILED_LOG

void ChangeTracker::mergeCellChanges(Change& existingChange, const Change& newChange) {
    logger_.pushLog(Log{std::format("        Merging cell changes {} and {}", existingChange.getKey(), newChange.getKey())});
    existingChange ^ newChange;
}

void ChangeTracker::freeze() {
    std::unique_lock lock(freezeMtx_);
    frozen_.store(true, std::memory_order_release);
}

void ChangeTracker::unfreeze() {
    {
        std::unique_lock lock(freezeMtx_);
        frozen_.store(false, std::memory_order_release);
    }
    freezeCv_.notify_all();
}

void ChangeTracker::waitIfFrozen() {
    std::unique_lock lock(freezeMtx_);
    freezeCv_.wait(lock, [this] { return !frozen_.load(std::memory_order_acquire); });
}

std::optional<Change> ChangeTracker::getChange(std::size_t key) {
    std::lock_guard<std::mutex> lg(changes_.mtx);

    auto it = changes_.flatData.find(key);
    if (it == changes_.flatData.end()) {
        logger_.pushLog(Log{std::format("ERROR: Change with key {} not found.", key)});
        return std::nullopt;
    }

    return it->second;
}

bool ChangeTracker::isConflicting(const Change& newChange) {
    if (!newChange.hasRowId() || newChange.getType() == ChangeType::INSERT_ROW) { return false; }
    const std::string& table = newChange.getTable();
    const uint32_t rowId = newChange.getRowId();
    if (!changes_.pKeyMappedData.contains(table)) { return false; }
    if (!changes_.pKeyMappedData.at(table).contains(rowId)) { return false; }
    return true;
}

Change& ChangeTracker::manageConflictL(Change& newChange) {
    logDetail(std::format("Managing conflict for change {}.", newChange.getKey()));
    if (!isConflicting(newChange)) { return newChange; }
    const std::string& table = newChange.getTable();
    const uint32_t rowId = newChange.getRowId();
    Change& existingChange = changes_.flatData.at(changes_.pKeyMappedData.at(table).at(rowId));
    switch (existingChange.getType()) {
    case ChangeType::DELETE_ROW:
        return existingChange;
    case ChangeType::INSERT_ROW:
        [[fallthrough]];
    case ChangeType::UPDATE_CELLS:
        mergeCellChanges(existingChange, newChange);
        dbService_.validateChange(existingChange, false);
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
            if (!changes_.flatData.contains(childKey)) { continue; }
            childSum &= changes_.flatData.at(childKey).isValid();
        }
        change.setValidity(childSum);
    }
    if (change.hasParent()) {
        for (const std::size_t& parentKey : change.getParents()) {
            if (changes_.flatData.contains(parentKey)) { propagateValidity(changes_.flatData.at(parentKey)); }
        }
    }
}

ChangeAddResult ChangeTracker::addChange(Change change, std::optional<uint32_t> existingRowId) {
    logDetail(std::format("Attempting to add change to table {}.", change.getTable()));
    {
        std::lock_guard<std::mutex> lg(changes_.mtx);
        if (changes_.uKeyMappedData.contains(change.getTable())) {
            // checks is there already exists a change, that has the same value in the ukey (name)
            // column
            const std::string ukey = dbService_.getTableUKey(change.getTable());
            if (changes_.uKeyMappedData.at(change.getTable()).contains(change.getCell(ukey))) {
                logger_.pushLog(Log{std::format("ERROR: change with the same ukey (name): {} already exists", ukey)});
                return ChangeAddResult::ALREADY_EXISTING;
            }
        }
    }

    if (!dbService_.validateChange(change, false)) { return ChangeAddResult::INVALID; }

    std::vector<Change> allChanges;
    if (change.getType() == ChangeType::UPDATE_CELLS) {
        assert(existingRowId.has_value());
        change.setRowId(*existingRowId);
    }

    waitIfFrozen();
    {
        std::lock_guard<std::mutex> lg(changes_.mtx);
        change = manageConflictL(change);
        collectRequiredChangesL(change, allChanges);
    }
    allocateIds(allChanges);

    std::lock_guard<std::mutex> lg(changes_.mtx);
    for (Change& c : allChanges) {
        c = manageConflictL(c);
        propagateValidity(c);
        if (!addChangeInternalL(c)) { return ChangeAddResult::INTERNAL_FAILURE; }
    }

    return ChangeAddResult::SUCCESS;
}

void ChangeTracker::collectRequiredChangesL(Change& change, std::vector<Change>& out) {
    logDetail(std::format("Collecting required changes for change {}.", change.getKey()));
    std::vector<Change> required = dbService_.getRequiredChanges(change, changes_.maxPKeys);
    handleRequiredChildrenMismatch(change, required);
    for (Change& r : required) {
        if (!dbService_.validateChange(r, true)) { return; }
        std::size_t existingRequiredKey = findExistingRequired(r);
        bool released = releaseDependancy(change, r);
        if (existingRequiredKey != 0) {
            Change& existingChange = changes_.flatData.at(existingRequiredKey);
            if (released) {
                logDetail(std::format("Connecting change {} to existing change {}.", change.getKey(), existingChange.getKey()));
                existingChange.addParent(change.getKey());
                existingChange.setSelected(change.isSelected());
                change.pushChild(existingChange); // has to be set here, instead of getRequiredChanges, because
                // this will not get added to flatData
                changes_.roots.erase(existingChange.getKey());
            }
        } else {
            change.pushChild(r);
        }
        if (existingRequiredKey != 0) { continue; }
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
        if (sizeDiff == diffsHandled) { return; }
        Change& child = changes_.flatData.at(childKey);
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
    if (!changes_.uKeyMappedData.contains(table)) { return 0; }
    const std::string& rChangeCellValue = rChange.getCell(dbService_.getTableUKey(table));
    if (changes_.uKeyMappedData.at(table).contains(rChangeCellValue)) { return changes_.uKeyMappedData.at(table).at(rChangeCellValue); }
    return 0;
}

void ChangeTracker::releaseAllDependancies(Change& change) {
    for (const std::size_t& childKey : change.getChildren()) {
        Change& child = changes_.flatData.at(childKey);
        change.removeChild(childKey);
        child.removeParent(change.getKey());
        // add to roots if it now has no parents
        if (!child.hasParent()) { changes_.roots.insert(childKey); }
    }
}

bool ChangeTracker::releaseDependancy(Change& change, const Change& rC) {
    logDetail(std::format("Attempting to release dependency between change {} and {}.", change.getKey(), rC.getKey()));

    const std::string& rCTableName = rC.getTable();
    const std::string rCUKeyHeader = dbService_.getTableUKey(rCTableName);
    const std::string& rCUKeyValue = "";
    std::string newRValue;

    // find new equivalent value corresponding to the old ukey-value of rC
    for (const auto& [col, val] : change.getCells()) {
        HeaderInfo headerInfoChange = dbService_.getTableHeaderInfo(change.getTable(), col);
        if (headerInfoChange.referencedTable == rCTableName) {
            newRValue = val;
            break;
        }
    }

    if (newRValue.empty()) { return false; }

    // find the previous requiredChange that needs to be released
    bool hadRelevantChildren = false;
    for (const std::size_t& childKey : change.getChildren()) {
        if (!changes_.flatData.contains(childKey)) { continue; } // not created yet
        Change& child = changes_.flatData.at(childKey);
        if (child.getTable() != rCTableName) { continue; } // wrong child
        hadRelevantChildren = true;
        if (child.getCell(rCUKeyHeader) != newRValue) { // previous requiredChange found
            change.removeChild(childKey);
            child.removeParent(change.getKey());
            // add to roots if it now has no parents
            if (!child.hasParent()) { changes_.roots.insert(childKey); }
            return true;
        };
    }
    return !hadRelevantChildren;
}

void ChangeTracker::allocateIds(std::vector<Change>& allChanges) {
    for (Change& c : allChanges) {
        if (!c.hasRowId()) { c.setRowId(++changes_.maxPKeys[c.getTable()]); }
    }
}

bool ChangeTracker::addChangeInternalL(const Change& change) {
    const std::string tableName = change.getTable();
    changes_.flatData.insert_or_assign(change.getKey(), change);
    changes_.pKeyMappedData[tableName].insert_or_assign(change.getRowId(), change.getKey());
    // store ukey value to prevent duplicates
    const std::string changeUKeyValue = change.getCell(dbService_.getTableUKey(tableName));
    if (!changeUKeyValue.empty()) { changes_.uKeyMappedData[tableName].insert_or_assign(changeUKeyValue, change.getKey()); }
    // store as root if no parent
    if (!change.hasParent()) { changes_.roots.insert(change.getKey()); }
    logger_.pushLog(Log{std::format("    Adding change {} to table {} at id {}", change.getKey(), change.getTable(), change.getRowId())});
    return true;
}

void ChangeTracker::collectAllDescendants(std::size_t key, std::unordered_set<std::size_t>& collected) {
    if (collected.contains(key)) { return; }
    const Change& change = changes_.flatData.at(key);
    if (change.getParentCount() > 1) { return; } // dont delete, when multiple parents exist
    collected.insert(key);
    for (std::size_t childKey : change.getChildren()) {
        collectAllDescendants(childKey, collected);
        changes_.flatData.at(childKey).removeParent(key);
    }
}

void ChangeTracker::removeChanges(const std::size_t changeKey) {
    waitIfFrozen();
    std::unordered_set<std::size_t> toRemove;
    std::lock_guard<std::mutex> lg(changes_.mtx);
    collectAllDescendants(changeKey, toRemove);
    for (std::size_t key : toRemove) {
        removeChangeL(key);
    }
}

void ChangeTracker::removeChanges(const Change::chHashV& changeHashes) {
    waitIfFrozen();
    std::unordered_set<std::size_t> toRemove;
    std::lock_guard<std::mutex> lg(changes_.mtx);
    for (std::size_t key : changeHashes) {
        collectAllDescendants(key, toRemove);
    }
    for (std::size_t key : toRemove) {
        removeChangeL(key);
    }
}

uiChangeInfo ChangeTracker::getSnapShot() {
    std::lock_guard<std::mutex> lgChanges(changes_.mtx);
    return uiChangeInfo{changes_.pKeyMappedData, changes_.flatData, changes_.roots};
}

void ChangeTracker::removeChangeL(std::size_t key) {
    if (!changes_.flatData.contains(key)) { return; };
    const Change& change = changes_.flatData.at(key);
    const std::string& tableName = change.getTable();
    auto& pkeyMap = changes_.pKeyMappedData.at(tableName);
    // remove ukey-entry if it exists
    if (changes_.uKeyMappedData.contains(tableName)) {
        auto& ukeyMap = changes_.uKeyMappedData.at(tableName);
        ukeyMap.erase(change.getCell(dbService_.getTableUKey(tableName)));
    }
    pkeyMap.erase(change.getRowId());
    if (change.getRowId() == changes_.maxPKeys.at(tableName)) {
        if (!pkeyMap.empty()) {
            changes_.maxPKeys[tableName] = pkeyMap.rbegin()->first;
        } else {
            changes_.maxPKeys[tableName] = initialMaxPKeys_.at(tableName);
        }
    }

    changes_.roots.erase(key);

    logger_.pushLog(Log{std::format("    Removing change {}", key)});
    changes_.flatData.erase(key);
}

void ChangeTracker::setMaxPKeys(std::map<std::string, std::size_t> pk) {
    std::lock_guard<std::mutex> lgChanges(changes_.mtx);
    changes_.maxPKeys = pk;
    initialMaxPKeys_ = pk;
}

std::size_t ChangeTracker::getMaxPKey(const std::string table) {
    std::lock_guard<std::mutex> lgChanges(changes_.mtx);

    if (!changes_.maxPKeys.contains(table)) { return 0; }
    return changes_.maxPKeys[table];
}

bool ChangeTracker::isChangeSelected(const std::size_t key) {
    std::lock_guard<std::mutex> lgChanges(changes_.mtx);
    if (!changes_.flatData.contains(key)) { return false; }
    return changes_.flatData.at(key).isSelected();
}

void ChangeTracker::toggleChangeSelect(const std::size_t key) {
    std::lock_guard<std::mutex> lgChanges(changes_.mtx);
    if (!changes_.flatData.contains(key)) { return; }
    if (changes_.flatData.at(key).hasParent()) { return; }
    setChangeRecL(changes_.flatData.at(key), !changes_.flatData.at(key).isSelected());
    return;
}

void ChangeTracker::setChangeRecL(Change& change, bool value) {
    // if (change.getParents().size() > 1) { return; }
    change.setSelected(value);
    for (const std::size_t& childKey : change.getChildren()) {
        Change& childChange = changes_.flatData.at(childKey);
        setChangeRecL(childChange, value);
    }
}

bool ChangeTracker::hasChild(const std::size_t key) {
    std::lock_guard<std::mutex> lgChanges(changes_.mtx);
    if (!changes_.flatData.contains(key)) { return false; }
    return changes_.flatData.at(key).hasChildren();
}

std::vector<std::size_t> ChangeTracker::getChildren(const std::size_t key) {
    if (hasChild(key)) {
        std::lock_guard<std::mutex> lgChanges(changes_.mtx);
        return changes_.flatData.at(key).getChildren();
    }
    return std::vector<std::size_t>{};
}

std::vector<std::size_t> ChangeTracker::getCalcRoots() {
    std::lock_guard<std::mutex> lgChanges(changes_.mtx);
    std::vector<std::size_t> all;
    std::size_t count = changes_.flatData.size();
    all.reserve(count);

    for (auto it = changes_.flatData.begin(); it != changes_.flatData.end(); ++it) {
        if (!it->second.hasParent()) { all.push_back(it->first); }
    }

    return all;
}

std::unordered_set<std::size_t> ChangeTracker::getRoots() {
    std::lock_guard<std::mutex> lgChanges(changes_.mtx);
    return changes_.roots;
}

bool ChangeTracker::gotAdded(ChangeAddResult result) {
    return result == ChangeAddResult::SUCCESS || result == ChangeAddResult::ALREADY_EXISTING;
}

void ChangeTracker::logDetail(std::string content) {
#ifdef WITH_DETAILED_LOG
    logger_.pushLog(Log{std::format("      INTERNAL: {}", std::move(content))});
#endif
}