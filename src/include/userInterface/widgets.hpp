#pragma once

#include "change.hpp"
#include "changeExeService.hpp"
#include "changeTracker.hpp"
#include "logger.hpp"

#include <optional>
#include <unordered_set>
#include <variant>

constexpr const std::size_t BUFFER_SIZE = 256;

struct editingData {
    std::size_t whichId;
    std::vector<std::array<char, BUFFER_SIZE>> insertBuffer;
    std::array<char, BUFFER_SIZE> editBuffer;
};

namespace Widgets {
inline float childSelectTimer = 0;

constexpr std::pair<ImU32, ImU32> colValid = std::pair<ImU32, ImU32>{IM_COL32(0, 120, 0, 120), IM_COL32(80, 200, 120, 255)};
constexpr std::pair<ImU32, ImU32> colInvalid = std::pair<ImU32, ImU32>{IM_COL32(120, 0, 0, 120), IM_COL32(220, 80, 80, 255)};
constexpr std::pair<ImU32, ImU32> colSelected = std::pair<ImU32, ImU32>{IM_COL32(217, 159, 0, 255), IM_COL32(179, 123, 0, 255)};
constexpr ImU32 colGreyBg = IM_COL32(71, 71, 71, 100);
constexpr ImU32 colHoveredGrey = IM_COL32(255, 255, 255, 100);
constexpr ImU32 colWhiteSemiOpaque = IM_COL32(255, 255, 255, 255);
struct Rect {
    ImVec2 start;
    ImVec2 end;
};

enum class MOUSE_EVENT_TYPE { NONE, HOVER, CLICK };
enum class ACTION_TYPE { NONE, REQUEST_EDIT, EDIT, REMOVE, INSERT, SELECT, DATA, HEADER };

struct DataEvent {
    std::string tableName;
    std::string pKey;
    std::string headerName;
};

struct eventTypes {
    MOUSE_EVENT_TYPE mouse{MOUSE_EVENT_TYPE::NONE};
    ACTION_TYPE action{ACTION_TYPE::NONE};
};

struct Event {
    eventTypes type;
    std::variant<DataEvent, Change> origin;
    Change::colValMap cells;
};

struct CellBoilerPlate {
    const std::string headerName;
    const ImVec2& pos;
    float width;
    bool enabled;
    bool editable;
    bool selected;
    bool isInsert;
    std::size_t headerIndex;
};

template <typename F, typename... Args>
concept drawFunction = std::invocable<F, const CellBoilerPlate&, const Rect&, Args...>;

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
    Rect headerPos;

    ImDrawList* drawList;
    std::map<std::string, std::vector<float>> columnWidths;

    Event lastEvent;
    Change::colValMap insertCells;

    static constexpr float SPLITTER_WIDTH = 10;
    static constexpr float SPLITTER_MIN_DIST = 60;
    static constexpr float PAD_OUTER_X = 10;
    static constexpr float LEFT_RESERVE = 20;
    static constexpr float RIGHT_RESERVE = 60;
    static constexpr float PAD_HEADER_Y = 5;
    static constexpr float PAD_INNER = 2;
    static constexpr float PAD_INNER_CONTENT = 5;

    void handleSplitterDrag(std::vector<float>& splitters, const std::size_t index);
    void splitterRefit(std::vector<float>& splitters, const float space);
    void drawHeader(const std::string& tableName);
    void drawSplitterSC(const std::string& tableName, std::size_t index, float rightEdgeAbs);
    void drawColumns(const std::string& tableName);
    void drawUserInputRowFields(const std::string& tableName,
                                const tHeaderVector& headers,
                                const std::vector<float>& splitterPoss,
                                ImVec2& cursor);
    // Draw one column (all cells). The passed `cellDrawer` is invoked for each data cell.
    template <typename CellDrawer>
    void drawColumn(const std::string& tableName,
                    const std::size_t i,
                    const std::vector<float>& splitterPoss,
                    ImVec2& cursor,
                    CellDrawer&& cellDrawer) {
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
            if (rowChange) {
                isUkeyAndHasParent = headerInfo.type == headerType::UNIQUE_KEY && rowChange->hasParent();
            }
            bool editable = edit.whichId == pKeyId && headerInfo.type != headerType::PRIMARY_KEY && !isUkeyAndHasParent;
            const CellBoilerPlate cellBoiler = CellBoilerPlate(headerInfo.name, cursor, width, true, editable, false, false, i);
            eventTypes fromData = drawCellSC(cellBoiler, std::forward<CellDrawer>(cellDrawer), cell);
            if (fromData.mouse != MOUSE_EVENT_TYPE::NONE || fromData.action == ACTION_TYPE::EDIT) {
                lastEvent.type = fromData;
                lastEvent.origin = Widgets::DataEvent(tableName, pKey, headerInfo.name);
            }

            // draw any insertion/change overlay for this cell (uses same rect as the cell)
            {
                ImVec2 min = ImVec2(cursor.x + headerPos.start.x + PAD_INNER, cursor.y + headerPos.start.y + PAD_INNER);
                ImVec2 max = ImVec2(min.x + width - PAD_INNER, min.y + rowHeight - PAD_INNER);
                Rect cellRect(min, max);
                drawList->PushClipRect(min, max, true);
                drawChangeOverlayIfNeeded(rowChange.get(), headerInfo.name, cellRect);
                drawList->PopClipRect();
            }

            // special last/action column
            if (i + 1 == dbData->headers.at(tableName).data.size()) {
                handleLastActionIfNeeded(tableName, splitterPoss, i, cursor, rowChange.get(), pKey);
            }

            cursor.y += rowHeight;
            cellIndex++;
            ImGui::PopID();
        }

        // --- Render insertion changes (rows that don't exist in dbData but are in uiChanges)
        drawInsertionRowsIfAny<CellDrawer>(tableName, headerInfo, i, splitterPoss, cursor, std::forward<CellDrawer>(cellDrawer), cellIndex);

        ImGui::PopID();
        ImGui::PopID();
    }

    // Helper: draw the first column cell (left reserved area) for the current row and update
    // lastEvent
    eventTypes handleFirstColumnIfNeeded(
        const std::string& tableName, const std::string& headerName, const std::string& pKey, ImVec2& cursor, const Change* change);

    // Helper: draw the last/action column(s) for the current row and update lastEvent
    eventTypes handleLastActionIfNeeded(const std::string& tableName,
                                        const std::vector<float>& splitterPoss,
                                        const std::size_t columnIndex,
                                        ImVec2& cursor,
                                        const Change* change,
                                        const std::string& pKey);

    // Draw a translucent full-row background if this row is the highlighted one
    void drawRowBackgroundIfNeeded(const ImVec2& cursor, const std::vector<float>& splitterPoss, const Change* change);

    // Render insertion changes for the given column (draws rows that are INSERT_ROW)
    template <typename CellDrawer>
    void drawInsertionRowsIfAny(const std::string& tableName,
                                const tHeaderInfo& headerInfo,
                                const std::size_t i,
                                const std::vector<float>& splitterPoss,
                                ImVec2& cursor,
                                CellDrawer&& cellDrawer,
                                std::size_t& cellIndex) {
        if (!uiChanges) {
            return;
        }
        if (!uiChanges->idMappedChanges.contains(tableName)) {
            return;
        }

        for (const auto& [pKeyNum, changeKey] : uiChanges->idMappedChanges.at(tableName)) {
            Change& change = uiChanges->changes.at(changeKey);
            if (change.getType() != changeType::INSERT_ROW) {
                continue;
            }
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
            const CellBoilerPlate cellBoiler = CellBoilerPlate(headerInfo.name, cursor, width, true, editable, false, false, i);
            eventTypes fromData = drawCellSC(cellBoiler, std::forward<CellDrawer>(cellDrawer), changedVal);
            if (fromData.mouse != MOUSE_EVENT_TYPE::NONE) {
                lastEvent.type = fromData;
                lastEvent.origin = Widgets::DataEvent(tableName, pKey, headerInfo.name);
            }

            // overlay for insertion (draw over cell)
            {
                ImVec2 min = ImVec2(cursor.x + headerPos.start.x + PAD_INNER, cursor.y + headerPos.start.y + PAD_INNER);
                ImVec2 max = ImVec2(min.x + width - PAD_INNER, min.y + rowHeight - PAD_INNER);
                Rect cellRect(min, max);
                drawChangeOverlayIfNeeded(&change, headerInfo.name, cellRect);
            }

            // last/action column for insertion row
            if (i + 1 == dbData->headers.at(tableName).data.size()) {
                handleLastActionIfNeeded(tableName, splitterPoss, i, cursor, &change, pKey);
            }

            cursor.y += rowHeight;
            cellIndex++;
            ImGui::PopID();
        }
    }

    // Draw overlay for inserted/updated cell values for given Change
    void drawChangeOverlayIfNeeded(Change* ch, const std::string& headerName, const Rect& r);
    eventTypes drawLastColumnEnter(const ImVec2& pos, const std::vector<float>& splitterPoss, const std::size_t columnIndex);
    eventTypes
    drawActionColumn(const ImVec2& pos, const std::vector<float>& splitterPoss, const std::size_t columnIndex, const Change* change);

    template <typename F, typename... Args>
        requires drawFunction<F, Args...>
    eventTypes drawCellSC(const CellBoilerPlate& cellBoiler, F&& function, Args&&... args) {
        eventTypes result;
        ImVec2 min{cellBoiler.pos.x + headerPos.start.x + PAD_INNER, cellBoiler.pos.y + headerPos.start.y + PAD_INNER};
        ImVec2 max{min.x + cellBoiler.width - PAD_INNER, min.y + rowHeight - PAD_INNER};
        Rect r{min, max};
        ImVec2 size{max.x - min.x, max.y - min.y};

        drawList->PushClipRect(min, max, true);
        result.action = std::forward<F>(function)(cellBoiler, r, std::forward<Args>(args)...);

        ImGui::SetCursorScreenPos(min);
        if (!cellBoiler.enabled)
            ImGui::BeginDisabled();
        ImGui::InvisibleButton("##cell", size);
        if (!cellBoiler.enabled)
            ImGui::EndDisabled();

        if (ImGui::IsItemHovered()) {
            drawList->AddRect(min, max, colHoveredGrey);
            result.mouse = MOUSE_EVENT_TYPE::HOVER;

            if constexpr (sizeof...(Args) > 0) {
                auto&& first = std::get<0>(std::forward_as_tuple(args...));
                if (!first.empty()) {
                    ImGui::BeginTooltip();
                    ImGui::TextUnformatted(first.c_str());
                    ImGui::EndTooltip();
                }
            }
        }

        if (ImGui::IsItemClicked()) {
            result.mouse = MOUSE_EVENT_TYPE::CLICK;
        }

        drawList->PopClipRect();
        return result;
    }

    ACTION_TYPE drawDataCell(const CellBoilerPlate& cell, const Rect& r, const std::string& value);
    ACTION_TYPE
    drawHeaderCell(const CellBoilerPlate& cell, const Rect& r, const std::string& header);
    ACTION_TYPE drawActionColumnXSC(const CellBoilerPlate& cell, const Rect& r);
    ACTION_TYPE drawActionColumnENTER(const CellBoilerPlate& cell, const Rect& r);
    ACTION_TYPE drawActionColumnEditSC(const CellBoilerPlate& cell, const Rect& r);
    ACTION_TYPE drawFirstColumnSC(const CellBoilerPlate& cell, const Rect& r);
    ACTION_TYPE drawInsertionInputField(const CellBoilerPlate& cell, const Rect& r);

  public:
    DbTable(editingData& cEdit, std::string& cSelectedTable, std::unordered_set<std::size_t>& cChangeHighlight, Logger& cLogger)
        : edit(cEdit), selectedTable(cSelectedTable), changeHighlight(cChangeHighlight), logger(cLogger) {}

    void drawTable(const std::string& tableName);
    void setData(std::shared_ptr<const completeDbData> newData);
    void setChangeData(std::shared_ptr<uiChangeInfo> changeData);
    Event getEvent() const;
    void popEvent();
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
    static constexpr float HPADDING = 6.0f;     // padding between row elements
    static constexpr float VPADDING_INT = 6.0f; // internal row height
    static constexpr float VPADDING = 2.0f;     // padding after row

  public:
    ChangeOverviewer(ChangeTracker& cChangeTracker,
                     ChangeExeService& cChangeExe,
                     float cChildWidth,
                     std::unordered_set<std::size_t>& cChangeHighlight,
                     std::string& cSelectedTable)
        : changeTracker(cChangeTracker), changeExe(cChangeExe), childWidth(cChildWidth), changeHighlight(cChangeHighlight),
          selectedTable(cSelectedTable) {}

    void setChangeData(std::shared_ptr<uiChangeInfo> changeData);
    bool drawChildren(const std::vector<std::size_t>& children, float allowedWidth);
    MOUSE_EVENT_TYPE drawSingleChangeOverview(const Change& change);
};

} // namespace Widgets