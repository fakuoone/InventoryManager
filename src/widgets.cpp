#include "userInterface/widgets.hpp"

namespace Widgets {
void DbTable::handleSplitterDrag(std::vector<float>& splitters, const std::size_t index) {
    float mouseX = ImGui::GetIO().MousePos.x - headerPos_.start.x;
    const float minRef = index <= 0 ? SPLITTER_MIN_DIST : splitters[index - 1] + SPLITTER_MIN_DIST;
    const float maxRef = index >= splitters.size() - 1 ? (headerPos_.end.x - headerPos_.start.x) : splitters[index + 1] - SPLITTER_MIN_DIST;
    // check if limits violated (splitter doesnt move other splitters)
    if (mouseX <= minRef || mouseX >= maxRef) { return; }
    splitters[index] = mouseX;
}

void DbTable::splitterRefit(std::vector<float>& splitters, const float space) {
    const float oldWidth = headerPos_.end.x - headerPos_.start.x;
    if (fabs(oldWidth - space) < 1e-3f) { return; }
    const std::size_t columnSize = splitters.size();
    if (space < (columnSize * SPLITTER_MIN_DIST + (columnSize - 1) * SPLITTER_WIDTH)) { return; }
    if (oldWidth < (columnSize * SPLITTER_MIN_DIST + (columnSize - 1) * SPLITTER_WIDTH)) { return; }
    for (std::size_t i = 0; i < columnSize; i++) {
        splitters[i] *= space / oldWidth;
    }
}

void DbTable::drawHeader(const std::string& tableName) {
    const auto& headers = dbData_->headers.at(tableName).data;
    auto& splitterPoss = columnWidths_.at(tableName);

    headerPos_.start = ImGui::GetCursorScreenPos();
    headerPos_.start.y += PAD_HEADER_Y;
    headerPos_.start.x += PAD_OUTER_X + LEFT_RESERVE;
    const float available = ImGui::GetContentRegionAvail().x - 2 * PAD_OUTER_X - LEFT_RESERVE - RIGHT_RESERVE;
    splitterRefit(splitterPoss, available);
    headerPos_.end = ImVec2(headerPos_.start.x + available, headerPos_.start.y + rowHeight_ + PAD_HEADER_Y);

    ImVec2 cursor = ImVec2(0, 0); // screen coordinates
    for (size_t i = 0; i < headers.size(); ++i) {
        float width = i > 0 ? splitterPoss[i] - splitterPoss[i - 1] - SPLITTER_WIDTH : splitterPoss[0] - 0.5 * SPLITTER_WIDTH;
        ImGui::PushID(headers[i].name.c_str());
        const CellInfo cellBoiler = CellInfo(headers[i], cursor, nullptr, width, true, false, false, false, i);
        EventTypes fromHeader = drawCellSC(
            cellBoiler,
            [this](const CellInfo& cb, const Rect& r, const std::string& v) -> ActionType { return drawHeaderCell(cb, r, v); },
            headers[i].name);
        if (fromHeader.mouse != MouseEventType::NONE && !lastEvent_.hasDataBesidesHover()) {
            lastEvent_.type = fromHeader;
            lastEvent_.origin = DataEvent(tableName, "", headers[i].name);
        }
        ImGui::PopID();

        cursor.x = splitterPoss[i] + 0.5 * SPLITTER_WIDTH;
        if (i + 1 < headers.size()) { drawSplitterSC(tableName, i, cursor.x); }
    }

    ImGui::Dummy(ImVec2(cursor.x, rowHeight_));
}

void DbTable::drawSplitterSC(const std::string& tableName, size_t index, float rightEdgeAbs) {
    const float rightEdge = rightEdgeAbs + headerPos_.start.x;
    const float leftEdge = rightEdge - SPLITTER_WIDTH;
    ImGui::SetCursorScreenPos(ImVec2(leftEdge, headerPos_.start.y));
    ImGui::InvisibleButton(("##splitter" + std::to_string(index)).c_str(), ImVec2(SPLITTER_WIDTH, rowHeight_));
    if (ImGui::IsItemActive()) { handleSplitterDrag(columnWidths_.at(tableName), index); }
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        drawList_->AddRectFilled(
            ImVec2(leftEdge, headerPos_.start.y), ImVec2(rightEdge, headerPos_.start.y + rowHeight_), IM_COL32(255, 255, 255, 150));
    }
}

void DbTable::drawColumns(const std::string& tableName) {
    const std::vector<float>& splitterPoss = columnWidths_.at(tableName);
    ImVec2 cursor = ImVec2(0, headerPos_.end.y - headerPos_.start.y);
    const auto& headers = dbData_->headers.at(tableName).data;

    drawUserInputRowFields(tableName, headers, splitterPoss, cursor);
    cursor.y += rowHeight_;
    cursor.x = 0;

    for (std::size_t i = 0; i < headers.size(); ++i) {
        drawColumn(tableName, i, splitterPoss, cursor);
        cursor.x = splitterPoss[i] + 0.5f * SPLITTER_WIDTH;
        cursor.y = headerPos_.end.y - headerPos_.start.y + rowHeight_;
    }
}

void DbTable::drawColumn(const std::string& tableName, const std::size_t i, const std::vector<float>& splitterPoss, ImVec2& cursor) {
    const HeaderInfo& headerInfo = dbData_->headers.at(tableName).data[i];
    ImGui::PushID(headerInfo.name.c_str());
    ImGui::PushID(static_cast<int>(i));
    std::size_t cellIndex = 0;

    for (const std::string& cell : dbData_->tableRows.at(tableName).at(headerInfo.name)) {
        const std::string pKey = dbData_->tableRows.at(tableName).at(dbData_->headers.at(tableName).pkey)[cellIndex];
        // pkey has to be integer
        if (!pKey.empty() && std::find_if(pKey.begin(), pKey.end(), [](unsigned char c) { return !std::isdigit(c); }) != pKey.end()) {
            continue;
        }
        const std::size_t pKeyId = static_cast<std::size_t>(std::stoi(pKey));
        rowChange_ = ChangeHelpers::getChangeOfRow(uiChanges_, tableName, pKeyId);
        const float width = i > 0 ? splitterPoss[i] - splitterPoss[i - 1] : splitterPoss[0] + 0.5f * SPLITTER_WIDTH;

        ImGui::PushID(static_cast<int>(cellIndex));

        // selection column
        if (i == 0) {
            // drawRowBackgroundIfNeeded(cursor, splitterPoss, rowChange.get());
            handleFirstColumnIfNeeded(tableName, headerInfo, pKey, cursor, rowChange_.get());
        }

        // data cell via celldrawer
        bool isUkeyAndHasParent = false;
        if (rowChange_) { isUkeyAndHasParent = headerInfo.type == DB::HeaderTypes::UNIQUE_KEY && rowChange_->hasParent(); }
        bool editable = edit_.whichId == pKeyId && headerInfo.type != DB::HeaderTypes::PRIMARY_KEY && !isUkeyAndHasParent;
        const CellInfo cellBoiler = CellInfo(headerInfo, cursor, rowChange_.get(), width, true, editable, false, false, i);

        EventTypes fromData = drawCellSC(
            cellBoiler,
            [this](const CellInfo& cellBoiler, const Rect& r, const std::string& v) -> ActionType {
                return drawDataCell(cellBoiler, r, v, CellType::DATA);
            },
            cell);
        if (fromData.mouse != MouseEventType::NONE || fromData.action == ActionType::EDIT) {
            if (fromData.action == ActionType::DATA && fromData.mouse == MouseEventType::CLICK) {
                // make cell editable by click
                fromData.action = ActionType::REQUEST_EDIT;
            }
            lastEvent_.type = fromData;
            lastEvent_.origin = Widgets::DataEvent(tableName, pKey, headerInfo.name);
        }

        // draw any insertion/change overlay for this cell (uses same rect as the cell)
        {
            ImVec2 min = ImVec2(cursor.x + headerPos_.start.x + PAD_INNER, cursor.y + headerPos_.start.y + PAD_INNER);
            ImVec2 max = ImVec2(min.x + width - PAD_INNER, min.y + rowHeight_ - PAD_INNER);
            Rect cellRect(min, max);
            drawList_->PushClipRect(min, max, true);
            drawChangeOverlayIfNeeded(rowChange_.get(), cell, headerInfo.name, cellRect, fromData);
            drawList_->PopClipRect();
        }

        // edit column
        if (i + 1 == dbData_->headers.at(tableName).data.size()) {
            handleLastActionIfNeeded(tableName, splitterPoss, i, cursor, rowChange_.get(), pKey);
        }

        cursor.y += rowHeight_;
        cellIndex++;
        ImGui::PopID();
    }

    // --- Render insertion changes (rows that don't exist in dbData but are in uiChanges)
    drawInsertionCellsOfColumn(tableName, headerInfo, i, splitterPoss, cursor, cellIndex);

    ImGui::PopID();
    ImGui::PopID();
}

void DbTable::drawUserInputRowFields(const std::string& tableName,
                                     const HeaderVector& headers,
                                     const std::vector<float>& splitterPoss,
                                     ImVec2& cursor) {
    ImGui::PushID("USERINPUT");
    if (edit_.insertBuffer.size() < headers.size()) { edit_.insertBuffer.resize(headers.size()); }
    Change::colValMap newChangeColVal{};

    for (std::size_t i = 0; i < headers.size(); ++i) {
        const HeaderInfo& headerInfo = headers[i];
        float width = i > 0 ? splitterPoss[i] - splitterPoss[i - 1] : splitterPoss[0] + 0.5f * SPLITTER_WIDTH;
        const CellInfo cellBoiler = CellInfo(headerInfo,
                                             cursor,
                                             nullptr,
                                             width,
                                             headerInfo.type != DB::HeaderTypes::PRIMARY_KEY,
                                             headerInfo.type != DB::HeaderTypes::PRIMARY_KEY,
                                             false,
                                             true,
                                             i);
        EventTypes inputEvent;
        ImGui::PushID(static_cast<int>(i));
        inputEvent =
            drawCellSC(cellBoiler, [this](const CellInfo& cell, const Rect& r) -> ActionType { return drawInsertionInputField(cell, r); });
        ImGui::PopID();

        ImGui::PushID("ENTER");
        if (i + 1 == headers.size()) {
            EventTypes lastColEnter = drawLastColumnEnter(cursor, splitterPoss, i);
            if (lastColEnter.mouse == MouseEventType::CLICK && !lastEvent_.hasDataBesidesHover()) {
                lastEvent_.cells = insertCells_;
                lastEvent_.type = lastColEnter;
                insertCells_.clear();
                for (auto& buffer : edit_.insertBuffer) {
                    std::memset(buffer.data(), 0, std::strlen(buffer.data()));
                }
            }
        }
        ImGui::PopID();

        cursor.x = splitterPoss[i] + 0.5f * SPLITTER_WIDTH;
    }
    ImGui::PopID();
}

// Helper: draw the first column cell (left reserved area) for the current row and update
// lastEvent
EventTypes DbTable::handleFirstColumnIfNeeded(
    const std::string& tableName, const HeaderInfo& header, const std::string& pKey, ImVec2& cursor, const Change* change) {
    EventTypes fromFirst;
    ImVec2 firstColumnStart = ImVec2(-LEFT_RESERVE, cursor.y);
    bool selected = false;
    if (change) {
        selected = change->isSelected();
        ImGui::PushID("FIRST");
        const CellInfo cellBoiler = CellInfo(header, firstColumnStart, change, LEFT_RESERVE, true, false, selected, false, INVALID_ID);
        fromFirst =
            drawCellSC(cellBoiler, [this](const CellInfo& cell, const Rect& r) -> ActionType { return drawFirstColumnSC(cell, r); });
        ImGui::PopID();
    }
    if (fromFirst.mouse != MouseEventType::NONE && !lastEvent_.hasDataBesidesHover()) {
        lastEvent_.type = fromFirst;
        if (change) {
            lastEvent_.origin = *change;
        } else {
            lastEvent_.origin = DataEvent(tableName, pKey, "FIRST");
        }
    }
    return fromFirst;
}

// Helper: draw the last/action column(s) for the current row and update lastEvent
EventTypes DbTable::handleLastActionIfNeeded(const std::string& tableName,
                                             const std::vector<float>& splitterPoss,
                                             const std::size_t columnIndex,
                                             ImVec2& cursor,
                                             const Change* change,
                                             const std::string& pKey) {
    EventTypes fromAction = drawActionColumn(cursor, splitterPoss, columnIndex, change);
    if (fromAction.mouse != MouseEventType::NONE && !lastEvent_.hasDataBesidesHover()) {
        lastEvent_.type = fromAction;
        if (change) {
            lastEvent_.origin = *change;
        } else {
            lastEvent_.origin = DataEvent(tableName, pKey, "LAST");
        }
    }
    return fromAction;
}

// Draw a translucent full-row background if this row is the highlighted one
void DbTable::drawRowBackgroundIfNeeded(const ImVec2& cursor, const std::vector<float>& splitterPoss, const Change* change) {
    if (!change) { return; }

    ImVec2 min = ImVec2(headerPos_.start.x + PAD_INNER, headerPos_.start.y + cursor.y + PAD_INNER);
    ImVec2 max = ImVec2(splitterPoss.back() + headerPos_.start.x + 0.5f * SPLITTER_WIDTH, min.y + rowHeight_ - PAD_INNER);
    // semi-transparent blue-ish highlight
    ImU32 bgCol = change->isValid() ? colValid.first : colInvalid.first;
    drawList_->AddRectFilled(min, max, bgCol);

    ImU32 borderCol = change->isValid() ? colValid.second : colInvalid.first; // adjust as needed
    drawList_->AddRect(min, max, borderCol, 0.0f, ~0, 1.0f);
}

void DbTable::drawInsertionCellsOfColumn(const std::string& tableName,
                                         const HeaderInfo& headerInfo,
                                         const std::size_t i,
                                         const std::vector<float>& splitterPoss,
                                         ImVec2& cursor,
                                         std::size_t& cellIndex) {
    if (!uiChanges_) { return; }
    if (!uiChanges_->idMappedChanges.contains(tableName)) { return; }

    for (const auto& [pKeyNum, changeKey] : uiChanges_->idMappedChanges.at(tableName)) {
        Change& change = uiChanges_->changes.at(changeKey);
        if (change.getType() != ChangeType::INSERT_ROW) { continue; }
        const std::string pKey = std::to_string(pKeyNum);

        float width = i > 0 ? splitterPoss[i] - splitterPoss[i - 1] : splitterPoss[0] + 0.5f * SPLITTER_WIDTH;
        ImGui::PushID(static_cast<int>(cellIndex));

        // first column for insertion row
        if (i == 0) {
            // drawRowBackgroundIfNeeded(cursor, splitterPoss, &change);
            handleFirstColumnIfNeeded(tableName, headerInfo, pKey, cursor, &change);
        }

        // data cell: use changed value if present, otherwise empty
        const std::string changedVal = change.getCell(headerInfo.name);
        bool isUkeyAndHasParent = headerInfo.type == DB::HeaderTypes::UNIQUE_KEY && change.hasParent();
        bool editable = edit_.whichId == pKeyNum && headerInfo.type != DB::HeaderTypes::PRIMARY_KEY && !isUkeyAndHasParent;
        const CellInfo cellBoiler = CellInfo(headerInfo, cursor, &change, width, true, editable, false, false, i);
        EventTypes fromData = drawCellSC(
            cellBoiler,
            [this](const CellInfo& cellBoiler, const Rect& r, const std::string& v) -> ActionType {
                return drawDataCell(cellBoiler, r, v, CellType::DATA);
            },
            changedVal);
        if (fromData.mouse != MouseEventType::NONE && !lastEvent_.hasDataBesidesHover()) {
            lastEvent_.type = fromData;
            lastEvent_.origin = Widgets::DataEvent(tableName, pKey, headerInfo.name);
        }

        // overlay for insertion (draw over cell)
        {
            ImVec2 min = ImVec2(cursor.x + headerPos_.start.x + PAD_INNER, cursor.y + headerPos_.start.y + PAD_INNER);
            ImVec2 max = ImVec2(min.x + width - PAD_INNER, min.y + rowHeight_ - PAD_INNER);
            Rect cellRect(min, max);
            drawChangeOverlayIfNeeded(&change, changedVal, headerInfo.name, cellRect, fromData);
        }

        // last/action column for insertion row
        if (i + 1 == dbData_->headers.at(tableName).data.size()) {
            handleLastActionIfNeeded(tableName, splitterPoss, i, cursor, &change, pKey);
        }

        cursor.y += rowHeight_;
        cellIndex++;
        ImGui::PopID();
    }
}

// Draw overlay for inserted/updated cell values for given Change
void DbTable::drawChangeOverlayIfNeeded(
    Change* ch, const std::string& originalValue, const std::string& headerName, const Rect& r, const EventTypes& event) {
    if (!ch) { return; }
    const std::string val = ch->getCell(headerName);
    if (val.empty()) { return; }

    // semi-transparent overlay to show changed/inserted value
    ImVec2 textSize = ImGui::CalcTextSize(val.c_str());
    float yOffset = ((r.end.y - r.start.y) - textSize.y) * 0.5f;
    if (yOffset < PAD_INNER_CONTENT) { yOffset = PAD_INNER_CONTENT; }
    ImVec2 textPos(r.start.x + PAD_INNER_CONTENT, r.start.y + yOffset);
    if (event.mouse == MouseEventType::HOVER) {
        drawList_->AddRectFilled(r.start, r.end, colHoveredGrey);
        drawList_->AddText(textPos, IM_COL32_WHITE, originalValue.c_str());
    }
}

EventTypes DbTable::drawLastColumnEnter(const ImVec2& pos, const std::vector<float>& splitterPoss, const std::size_t columnIndex) {
    EventTypes actionEvent;

    ImVec2 actionColumnStart = ImVec2(splitterPoss[columnIndex] + 0.5 * SPLITTER_WIDTH, pos.y);
    const CellInfo cellBoilerStart =
        CellInfo(HeaderInfo(), actionColumnStart, nullptr, RIGHT_RESERVE, true, false, false, false, INVALID_ID);
    actionEvent =
        drawCellSC(cellBoilerStart, [this](const CellInfo& cell, const Rect& r) -> ActionType { return drawActionColumnENTER(cell, r); });

    return actionEvent;
}

EventTypes
DbTable::drawActionColumn(const ImVec2& pos, const std::vector<float>& splitterPoss, const std::size_t columnIndex, const Change* change) {
    EventTypes actionEvent;
    bool enableDelete = true;
    bool enableUpdate = true;
    bool showEdit = true;
    if (change) {
        if (change->hasParent()) { enableDelete = false; }
        if (change->getType() == ChangeType::DELETE_ROW) {
            showEdit = false;
            enableUpdate = false;
        }
    }

    float individualWidth = showEdit ? RIGHT_RESERVE / 2 : RIGHT_RESERVE;
    ImVec2 actionColumnStart = ImVec2(splitterPoss[columnIndex] + 0.5 * SPLITTER_WIDTH, pos.y);
    ImGui::PushID("ACTIONX");
    const CellInfo cellBoilerStart =
        CellInfo(HeaderInfo(), actionColumnStart, change, individualWidth, enableDelete, false, false, false, INVALID_ID);
    actionEvent =
        drawCellSC(cellBoilerStart, [this](const CellInfo& cell, const Rect& r) -> ActionType { return drawActionColumnXSC(cell, r); });
    ImGui::PopID();

    if (showEdit) {
        ImVec2 actionColumn2nd = ImVec2(actionColumnStart.x + individualWidth, pos.y);
        ImGui::PushID("ACTIONED");
        const CellInfo cellBoiler =
            CellInfo(HeaderInfo(), actionColumn2nd, change, individualWidth, enableUpdate, false, false, false, INVALID_ID);
        EventTypes editEvent =
            drawCellSC(cellBoiler, [this](const CellInfo& cell, const Rect& r) -> ActionType { return drawActionColumnEditSC(cell, r); });
        if (actionEvent.mouse == MouseEventType::NONE) { actionEvent = editEvent; }
        ImGui::PopID();
    }
    return actionEvent;
}

ActionType DbTable::drawDataCell(const CellInfo& cell, const Rect& r, const std::string& value, CellType cellType) {
    ActionType action{ActionType::DATA};
    ImU32 colBg = cell.selected ? colSelected.first : colGreyBg;
    drawList_->PushClipRect(r.start, r.end, true);
    drawList_->AddRectFilled(r.start, r.end, colBg);

    ImVec2 textSize = ImGui::CalcTextSize(value.c_str());
    float yOffset = (r.end.y - r.start.y - textSize.y) * 0.5f;
    yOffset = std::max(yOffset, PAD_INNER_CONTENT);

    ImVec2 textPos{r.start.x + PAD_INNER_CONTENT, r.start.y + yOffset};

    if (cell.editable) {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
        ImGui::SetCursorScreenPos(textPos);
        ImGui::SetNextItemWidth(r.end.x - textPos.x - PAD_INNER_CONTENT);

        char* dataSource = cell.isInsert ? edit_.insertBuffer[cell.headerIndex].data() : edit_.editBuffer.data();
        if (!cell.isInsert && edit_.whichId != cell.headerIndex) { std::snprintf(dataSource, UI::BUFFER_SIZE, "%s", value.c_str()); }

        bool enterPressed = ImGui::InputText("##edit", dataSource, UI::BUFFER_SIZE, ImGuiInputTextFlags_EnterReturnsTrue);
        if (enterPressed || ImGui::IsItemDeactivatedAfterEdit()) {
            if (cell.isInsert) {
                insertCells_.emplace(cell.header.name, std::string(dataSource));
            } else {
                Change::colValMap newChangeColVal{{cell.header.name, std::string(dataSource)}};
                lastEvent_.cells = std::move(newChangeColVal);
                edit_.whichId = INVALID_ID;
                action = ActionType::EDIT;
            }
        }
        ImGui::PopStyleVar();
    } else {
        ImU32 col = cell.enabled ? IM_COL32_WHITE : IM_COL32(255, 255, 255, 100);
        drawList_->AddText(textPos, col, value.c_str());
        drawChangeInCell(cell, r, textPos, col, value);

        if (cellType == CellType::HEADER) {
            std::string depth = std::to_string(cell.header.depth);
            drawList_->AddText(ImVec2{r.end.x - ImGui::CalcTextSize(depth.c_str()).x, r.start.y}, col, depth.c_str());
        }
    }

    drawList_->PopClipRect();
    return action;
}

void DbTable::drawChangeInCell(const CellInfo& cell, const Rect& r, ImVec2 textPos, ImU32 col, const std::string& value) {
    if (!cell.change || cell.selected) { return; }
    if (cell.header.type == DB::HeaderTypes::PRIMARY_KEY) { return; }
    bool isValid = cell.change->isLocallyValid();
    if (!isValid) {
        // nullables, references to valid changes (1 level deep is enough, otherwise performance...)
        if (!cell.change->hasChildren()) {
            isValid = cell.header.nullable || !value.empty();
        } else {
            for (const std::size_t key : cell.change->getChildren()) {
                const Change& child = uiChanges_.get()->changes.at(key);
                if (child.getTable() == cell.header.name) {
                    isValid = child.isLocallyValid();
                    break;
                }
            }
        }
    }

    std::pair<ImU32, ImU32> changeCols = isValid ? colValid : colInvalid;
    switch (cell.change->getType()) {
    case ChangeType::DELETE_ROW:
        drawList_->AddLine(ImVec2{r.start.x, r.start.y + (r.end.y - r.start.y) / 2},
                           ImVec2{r.end.x, r.start.y + (r.end.y - r.start.y) / 2},
                           ImU32(colInvalid.second));
        break;

    case ChangeType::UPDATE_CELLS:
        [[fallthrough]];
    case ChangeType::INSERT_ROW:
        if (!cell.change->getCell(cell.header.name).empty() || value.empty()) {
            drawList_->AddRectFilled(r.start, r.end, changeCols.first);
            drawList_->AddRect(r.start, r.end, changeCols.second);
            drawList_->AddText(textPos, col, cell.change->getCell(cell.header.name).c_str());
        }
    default:
        break;
    }
}

ActionType DbTable::drawHeaderCell(const CellInfo& cell, const Rect& r, const std::string& header) {
    drawDataCell(cell, r, header, CellType::HEADER);
    return ActionType::HEADER;
}

ActionType DbTable::drawActionColumnXSC(const CellInfo& cell, const Rect& r) {
    drawDataCell(cell, r, "X", CellType::ACTION_COLUMN);
    return ActionType::REMOVE;
}

ActionType DbTable::drawActionColumnENTER(const CellInfo& cell, const Rect& r) {
    drawDataCell(cell, r, "ENTER", CellType::ACTION_COLUMN);
    return ActionType::INSERT;
}

ActionType DbTable::drawActionColumnEditSC(const CellInfo& cell, const Rect& r) {
    drawDataCell(cell, r, "ED", CellType::ACTION_COLUMN);
    return ActionType::REQUEST_EDIT;
}

ActionType DbTable::drawFirstColumnSC(const CellInfo& cell, const Rect& r) {
    drawDataCell(cell, r, "^", CellType::SELECTION);
    return ActionType::SELECT;
}

ActionType DbTable::drawInsertionInputField(const CellInfo& cell, const Rect& r) {
    return drawDataCell(cell, r, "", CellType::DATA);
}

void DbTable::drawTable(const std::string& tableName) {
    if (rowHeight_ == 0) { rowHeight_ = ImGui::CalcTextSize("test").y + (PAD_INNER + PAD_INNER_CONTENT) * 2; }
    drawList_ = ImGui::GetWindowDrawList();
    ImGui::PushID(tableName.c_str());
    drawHeader(tableName);
    drawColumns(tableName);
    ImGui::PopID();
}

void DbTable::setData(std::shared_ptr<const CompleteDbData> newData) {
    dbData_ = newData;
    for (const auto& [tableName, tableInfo] : dbData_->headers) {
        std::size_t colCount = tableInfo.data.size();
        float widthPerColumn = (ImGui::GetContentRegionAvail().x - 2 * PAD_OUTER_X - LEFT_RESERVE - RIGHT_RESERVE) / (float)colCount;
        columnWidths_[tableName].clear();
        for (std::size_t i = 0; i < colCount; i++) {
            columnWidths_[tableName].push_back((i + 1) * widthPerColumn);
        }
    }
}

void DbTable::setChangeData(std::shared_ptr<uiChangeInfo> changeData) {
    uiChanges_ = changeData;
}

Event DbTable::getEvent() const {
    return lastEvent_;
}

void DbTable::popEvent() {
    lastEvent_ = Event();
}

void ChangeOverviewer::setChangeData(std::shared_ptr<uiChangeInfo> changeData) {
    uiChanges = changeData;
}

MouseEventType ChangeOverviewer::drawSingleChangeOverview(const Change& change,
                                                          std::size_t* visualDepth,
                                                          const std::size_t parent,
                                                          bool isChildrenNotLast) {
    MouseEventType event(MouseEventType::NONE);
    const uint32_t rowId = change.getRowId();
    const std::size_t uid = change.getKey();
    // const std::vector<std::size_t> children = change.getChildren();
    const bool selected = changeTracker.isChangeSelected(uid);
    constexpr float INDENTATION_WIDTH = 8.0f;

    const char* type = "UNKNOWN";
    switch (change.getType()) {
    case ChangeType::NONE:
        type = "NONE";
        break;
    case ChangeType::DELETE_ROW:
        type = "DELETE";
        break;
    case ChangeType::INSERT_ROW:
        type = "INSERT";
        break;
    case ChangeType::UPDATE_CELLS:
        type = "UPDATE";
        break;
    }

    ImGui::PushID(static_cast<int>(parent));
    ImGui::PushID(static_cast<int>(rowId));
    ImGui::PushID(static_cast<int>(*visualDepth));

    ImU32 bgCol = change.isValid() ? colValid.first : colInvalid.first;
    ImU32 borderCol = change.isValid() ? colValid.second : colInvalid.second;

    if (changeHighlight.contains(change.getKey())) {
        childSelectTimer -= ImGui::GetIO().DeltaTime;
        if (childSelectTimer < 0) {
            changeHighlight.erase(change.getKey());
            childSelectTimer = 0;
        }
        bgCol = colSelected.first;
        borderCol = colSelected.second;
    }

    const float width = ImGui::GetContentRegionAvail().x - *visualDepth * INDENTATION_WIDTH;
    ImVec2 startPos = ImGui::GetCursorScreenPos();
    startPos.x += *visualDepth * INDENTATION_WIDTH;

    const float remainingWidth = width - (UID_COL + TYPE_COL + ROW_COL + HPADDING * 2.0f);
    const float childrenWidth = 0;
    const float remainingTextWidth = remainingWidth - childrenWidth;

    const std::string summary = change.getCellSummary(60);
    ImVec2 summarySize = ImGui::CalcTextSize(summary.c_str(), nullptr, false, remainingTextWidth);

    const float rowHeight = std::max(ImGui::GetFrameHeight(), summarySize.y) + VPADDING_INT * 2.0f;

    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton("##change_row", ImVec2(width, rowHeight));
    const bool hovered = ImGui::IsItemHovered();
    const bool clicked = ImGui::IsItemClicked();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 min = startPos;
    ImVec2 max = {startPos.x + width, startPos.y + rowHeight};

    dl->AddRectFilled(min, max, bgCol);

    if (selected) { dl->AddRect(min, max, borderCol); }
    if (hovered) { dl->AddRect(min, max, IM_COL32(255, 255, 255, 60)); }

    ImGui::SetCursorScreenPos({startPos.x + HPADDING, startPos.y + HPADDING});
    ImGui::AlignTextToFramePadding();

    // UID
    ImGui::Text("%zu", uid);
    ImGui::SameLine(startPos.x + HPADDING + UID_COL);

    // Type
    ImGui::TextUnformatted(type);
    ImGui::SameLine(startPos.x + HPADDING + UID_COL + TYPE_COL);

    // Summary
    if ((remainingTextWidth - summarySize.x) > 0) {
        ImGui::PushTextWrapPos(startPos.x + HPADDING + UID_COL + TYPE_COL + remainingTextWidth);
        ImGui::TextUnformatted(summary.c_str());
        ImGui::PopTextWrapPos();
    }

    ImGui::SameLine(startPos.x + HPADDING + UID_COL + TYPE_COL + summarySize.x);
    if (clicked) {
        // changeTracker.toggleChangeSelect(imGuiKeyId);
        event = MouseEventType::CLICK;
    }

    // Row ID (right aligned)
    ImGui::SameLine(startPos.x + width - ROW_COL);
    ImGui::Text("Row %u", rowId);

    ImGui::PopID();
    ImGui::PopID();
    ImGui::PopID();

    // padding
    ImVec2 end = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos({end.x, max.y});
    if (isChildrenNotLast) {
        ImGui::Dummy(ImVec2(0.0f, 0.25f));
    } else {
        ImGui::Dummy(ImVec2(0.0f, VPADDING));
    }

    return event;
}
} // namespace Widgets