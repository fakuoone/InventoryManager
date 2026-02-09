#include "userInterface/widgets.hpp"

namespace Widgets {
void DbTable::handleSplitterDrag(std::vector<float>& splitters, const std::size_t index) {
    float mouseX = ImGui::GetIO().MousePos.x - headerPos.start.x;
    const float minRef = index <= 0 ? SPLITTER_MIN_DIST : splitters[index - 1] + SPLITTER_MIN_DIST;
    const float maxRef = index >= splitters.size() - 1 ? (headerPos.end.x - headerPos.start.x) : splitters[index + 1] - SPLITTER_MIN_DIST;
    // check if limits violated (splitter doesnt move other splitters)
    if (mouseX <= minRef || mouseX >= maxRef) {
        return;
    }
    splitters[index] = mouseX;
}

void DbTable::splitterRefit(std::vector<float>& splitters, const float space) {
    const float oldWidth = headerPos.end.x - headerPos.start.x;
    if (fabs(oldWidth - space) < 1e-3f) {
        return;
    }
    const std::size_t columnSize = splitters.size();
    if (space < (columnSize * SPLITTER_MIN_DIST + (columnSize - 1) * SPLITTER_WIDTH)) {
        return;
    }
    if (oldWidth < (columnSize * SPLITTER_MIN_DIST + (columnSize - 1) * SPLITTER_WIDTH)) {
        return;
    }
    for (std::size_t i = 0; i < columnSize; i++) {
        splitters[i] *= space / oldWidth;
    }
}

void DbTable::drawHeader(const std::string& tableName) {
    const auto& headers = dbData->headers.at(tableName).data;
    auto& splitterPoss = columnWidths.at(tableName);

    headerPos.start = ImGui::GetCursorScreenPos();
    headerPos.start.y += PAD_HEADER_Y;
    headerPos.start.x += PAD_OUTER_X + LEFT_RESERVE;
    const float available = ImGui::GetContentRegionAvail().x - 2 * PAD_OUTER_X - LEFT_RESERVE - RIGHT_RESERVE;
    splitterRefit(splitterPoss, available);
    headerPos.end = ImVec2(headerPos.start.x + available, headerPos.start.y + rowHeight + PAD_HEADER_Y);

    ImVec2 cursor = ImVec2(0, 0); // screen coordinates
    for (size_t i = 0; i < headers.size(); ++i) {
        float width = i > 0 ? splitterPoss[i] - splitterPoss[i - 1] - SPLITTER_WIDTH : splitterPoss[0] - 0.5 * SPLITTER_WIDTH;
        ImGui::PushID(headers[i].name.c_str());
        const CellBoilerPlate cellBoiler = CellBoilerPlate(headers[i].name, cursor, width, true, false, false, false, i);
        eventTypes fromHeader = drawCellSC(
            cellBoiler,
            [this](const CellBoilerPlate& cb, const Rect& r, const std::string& v) -> ACTION_TYPE { return drawHeaderCell(cb, r, v); },
            headers[i].name);
        if (fromHeader.mouse != MOUSE_EVENT_TYPE::NONE) {
            lastEvent.type = fromHeader;
            lastEvent.origin = DataEvent(tableName, "", headers[i].name);
        }
        ImGui::PopID();

        cursor.x = splitterPoss[i] + 0.5 * SPLITTER_WIDTH;
        if (i + 1 < headers.size()) {
            drawSplitterSC(tableName, i, cursor.x);
        }
    }

    ImGui::Dummy(ImVec2(cursor.x, rowHeight));
}

void DbTable::drawSplitterSC(const std::string& tableName, size_t index, float rightEdgeAbs) {
    const float rightEdge = rightEdgeAbs + headerPos.start.x;
    const float leftEdge = rightEdge - SPLITTER_WIDTH;
    ImGui::SetCursorScreenPos(ImVec2(leftEdge, headerPos.start.y));
    ImGui::InvisibleButton(("##splitter" + std::to_string(index)).c_str(), ImVec2(SPLITTER_WIDTH, rowHeight));
    if (ImGui::IsItemActive()) {
        handleSplitterDrag(columnWidths.at(tableName), index);
    }
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        drawList->AddRectFilled(
            ImVec2(leftEdge, headerPos.start.y), ImVec2(rightEdge, headerPos.start.y + rowHeight), IM_COL32(255, 255, 255, 150));
    }
}

void DbTable::drawColumns(const std::string& tableName) {
    const std::vector<float>& splitterPoss = columnWidths.at(tableName);
    ImVec2 cursor = ImVec2(0, headerPos.end.y - headerPos.start.y);
    const auto& headers = dbData->headers.at(tableName).data;

    drawUserInputRowFields(tableName, headers, splitterPoss, cursor);
    cursor.y += rowHeight;
    cursor.x = 0;

    for (std::size_t i = 0; i < headers.size(); ++i) {
        drawColumn(tableName,
                   i,
                   splitterPoss,
                   cursor,
                   [this](const CellBoilerPlate& cellBoiler, const Rect& r, const std::string& v) -> ACTION_TYPE {
                       return drawDataCell(cellBoiler, r, v);
                   });
        cursor.x = splitterPoss[i] + 0.5f * SPLITTER_WIDTH;
        cursor.y = headerPos.end.y - headerPos.start.y + rowHeight;
    }
}

void DbTable::drawUserInputRowFields(const std::string& tableName,
                                     const tHeaderVector& headers,
                                     const std::vector<float>& splitterPoss,
                                     ImVec2& cursor) {
    ImGui::PushID("USERINPUT");
    if (edit.insertBuffer.size() < headers.size()) {
        edit.insertBuffer.resize(headers.size());
    }
    Change::colValMap newChangeColVal{};

    for (std::size_t i = 0; i < headers.size(); ++i) {
        const tHeaderInfo& headerInfo = headers[i];
        float width = i > 0 ? splitterPoss[i] - splitterPoss[i - 1] : splitterPoss[0] + 0.5f * SPLITTER_WIDTH;
        const CellBoilerPlate cellBoiler = CellBoilerPlate(headerInfo.name,
                                                           cursor,
                                                           width,
                                                           headerInfo.type != headerType::PRIMARY_KEY,
                                                           headerInfo.type != headerType::PRIMARY_KEY,
                                                           false,
                                                           true,
                                                           i);
        eventTypes inputEvent;
        ImGui::PushID(static_cast<int>(i));
        inputEvent = drawCellSC(
            cellBoiler, [this](const CellBoilerPlate& cell, const Rect& r) -> ACTION_TYPE { return drawInsertionInputField(cell, r); });
        ImGui::PopID();

        ImGui::PushID("ENTER");
        if (i + 1 == headers.size()) {
            eventTypes lastColEnter = drawLastColumnEnter(cursor, splitterPoss, i);
            if (lastColEnter.mouse == MOUSE_EVENT_TYPE::CLICK) {
                lastEvent.cells = insertCells;
                lastEvent.type = lastColEnter;
                insertCells.clear();
                for (auto& buffer : edit.insertBuffer) {
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
eventTypes DbTable::handleFirstColumnIfNeeded(
    const std::string& tableName, const std::string& headerName, const std::string& pKey, ImVec2& cursor, const Change* change) {
    eventTypes fromFirst;
    ImVec2 firstColumnStart = ImVec2(-LEFT_RESERVE, cursor.y);
    bool selected = false;
    if (change) {
        selected = change->isSelected();
        ImGui::PushID("FIRST");
        const CellBoilerPlate cellBoiler =
            CellBoilerPlate(headerName, firstColumnStart, LEFT_RESERVE, true, false, selected, false, INVALID_ID);
        fromFirst = drawCellSC(cellBoiler,
                               [this](const CellBoilerPlate& cell, const Rect& r) -> ACTION_TYPE { return drawFirstColumnSC(cell, r); });
        ImGui::PopID();
    }
    if (fromFirst.mouse != MOUSE_EVENT_TYPE::NONE) {
        lastEvent.type = fromFirst;
        if (change) {
            lastEvent.origin = *change;
        } else {
            lastEvent.origin = DataEvent(tableName, pKey, "FIRST");
        }
    }
    return fromFirst;
}

// Helper: draw the last/action column(s) for the current row and update lastEvent
eventTypes DbTable::handleLastActionIfNeeded(const std::string& tableName,
                                             const std::vector<float>& splitterPoss,
                                             const std::size_t columnIndex,
                                             ImVec2& cursor,
                                             const Change* change,
                                             const std::string& pKey) {
    eventTypes fromAction = drawActionColumn(cursor, splitterPoss, columnIndex, change);
    if (fromAction.mouse != MOUSE_EVENT_TYPE::NONE) {
        lastEvent.type = fromAction;
        if (change) {
            lastEvent.origin = *change;
        } else {
            lastEvent.origin = DataEvent(tableName, pKey, "LAST");
        }
    }
    return fromAction;
}

// Draw a translucent full-row background if this row is the highlighted one
void DbTable::drawRowBackgroundIfNeeded(const ImVec2& cursor, const std::vector<float>& splitterPoss, const Change* change) {
    if (!change) {
        return;
    }

    ImVec2 min = ImVec2(headerPos.start.x + PAD_INNER, headerPos.start.y + cursor.y + PAD_INNER);
    ImVec2 max = ImVec2(splitterPoss.back() + headerPos.start.x + 0.5f * SPLITTER_WIDTH, min.y + rowHeight - PAD_INNER);
    // semi-transparent blue-ish highlight
    ImU32 bgCol = change->isValid() ? colValid.first : colInvalid.first;
    drawList->AddRectFilled(min, max, bgCol);

    ImU32 borderCol = change->isValid() ? colValid.second : colInvalid.first; // adjust as needed
    drawList->AddRect(min, max, borderCol, 0.0f, ~0, 1.0f);
}

// Draw overlay for inserted/updated cell values for given Change
void DbTable::drawChangeOverlayIfNeeded(Change* ch, const std::string& headerName, const Rect& r) {
    if (!ch) {
        return;
    }
    const std::string val = ch->getCell(headerName);
    if (val.empty()) {
        return;
    }

    // semi-transparent overlay to show changed/inserted value
    ImVec2 textSize = ImGui::CalcTextSize(val.c_str());
    float yOffset = ((r.end.y - r.start.y) - textSize.y) * 0.5f;
    if (yOffset < PAD_INNER_CONTENT) {
        yOffset = PAD_INNER_CONTENT;
    }
    ImVec2 textPos(r.start.x + PAD_INNER_CONTENT, r.start.y + yOffset);
    drawList->AddText(textPos, colSelected.second, val.c_str());
}

eventTypes DbTable::drawLastColumnEnter(const ImVec2& pos, const std::vector<float>& splitterPoss, const std::size_t columnIndex) {
    eventTypes actionEvent;

    ImVec2 actionColumnStart = ImVec2(splitterPoss[columnIndex] + 0.5 * SPLITTER_WIDTH, pos.y);
    const CellBoilerPlate cellBoilerStart =
        CellBoilerPlate("LAST", actionColumnStart, RIGHT_RESERVE, true, false, false, false, INVALID_ID);
    actionEvent = drawCellSC(cellBoilerStart,
                             [this](const CellBoilerPlate& cell, const Rect& r) -> ACTION_TYPE { return drawActionColumnENTER(cell, r); });

    return actionEvent;
}

eventTypes
DbTable::drawActionColumn(const ImVec2& pos, const std::vector<float>& splitterPoss, const std::size_t columnIndex, const Change* change) {
    eventTypes actionEvent;
    bool enableDelete = true;
    bool enableUpdate = true;
    bool showEdit = true;
    if (change) {
        if (change->hasParent()) {
            enableDelete = false;
        }
        if (change->getType() == changeType::DELETE_ROW) {
            showEdit = false;
            enableUpdate = false;
        }
    }

    float individualWidth = showEdit ? RIGHT_RESERVE / 2 : RIGHT_RESERVE;
    ImVec2 actionColumnStart = ImVec2(splitterPoss[columnIndex] + 0.5 * SPLITTER_WIDTH, pos.y);
    ImGui::PushID("ACTIONX");
    const CellBoilerPlate cellBoilerStart =
        CellBoilerPlate("LAST", actionColumnStart, individualWidth, enableDelete, false, false, false, INVALID_ID);
    actionEvent = drawCellSC(cellBoilerStart,
                             [this](const CellBoilerPlate& cell, const Rect& r) -> ACTION_TYPE { return drawActionColumnXSC(cell, r); });
    ImGui::PopID();

    if (showEdit) {
        ImVec2 actionColumn2nd = ImVec2(actionColumnStart.x + individualWidth, pos.y);
        ImGui::PushID("ACTIONED");
        const CellBoilerPlate cellBoiler =
            CellBoilerPlate("LAST", actionColumn2nd, individualWidth, enableUpdate, false, false, false, INVALID_ID);
        eventTypes editEvent = drawCellSC(
            cellBoiler, [this](const CellBoilerPlate& cell, const Rect& r) -> ACTION_TYPE { return drawActionColumnEditSC(cell, r); });
        if (actionEvent.mouse == MOUSE_EVENT_TYPE::NONE) {
            actionEvent = editEvent;
        }
        ImGui::PopID();
    }
    return actionEvent;
}

ACTION_TYPE DbTable::drawDataCell(const CellBoilerPlate& cell, const Rect& r, const std::string& value) {
    ACTION_TYPE action{ACTION_TYPE::DATA};
    ImU32 colBg = cell.selected ? colSelected.first : colGreyBg;
    drawList->PushClipRect(r.start, r.end, true);
    drawList->AddRectFilled(r.start, r.end, colBg);

    ImVec2 textSize = ImGui::CalcTextSize(value.c_str());
    float yOffset = (r.end.y - r.start.y - textSize.y) * 0.5f;
    yOffset = std::max(yOffset, PAD_INNER_CONTENT);

    ImVec2 textPos{r.start.x + PAD_INNER_CONTENT, r.start.y + yOffset};

    if (cell.editable) {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
        ImGui::SetCursorScreenPos(textPos);
        ImGui::SetNextItemWidth(r.end.x - textPos.x - PAD_INNER_CONTENT);

        char* dataSource = cell.isInsert ? edit.insertBuffer[cell.headerIndex].data() : edit.editBuffer.data();
        if (!cell.isInsert) {
            std::snprintf(dataSource, BUFFER_SIZE, "%s", value.c_str());
        }

        bool enterPressed = ImGui::InputText("##edit", dataSource, BUFFER_SIZE, ImGuiInputTextFlags_EnterReturnsTrue);
        if (enterPressed || ImGui::IsItemDeactivatedAfterEdit()) {
            if (cell.isInsert) {
                insertCells.emplace(cell.headerName, std::string(dataSource));
            } else {
                Change::colValMap newChangeColVal{{cell.headerName, std::string(dataSource)}};
                lastEvent.cells = std::move(newChangeColVal);
                edit.whichId = INVALID_ID;
                action = ACTION_TYPE::EDIT;
            }
        }
        ImGui::PopStyleVar();
    } else {
        ImU32 col = cell.enabled ? IM_COL32_WHITE : IM_COL32(255, 255, 255, 100);
        drawList->AddText(textPos, col, value.c_str());
    }

    drawList->PopClipRect();
    return action;
}

ACTION_TYPE DbTable::drawHeaderCell(const CellBoilerPlate& cell, const Rect& r, const std::string& header) {
    drawDataCell(cell, r, header);
    return ACTION_TYPE::HEADER;
}

ACTION_TYPE DbTable::drawActionColumnXSC(const CellBoilerPlate& cell, const Rect& r) {
    drawDataCell(cell, r, "X");
    return ACTION_TYPE::REMOVE;
}

ACTION_TYPE DbTable::drawActionColumnENTER(const CellBoilerPlate& cell, const Rect& r) {
    drawDataCell(cell, r, "ENTER");
    return ACTION_TYPE::INSERT;
}

ACTION_TYPE DbTable::drawActionColumnEditSC(const CellBoilerPlate& cell, const Rect& r) {
    drawDataCell(cell, r, "ED");
    return ACTION_TYPE::REQUEST_EDIT;
}

ACTION_TYPE DbTable::drawFirstColumnSC(const CellBoilerPlate& cell, const Rect& r) {
    drawDataCell(cell, r, "^");
    return ACTION_TYPE::SELECT;
}

ACTION_TYPE DbTable::drawInsertionInputField(const CellBoilerPlate& cell, const Rect& r) {
    return drawDataCell(cell, r, "");
}

void DbTable::drawTable(const std::string& tableName) {
    if (rowHeight == 0) {
        rowHeight = ImGui::CalcTextSize("test").y + (PAD_INNER + PAD_INNER_CONTENT) * 2;
    }
    drawList = ImGui::GetWindowDrawList();
    ImGui::PushID(tableName.c_str());
    drawHeader(tableName);
    drawColumns(tableName);
    ImGui::PopID();
}

void DbTable::setData(std::shared_ptr<const completeDbData> newData) {
    dbData = newData;
    for (const auto& [tableName, tableInfo] : dbData->headers) {
        std::size_t colCount = tableInfo.data.size();
        float widthPerColumn = (ImGui::GetContentRegionAvail().x - 2 * PAD_OUTER_X - LEFT_RESERVE - RIGHT_RESERVE) / (float)colCount;
        columnWidths[tableName].clear();
        for (std::size_t i = 0; i < colCount; i++) {
            columnWidths[tableName].push_back((i + 1) * widthPerColumn);
        }
    }
}

void DbTable::setChangeData(std::shared_ptr<uiChangeInfo> changeData) {
    uiChanges = changeData;
}

Event DbTable::getEvent() const {
    return lastEvent;
}

void DbTable::popEvent() {
    lastEvent = Event();
}

void ChangeOverviewer::setChangeData(std::shared_ptr<uiChangeInfo> changeData) {
    uiChanges = changeData;
}

// ChangeOverviewer
bool ChangeOverviewer::drawChildren(const std::vector<std::size_t>& children, float allowedWidth) {
    // TODO: Draw parent aswell
    // TODO: give a label to unclear data (children:)
    // TODO: fix bottom padding when text is expanded
    // TODO: fix rowid to always be visible (gets pushed out of sight when not enough space)
    const float childHeight = ImGui::GetFrameHeight();
    uint16_t drawableCount = allowedWidth / (childWidth + hPadding);
    uint16_t count = std::min<uint16_t>(drawableCount, (uint16_t)children.size());
    ImVec2 startPos = ImGui::GetCursorPos();
    startPos.x += hPadding;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    bool clicked = false;

    for (uint16_t i = 0; i < count; i++) {
        const ImVec2 childPos = {startPos.x + i * (childWidth + hPadding), startPos.y};
        ImGui::SetCursorPos(childPos);
        ImGui::InvisibleButton(("##child_" + std::to_string(children[i])).c_str(), ImVec2(childWidth, childHeight));
        const bool hovered = ImGui::IsItemHovered();
        const bool localClicked = ImGui::IsItemClicked();
        clicked = clicked | localClicked;

        // ---- Background
        ImU32 bg = hovered ? IM_COL32(80, 80, 80, 160) : IM_COL32(60, 60, 60, 120);

        dl->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), bg, 0.0f);

        // TODO: Beim scrollen verschiebt sich der Text nicht.
        // ---- Centered text
        const std::string label = std::to_string(children[i]);
        ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
        ImVec2 textPos = {childPos.x + (childWidth - textSize.x) * 0.5f, childPos.y + (childHeight - textSize.y) * 0.5f};
        dl->AddText(textPos, IM_COL32_WHITE, label.c_str());

        // ---- Click handling
        if (localClicked) {
            childSelectTimer = 1.0f;
            selectedTable = uiChanges->changes.at(children[i]).getTable();
            changeHighlight.emplace(children[i]);
        }
    }

    // ---- Ellipsis if truncated
    if (count < children.size()) {
        ImGui::SetCursorPos({startPos.x + count * (childWidth + hPadding), startPos.y});
        ImGui::TextUnformatted("...");
    }

    // ---- Advance cursor once
    ImGui::SetCursorPosY(startPos.y + childHeight);
    return clicked;
}

MOUSE_EVENT_TYPE ChangeOverviewer::drawSingleChangeOverview(const Change& change) {
    MOUSE_EVENT_TYPE event(MOUSE_EVENT_TYPE::NONE);
    const uint32_t rowId = change.getRowId();
    const std::size_t uid = change.getKey();
    const std::vector<std::size_t> children = change.getChildren();
    const bool selected = changeTracker.isChangeSelected(uid);

    // const ImGuiID imGuiKeyId = static_cast<ImGuiID>(uid);

    const char* type = "UNKNOWN";
    switch (change.getType()) {
    case changeType::NONE:
        type = "NONE";
        break;
    case changeType::DELETE_ROW:
        type = "DELETE";
        break;
    case changeType::INSERT_ROW:
        type = "INSERT";
        break;
    case changeType::UPDATE_CELLS:
        type = "UPDATE";
        break;
    }

    ImGui::PushID(static_cast<int>(rowId));

    ImU32 bgCol = change.isValid() ? colValid.first : colInvalid.first;
    ImU32 borderCol = change.isValid() ? colValid.second : colInvalid.second;
    // make yellow
    if (changeHighlight.contains(change.getKey())) {
        childSelectTimer -= ImGui::GetIO().DeltaTime;
        if (childSelectTimer < 0) {
            changeHighlight.erase(change.getKey());
            childSelectTimer = 0;
        }
        bgCol = colSelected.first;
        borderCol = colSelected.second;
    }

    // ---- Full width
    const float width = ImGui::GetContentRegionAvail().x;
    ImVec2 startPos = ImGui::GetCursorScreenPos();

    // Calculate widths
    const float remainingWidth = width - (UID_COL + TYPE_COL + ROW_COL + HPADDING * 2.0f);
    const uint16_t maxChildren = static_cast<uint16_t>(remainingWidth / (childWidth + HPADDING));
    const uint16_t visibleChildren = std::min<uint16_t>(maxChildren, children.size());
    const float childrenWidth = visibleChildren * (childWidth + HPADDING);
    const float remainingTextWidth = remainingWidth - childrenWidth;

    const std::string summary = change.getCellSummary(60);
    ImVec2 summarySize = ImGui::CalcTextSize(summary.c_str(), nullptr, false, remainingTextWidth);

    // ---- Dynamic row height
    const float rowHeight = std::max(ImGui::GetFrameHeight(), summarySize.y) + VPADDING_INT * 2.0f;

    // ---- Interaction + layout
    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton("##change_row", ImVec2(width, rowHeight));
    const bool hovered = ImGui::IsItemHovered();
    const bool clicked = ImGui::IsItemClicked();

    // ---- Draw background
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 min = startPos;
    ImVec2 max = {startPos.x + width, startPos.y + rowHeight};

    dl->AddRectFilled(min, max, bgCol);

    if (selected) {
        dl->AddRect(min, max, borderCol);
    }
    if (hovered) {
        dl->AddRect(min, max, IM_COL32(255, 255, 255, 60));
    }

    // ---- Draw content (single row, wrapped summary)
    ImGui::SetCursorScreenPos({startPos.x + HPADDING, startPos.y + HPADDING});

    ImGui::AlignTextToFramePadding();

    // UID
    ImGui::Text("%zu", uid);
    ImGui::SameLine(UID_COL);

    // Type
    ImGui::TextUnformatted(type);
    ImGui::SameLine(UID_COL + TYPE_COL);

    // Summary
    if ((remainingTextWidth - summarySize.x) > 0) {
        ImGui::PushTextWrapPos(startPos.x + HPADDING + UID_COL + TYPE_COL + remainingTextWidth);
        ImGui::TextUnformatted(summary.c_str());
        ImGui::PopTextWrapPos();
    }

    // children
    ImGui::SameLine(UID_COL + TYPE_COL + summarySize.x);
    bool childClicked = drawChildren(children, childrenWidth);

    if (clicked && !childClicked) {
        // changeTracker.toggleChangeSelect(imGuiKeyId);
        event = MOUSE_EVENT_TYPE::CLICK;
    }

    // Row ID (right aligned)
    ImGui::SameLine(startPos.x + width - ROW_COL);
    ImGui::Text("Row %u", rowId);

    ImGui::PopID();

    // padding
    ImVec2 end = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos({end.x, max.y});
    ImGui::Dummy(ImVec2(0.0f, VPADDING));
    return event;
}
} // namespace Widgets