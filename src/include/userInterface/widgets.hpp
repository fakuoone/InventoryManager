#pragma once

#include "change.hpp"
#include "logger.hpp"

#include <unordered_set>
#include <variant>
#include <optional>

constexpr const std::size_t BUFFER_SIZE = 256;

struct editingData {
    std::size_t whichId;
    std::vector<std::array<char, BUFFER_SIZE>> insertBuffer;
    std::array<char, BUFFER_SIZE> editBuffer;
};

namespace Widgets {
static inline float childSelectTimer = 0;

static constexpr const std::pair<ImU32, ImU32> colValid = std::pair<ImU32, ImU32>{IM_COL32(0, 120, 0, 120), IM_COL32(80, 200, 120, 255)};
static constexpr const std::pair<ImU32, ImU32> colInvalid = std::pair<ImU32, ImU32>{IM_COL32(120, 0, 0, 120), IM_COL32(220, 80, 80, 255)};
static constexpr const std::pair<ImU32, ImU32> colSelected = std::pair<ImU32, ImU32>{IM_COL32(217, 159, 0, 255), IM_COL32(179, 123, 0, 255)};
static constexpr const ImU32 colGreyBg = IM_COL32(71, 71, 71, 100);

struct rect {
    ImVec2 start;
    ImVec2 end;
};

enum class MOUSE_EVENT_TYPE { NONE, HOVER, CLICK };
enum class ACTION_TYPE { NONE, REQUEST_EDIT, EDIT, REMOVE, INSERT, SELECT, DATA, HEADER };

struct dataEvent {
    std::string tableName;
    std::string pKey;
    std::string headerName;
};

struct eventTypes {
    MOUSE_EVENT_TYPE mouse{MOUSE_EVENT_TYPE::NONE};
    ACTION_TYPE action{ACTION_TYPE::NONE};
};

struct event {
    eventTypes type;
    std::variant<dataEvent, Change> origin;
    Change::colValMap cells;
};

struct cellBoilerPlate {
    const std::string headerName;
    const ImVec2& pos;
    float width;
    bool enabled;
    bool editable;
    bool selected;
};

template <typename F, typename... Args>
concept drawFunction = std::invocable<F, const cellBoilerPlate&, const rect&, Args...>;

class DbTable {
   private:
    std::shared_ptr<const completeDbData> dbData;
    std::shared_ptr<uiChangeInfo> uiChanges;
    std::unique_ptr<Change> rowChange;

    editingData& edit;
    std::string& selectedTable;
    std::unordered_set<std::size_t>& changeHighlight;
    Logger& logger;

    float rowHeight = 0;
    rect headerPos;

    ImDrawList* drawList;
    std::map<std::string, std::vector<float>> columnWidths;

    event lastEvent;

    static constexpr const float SPLITTER_WIDTH = 10;
    static constexpr const float SPLITTER_MIN_DIST = 60;
    static constexpr const float PAD_OUTER_X = 10;
    static constexpr const float LEFT_RESERVE = 20;
    static constexpr const float RIGHT_RESERVE = 60;
    static constexpr const float PAD_HEADER_Y = 5;
    static constexpr const float PAD_INNER = 2;
    static constexpr const float PAD_INNER_CONTENT = 5;

    void handleSplitterDrag(std::vector<float>& splitters, const std::size_t index) {
        float mouseX = ImGui::GetIO().MousePos.x - headerPos.start.x;
        const float minRef = index <= 0 ? SPLITTER_MIN_DIST : splitters[index - 1] + SPLITTER_MIN_DIST;
        const float maxRef = index >= splitters.size() - 1 ? (headerPos.end.x - headerPos.start.x) : splitters[index + 1] - SPLITTER_MIN_DIST;
        // check if limits violated (splitter doesnt move other splitters)
        if (mouseX <= minRef || mouseX >= maxRef) { return; }
        splitters[index] = mouseX;
    }

    void splitterRefit(std::vector<float>& splitters, const float space) {
        const float oldWidth = headerPos.end.x - headerPos.start.x;
        if (fabs(oldWidth - space) < 1e-3f) { return; }
        const std::size_t columnSize = splitters.size();
        if (space < (columnSize * SPLITTER_MIN_DIST + (columnSize - 1) * SPLITTER_WIDTH)) { return; }
        if (oldWidth < (columnSize * SPLITTER_MIN_DIST + (columnSize - 1) * SPLITTER_WIDTH)) { return; }
        for (std::size_t i = 0; i < columnSize; i++) {
            splitters[i] *= space / oldWidth;
        }
    }

    void drawHeader(const std::string& tableName) {
        const auto& headers = dbData->headers.at(tableName).data;
        auto& splitterPoss = columnWidths.at(tableName);

        headerPos.start = ImGui::GetCursorScreenPos();
        headerPos.start.y += PAD_HEADER_Y;
        headerPos.start.x += PAD_OUTER_X + LEFT_RESERVE;
        const float available = ImGui::GetContentRegionAvail().x - 2 * PAD_OUTER_X - LEFT_RESERVE - RIGHT_RESERVE;
        splitterRefit(splitterPoss, available);
        headerPos.end = ImVec2(headerPos.start.x + available, headerPos.start.y + rowHeight + PAD_HEADER_Y);

        ImVec2 cursor = ImVec2(0, 0);  // screen coordinates
        for (size_t i = 0; i < headers.size(); ++i) {
            float width = i > 0 ? splitterPoss[i] - splitterPoss[i - 1] - SPLITTER_WIDTH : splitterPoss[0] - 0.5 * SPLITTER_WIDTH;
            ImGui::PushID(headers[i].name.c_str());
            const cellBoilerPlate cellBoiler = cellBoilerPlate(headers[i].name, cursor, width, true, false, false);
            eventTypes fromHeader = drawCellSC(
                cellBoiler,
                [this](const cellBoilerPlate& cb, const rect& r, const std::string& v) -> ACTION_TYPE {
                    return drawHeaderCell(cb, r, v);
                },
                headers[i].name);
            if (fromHeader.mouse != MOUSE_EVENT_TYPE::NONE) {
                lastEvent.type = fromHeader;
                lastEvent.origin = dataEvent(tableName, "", headers[i].name);
            }
            ImGui::PopID();
            // ...existing code...
            cursor.x = splitterPoss[i] + 0.5 * SPLITTER_WIDTH;
            if (i + 1 < headers.size()) { drawSplitterSC(tableName, i, cursor.x); }
        }

        ImGui::Dummy(ImVec2(cursor.x, rowHeight));
    }

    void drawSplitterSC(const std::string& tableName, size_t index, float rightEdgeAbs) {
        const float rightEdge = rightEdgeAbs + headerPos.start.x;
        const float leftEdge = rightEdge - SPLITTER_WIDTH;
        ImGui::SetCursorScreenPos(ImVec2(leftEdge, headerPos.start.y));
        ImGui::InvisibleButton(("##splitter" + std::to_string(index)).c_str(), ImVec2(SPLITTER_WIDTH, rowHeight));
        if (ImGui::IsItemActive()) { handleSplitterDrag(columnWidths.at(tableName), index); }
        if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            drawList->AddRectFilled(ImVec2(leftEdge, headerPos.start.y), ImVec2(rightEdge, headerPos.start.y + rowHeight), IM_COL32(255, 255, 255, 150));
        }
    }

    void drawColumns(const std::string& tableName) {
        const std::vector<float>& splitterPoss = columnWidths.at(tableName);
        ImVec2 cursor = ImVec2(0, headerPos.end.y - headerPos.start.y);
        const auto& headers = dbData->headers.at(tableName).data;

        drawUserInputRowFields(tableName, headers, splitterPoss, cursor);
        cursor.y += rowHeight;
        cursor.x = 0;

        for (std::size_t i = 0; i < headers.size(); ++i) {
            // call helper that iterates cells for this column and invokes the provided drawer
            drawColumn(tableName, i, splitterPoss, cursor, [this](const cellBoilerPlate& cellBoiler, const rect& r, const std::string& v) -> ACTION_TYPE {
                return drawDataCell(cellBoiler, r, v);
            });
            cursor.x = splitterPoss[i] + 0.5f * SPLITTER_WIDTH;
            cursor.y = headerPos.end.y - headerPos.start.y + rowHeight;
        }
    }

    void drawUserInputRowFields(const std::string& tableName, const tHeaderVector& headers, const std::vector<float>& splitterPoss, ImVec2& cursor) {
        ImGui::PushID(1);
        if (edit.editBuffer.size() < headers.size() - 1) { edit.insertBuffer.resize(headers.size()); }
        Change::colValMap newChangeColVal{};

        for (std::size_t i = 0; i < headers.size(); ++i) {
            const tHeaderInfo& headerInfo = headers[i];
            float width = i > 0 ? splitterPoss[i] - splitterPoss[i - 1] : splitterPoss[0] + 0.5f * SPLITTER_WIDTH;
            const cellBoilerPlate cellBoiler = cellBoilerPlate(headerInfo.name, cursor, width, headerInfo.type != headerType::PRIMARY_KEY, headerInfo.type != headerType::PRIMARY_KEY, false);
            eventTypes inputEvent;
            ImGui::PushID(static_cast<int>(i));
            inputEvent = drawCellSC(cellBoiler, [this](const cellBoilerPlate& cell, const rect& r) -> ACTION_TYPE {
                return drawInsertionInputField(cell, r);
            });
            ImGui::PopID();

            ImGui::PushID("ENTER");
            if (i + 1 == headers.size()) { drawLastColumnEnter(cursor, splitterPoss, i); }
            ImGui::PopID();

            cursor.x = splitterPoss[i] + 0.5f * SPLITTER_WIDTH;
        }
        ImGui::PopID();
    }

    // Draw one column (all cells). The passed `cellDrawer` is invoked for each data cell.
    template <typename CellDrawer>
    void drawColumn(const std::string& tableName, const std::size_t i, const std::vector<float>& splitterPoss, ImVec2& cursor, CellDrawer&& cellDrawer) {
        const tHeaderInfo& headerInfo = dbData->headers.at(tableName).data[i];
        ImGui::PushID(headerInfo.name.c_str());
        ImGui::PushID(static_cast<int>(i));
        std::size_t cellIndex = 0;

        for (const std::string& cell : dbData->tableRows.at(tableName).at(headerInfo.name)) {
            const std::string pKey = dbData->tableRows.at(tableName).at(dbData->headers.at(tableName).pkey)[cellIndex];
            const std::size_t pKeyId = static_cast<std::size_t>(std::stoi(pKey));
            rowChange = ChangeHelpers::getChangeOfRow(uiChanges, tableName, static_cast<std::size_t>(std::stoi(pKey)));
            float width = i > 0 ? splitterPoss[i] - splitterPoss[i - 1] : splitterPoss[0] + 0.5f * SPLITTER_WIDTH;

            ImGui::PushID(static_cast<int>(cellIndex));

            // special first column (draw full-row background here if requested)
            if (i == 0) {
                drawRowBackgroundIfNeeded(cursor, splitterPoss, rowChange.get());
                handleFirstColumnIfNeeded(tableName, headerInfo.name, pKey, cursor, rowChange.get());
            }

            // data cell via provided drawer
            bool isUkeyAndHasParent = false;
            if (rowChange) { isUkeyAndHasParent = headerInfo.type == headerType::UNIQUE_KEY && rowChange->hasParent(); }
            bool editable = edit.whichId == pKeyId && headerInfo.type != headerType::PRIMARY_KEY && !isUkeyAndHasParent;
            const cellBoilerPlate cellBoiler = cellBoilerPlate(headerInfo.name, cursor, width, true, editable, false);
            eventTypes fromData = drawCellSC(cellBoiler, std::forward<CellDrawer>(cellDrawer), cell);
            if (fromData.mouse != MOUSE_EVENT_TYPE::NONE || fromData.action == ACTION_TYPE::EDIT) {
                lastEvent.type = fromData;
                lastEvent.origin = Widgets::dataEvent(tableName, pKey, headerInfo.name);
            }

            // draw any insertion/change overlay for this cell (uses same rect as the cell)
            {
                ImVec2 min = ImVec2(cursor.x + headerPos.start.x + PAD_INNER, cursor.y + headerPos.start.y + PAD_INNER);
                ImVec2 max = ImVec2(min.x + width - PAD_INNER, min.y + rowHeight - PAD_INNER);
                rect cellRect(min, max);
                drawChangeOverlayIfNeeded(rowChange.get(), headerInfo.name, cellRect);
            }

            // special last/action column
            if (i + 1 == dbData->headers.at(tableName).data.size()) { handleLastActionIfNeeded(tableName, splitterPoss, i, cursor, rowChange.get(), pKey); }

            cursor.y += rowHeight;
            cellIndex++;
            ImGui::PopID();
        }

        // --- Render insertion changes (rows that don't exist in dbData but are in uiChanges)
        drawInsertionRowsIfAny<CellDrawer>(tableName, headerInfo, i, splitterPoss, cursor, std::forward<CellDrawer>(cellDrawer), cellIndex);

        ImGui::PopID();
        ImGui::PopID();
    }

    // Helper: draw the first column cell (left reserved area) for the current row and update lastEvent
    eventTypes handleFirstColumnIfNeeded(const std::string& tableName, const std::string& headerName, const std::string& pKey, ImVec2& cursor, const Change* change) {
        eventTypes fromFirst;
        ImVec2 firstColumnStart = ImVec2(-LEFT_RESERVE, cursor.y);
        ImGui::PushID("FIRST");
        bool selected = false;
        if (change) { selected = change->isSelected(); }
        const cellBoilerPlate cellBoiler = cellBoilerPlate(headerName, firstColumnStart, LEFT_RESERVE, true, false, selected);
        fromFirst = drawCellSC(cellBoiler, [this](const cellBoilerPlate& cell, const rect& r) -> ACTION_TYPE {
            return drawFirstColumnSC(cell, r);
        });
        ImGui::PopID();
        if (fromFirst.mouse != MOUSE_EVENT_TYPE::NONE) {
            lastEvent.type = fromFirst;
            if (change) {
                lastEvent.origin = *change;
            } else {
                lastEvent.origin = dataEvent(tableName, pKey, "FIRST");
            }
        }
        return fromFirst;
    }

    // Helper: draw the last/action column(s) for the current row and update lastEvent
    eventTypes handleLastActionIfNeeded(const std::string& tableName, const std::vector<float>& splitterPoss, const std::size_t columnIndex, ImVec2& cursor, const Change* change, const std::string& pKey) {
        eventTypes fromAction = drawActionColumn(cursor, splitterPoss, columnIndex, change);
        if (fromAction.mouse != MOUSE_EVENT_TYPE::NONE) {
            lastEvent.type = fromAction;
            if (change) {
                lastEvent.origin = *change;
            } else {
                lastEvent.origin = dataEvent(tableName, pKey, "LAST");
            }
        }
        return fromAction;
    }

    // Draw a translucent full-row background if this row is the highlighted one
    void drawRowBackgroundIfNeeded(const ImVec2& cursor, const std::vector<float>& splitterPoss, const Change* change) {
        if (!change) { return; }

        ImVec2 min = ImVec2(headerPos.start.x + PAD_INNER, headerPos.start.y + cursor.y + PAD_INNER);
        ImVec2 max = ImVec2(splitterPoss.back() + headerPos.start.x + 0.5f * SPLITTER_WIDTH, min.y + rowHeight - PAD_INNER);
        // semi-transparent blue-ish highlight
        ImU32 bgCol = change->isValid() ? colValid.first : colInvalid.first;
        drawList->AddRectFilled(min, max, bgCol);

        ImU32 borderCol = change->isValid() ? colValid.second : colInvalid.first;  // adjust as needed
        drawList->AddRect(min, max, borderCol, 0.0f, ~0, 1.0f);
    }

    // Render insertion changes for the given column (draws rows that are INSERT_ROW)
    template <typename CellDrawer>
    void drawInsertionRowsIfAny(const std::string& tableName, const tHeaderInfo& headerInfo, const std::size_t i, const std::vector<float>& splitterPoss, ImVec2& cursor, CellDrawer&& cellDrawer, std::size_t& cellIndex) {
        if (!uiChanges) { return; }
        if (!uiChanges->idMappedChanges.contains(tableName)) { return; }

        for (const auto& [pKeyNum, changeKey] : uiChanges->idMappedChanges.at(tableName)) {
            Change& change = uiChanges->changes.at(changeKey);
            if (change.getType() != changeType::INSERT_ROW) { continue; }
            const std::string pKey = std::to_string(pKeyNum);

            float width = i > 0 ? splitterPoss[i] - splitterPoss[i - 1] : splitterPoss[0] + 0.5f * SPLITTER_WIDTH;
            ImGui::PushID(static_cast<int>(cellIndex));

            // first column for insertion row
            if (i == 0) {
                drawRowBackgroundIfNeeded(cursor, splitterPoss, &change);
                handleFirstColumnIfNeeded(tableName, headerInfo.name, pKey, cursor, &change);
            }

            // data cell: use changed value if present, otherwise empty
            const std::string changedVal = change.getCell(headerInfo.name);
            bool isUkeyAndHasParent = headerInfo.type == headerType::UNIQUE_KEY && change.hasParent();
            bool editable = edit.whichId == pKeyNum && headerInfo.type != headerType::PRIMARY_KEY && !isUkeyAndHasParent;
            const cellBoilerPlate cellBoiler = cellBoilerPlate(headerInfo.name, cursor, width, true, editable, false);
            eventTypes fromData = drawCellSC(cellBoiler, std::forward<CellDrawer>(cellDrawer), changedVal);
            if (fromData.mouse != MOUSE_EVENT_TYPE::NONE) {
                lastEvent.type = fromData;
                lastEvent.origin = Widgets::dataEvent(tableName, pKey, headerInfo.name);
            }

            // overlay for insertion (draw over cell)
            {
                ImVec2 min = ImVec2(cursor.x + headerPos.start.x + PAD_INNER, cursor.y + headerPos.start.y + PAD_INNER);
                ImVec2 max = ImVec2(min.x + width - PAD_INNER, min.y + rowHeight - PAD_INNER);
                rect cellRect(min, max);
                drawChangeOverlayIfNeeded(&change, headerInfo.name, cellRect);
            }

            // last/action column for insertion row
            if (i + 1 == dbData->headers.at(tableName).data.size()) { handleLastActionIfNeeded(tableName, splitterPoss, i, cursor, &change, pKey); }

            cursor.y += rowHeight;
            cellIndex++;
            ImGui::PopID();
        }
    }

    // Draw overlay for inserted/updated cell values for given Change
    void drawChangeOverlayIfNeeded(Change* ch, const std::string& headerName, const rect& r) {
        if (!ch) { return; }
        const std::string val = ch->getCell(headerName);
        if (val.empty()) { return; }

        // semi-transparent overlay to show changed/inserted value
        ImVec2 textSize = ImGui::CalcTextSize(val.c_str());
        float yOffset = ((r.end.y - r.start.y) - textSize.y) * 0.5f;
        if (yOffset < PAD_INNER_CONTENT) { yOffset = PAD_INNER_CONTENT; }
        ImVec2 textPos(r.start.x + PAD_INNER_CONTENT, r.start.y + yOffset);
        drawList->AddText(textPos, IM_COL32_WHITE, val.c_str());
    }

    eventTypes drawLastColumnEnter(const ImVec2& pos, const std::vector<float>& splitterPoss, const std::size_t columnIndex) {
        eventTypes actionEvent;

        ImVec2 actionColumnStart = ImVec2(splitterPoss[columnIndex] + 0.5 * SPLITTER_WIDTH, pos.y);
        const cellBoilerPlate cellBoilerStart = cellBoilerPlate("LAST", actionColumnStart, RIGHT_RESERVE, true, false, false);
        actionEvent = drawCellSC(cellBoilerStart, [this](const cellBoilerPlate& cell, const rect& r) -> ACTION_TYPE {
            return drawActionColumnENTER(cell, r);
        });

        return actionEvent;
    }

    eventTypes drawActionColumn(const ImVec2& pos, const std::vector<float>& splitterPoss, const std::size_t columnIndex, const Change* change) {
        eventTypes actionEvent;
        bool enableDelete = true;
        bool enableUpdate = true;
        bool showEdit = true;
        if (change) {
            if (change->hasParent()) { enableDelete = false; }
            if (change->getType() == changeType::DELETE_ROW) {
                showEdit = false;
                enableUpdate = false;
            }
        }

        float individualWidth = showEdit ? RIGHT_RESERVE / 2 : RIGHT_RESERVE;
        ImVec2 actionColumnStart = ImVec2(splitterPoss[columnIndex] + 0.5 * SPLITTER_WIDTH, pos.y);
        ImGui::PushID("ACTIONX");
        const cellBoilerPlate cellBoilerStart = cellBoilerPlate("LAST", actionColumnStart, individualWidth, enableDelete, false, false);
        actionEvent = drawCellSC(cellBoilerStart, [this](const cellBoilerPlate& cell, const rect& r) -> ACTION_TYPE {
            return drawActionColumnXSC(cell, r);
        });
        ImGui::PopID();

        if (showEdit) {
            ImVec2 actionColumn2nd = ImVec2(actionColumnStart.x + individualWidth, pos.y);
            ImGui::PushID("ACTIONED");
            const cellBoilerPlate cellBoiler = cellBoilerPlate("LAST", actionColumn2nd, individualWidth, enableUpdate, false, false);
            eventTypes editEvent = drawCellSC(cellBoiler, [this](const cellBoilerPlate& cell, const rect& r) -> ACTION_TYPE {
                return drawActionColumnEditSC(cell, r);
            });
            if (actionEvent.mouse == MOUSE_EVENT_TYPE::NONE) { actionEvent = editEvent; }
            ImGui::PopID();
        }
        return actionEvent;
    }

    template <typename F, typename... Args>
    requires drawFunction<F, Args...> eventTypes drawCellSC(const cellBoilerPlate& cellBoiler, F&& function, Args&&... args) {
        eventTypes result;
        ImVec2 min{cellBoiler.pos.x + headerPos.start.x + PAD_INNER, cellBoiler.pos.y + headerPos.start.y + PAD_INNER};
        ImVec2 max{min.x + cellBoiler.width - PAD_INNER, min.y + rowHeight - PAD_INNER};
        rect r{min, max};
        ImVec2 size{max.x - min.x, max.y - min.y};

        result.action = std::forward<F>(function)(cellBoiler, r, std::forward<Args>(args)...);

        ImGui::SetCursorScreenPos(min);
        if (!cellBoiler.enabled) ImGui::BeginDisabled();
        ImGui::InvisibleButton("##cell", size);
        if (!cellBoiler.enabled) ImGui::EndDisabled();

        if (ImGui::IsItemHovered()) {
            drawList->AddRect(min, max, IM_COL32(255, 255, 255, 100));
            result.mouse = MOUSE_EVENT_TYPE::HOVER;
        }

        if (ImGui::IsItemClicked()) {
            result.mouse = MOUSE_EVENT_TYPE::CLICK;
            logger.pushLog(Log{"hello"});
        }

        return result;
    }

    ACTION_TYPE drawDataCell(const cellBoilerPlate& cell, const rect& r, const std::string& value) {
        ACTION_TYPE action{ACTION_TYPE::DATA};
        ImU32 colBg = cell.selected ? colSelected.first : colGreyBg;
        drawList->AddRectFilled(r.start, r.end, colBg);

        ImVec2 textSize = ImGui::CalcTextSize(value.c_str());
        float yOffset = (r.end.y - r.start.y - textSize.y) * 0.5f;
        yOffset = std::max(yOffset, PAD_INNER_CONTENT);

        ImVec2 textPos{r.start.x + PAD_INNER_CONTENT, r.start.y + yOffset};

        if (cell.editable) {
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
            ImGui::SetCursorScreenPos(textPos);
            ImGui::SetNextItemWidth(r.end.x - textPos.x - PAD_INNER_CONTENT);
            std::snprintf(edit.editBuffer.data(), BUFFER_SIZE, "%s", value.c_str());
            bool enterPressed = ImGui::InputText("##edit", edit.editBuffer.data(), BUFFER_SIZE, ImGuiInputTextFlags_EnterReturnsTrue);
            if (enterPressed && ImGui::IsItemEdited()) {
                Change::colValMap newChangeColVal{{cell.headerName, std::string(edit.editBuffer.data())}};
                lastEvent.cells = std::move(newChangeColVal);
                edit.whichId = INVALID_ID;
                action = ACTION_TYPE::EDIT;
            }
            ImGui::PopStyleVar();
        } else {
            ImU32 col = cell.enabled ? IM_COL32_WHITE : IM_COL32(255, 255, 255, 100);
            drawList->AddText(textPos, col, value.c_str());
        }

        return action;
    }

    ACTION_TYPE drawHeaderCell(const cellBoilerPlate& cell, const rect& r, const std::string& header) {
        drawDataCell(cell, r, header);
        return ACTION_TYPE::HEADER;
    }

    ACTION_TYPE drawActionColumnXSC(const cellBoilerPlate& cell, const rect& r) {
        drawDataCell(cell, r, "X");
        return ACTION_TYPE::REMOVE;
    }

    ACTION_TYPE drawActionColumnENTER(const cellBoilerPlate& cell, const rect& r) {
        drawDataCell(cell, r, "ENTER");
        return ACTION_TYPE::INSERT;
    }

    ACTION_TYPE drawActionColumnEditSC(const cellBoilerPlate& cell, const rect& r) {
        drawDataCell(cell, r, "ED");
        return ACTION_TYPE::REQUEST_EDIT;
    }

    ACTION_TYPE drawFirstColumnSC(const cellBoilerPlate& cell, const rect& r) {
        drawDataCell(cell, r, "^");
        return ACTION_TYPE::SELECT;
    }

    ACTION_TYPE drawInsertionInputField(const cellBoilerPlate& cell, const rect& r) { return drawDataCell(cell, r, ""); }

   public:
    DbTable(editingData& cEdit, std::string& cSelectedTable, std::unordered_set<std::size_t>& cChangeHighlight, Logger& cLogger) : edit(cEdit), selectedTable(cSelectedTable), changeHighlight(cChangeHighlight), logger(cLogger) {}

    void drawTable(const std::string& tableName) {
        if (rowHeight == 0) { rowHeight = ImGui::CalcTextSize("test").y + (PAD_INNER + PAD_INNER_CONTENT) * 2; }
        drawList = ImGui::GetWindowDrawList();
        ImGui::PushID(tableName.c_str());
        drawHeader(tableName);
        drawColumns(tableName);
        ImGui::PopID();
    }

    void setData(std::shared_ptr<const completeDbData> newData) {
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

    void setChangeData(std::shared_ptr<uiChangeInfo> changeData) { uiChanges = changeData; }

    event getEvent() const { return lastEvent; }

    void popEvent() { lastEvent = event(); }
};

class ChangeOverviewer {
   private:
    ChangeTracker& changeTracker;
    ChangeExeService& changeExe;
    std::shared_ptr<uiChangeInfo> uiChanges;
    float childWidth;
    float hPadding;
    std::unordered_set<std::size_t>& changeHighlight;
    std::string& selectedTable;

    // ---- Styling / layout constants
    static constexpr float UID_COL = 30.0f;
    static constexpr float TYPE_COL = 70.0f;
    static constexpr float ROW_COL = 60.0f;
    static constexpr float HPADDING = 6.0f;      // padding between row elements
    static constexpr float VPADDING_INT = 6.0f;  // internal row height
    static constexpr float VPADDING = 2.0f;      // padding after row

   public:
    ChangeOverviewer(ChangeTracker& cChangeTracker, ChangeExeService& cChangeExe, std::shared_ptr<uiChangeInfo> cUiChanges, float cChildWidth, std::unordered_set<std::size_t>& cChangeHighlight, std::string& cSelectedTable)
        : changeTracker(cChangeTracker), changeExe(cChangeExe), uiChanges(cUiChanges), childWidth(cChildWidth), changeHighlight(cChangeHighlight), selectedTable(cSelectedTable) {}

    bool drawChildren(const std::vector<std::size_t>& children, float allowedWidth) {
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

    void drawSingleChangeOverview(const Change& change) {
        const uint32_t rowId = change.getRowId();
        const std::size_t uid = change.getKey();
        const std::vector<std::size_t> children = change.getChildren();
        const bool selected = changeTracker.isChangeSelected(uid);

        const ImGuiID imGuiKeyId = static_cast<ImGuiID>(uid);

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

        if (selected) { dl->AddRect(min, max, borderCol); }
        if (hovered) { dl->AddRect(min, max, IM_COL32(255, 255, 255, 60)); }

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

        if (clicked && !childClicked) { changeTracker.toggleChangeSelect(imGuiKeyId); }

        // Row ID (right aligned)
        ImGui::SameLine(startPos.x + width - ROW_COL);
        ImGui::Text("Row %u", rowId);

        ImGui::PopID();

        // padding
        ImVec2 end = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos({end.x, max.y});
        ImGui::Dummy(ImVec2(0.0f, VPADDING));
    }
};

bool drawSelectableCircle(bool selected, bool enabled) {
    const float radius = 6.0f;
    ImGui::PushID(&selected);

    if (!enabled) { ImGui::BeginDisabled(); }

    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 center(pos.x + radius, pos.y + radius);
    ImGui::InvisibleButton("##circle", ImVec2(radius * 2.0f, radius * 2.0f));

    bool clicked = ImGui::IsItemClicked();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 borderCol = IM_COL32(160, 160, 160, 255);
    ImU32 fillCol = IM_COL32(80, 200, 120, 255);

    dl->AddCircle(center, radius, borderCol, 16, 1.5f);
    if (selected) { dl->AddCircleFilled(center, radius - 2.0f, fillCol, 16); }

    if (!enabled) { ImGui::EndDisabled(); }

    ImGui::PopID();
    return clicked;
}

}  // namespace Widgets