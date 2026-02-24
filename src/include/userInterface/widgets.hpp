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

constexpr std::pair<ImU32, ImU32> colValid = std::pair<ImU32, ImU32>{IM_COL32(0, 120, 0, 255), IM_COL32(80, 200, 120, 255)};
constexpr std::pair<ImU32, ImU32> colInvalid = std::pair<ImU32, ImU32>{IM_COL32(120, 0, 0, 255), IM_COL32(220, 80, 80, 255)};
constexpr std::pair<ImU32, ImU32> colSelected = std::pair<ImU32, ImU32>{IM_COL32(217, 159, 0, 255), IM_COL32(179, 123, 0, 255)};
constexpr ImU32 colGreyBg = IM_COL32(50, 50, 50, 255);
constexpr ImU32 colHoveredGrey = IM_COL32(100, 100, 100, 255);
constexpr ImU32 colWhiteSemiOpaque = IM_COL32(255, 255, 255, 255);

struct Rect {
    ImVec2 start;
    ImVec2 end;
};

enum class MouseEventType { NONE, HOVER, CLICK };
enum class ActionType { NONE, REQUEST_EDIT, EDIT, REMOVE, INSERT, SELECT, DATA, HEADER };
enum class CellType { SELECTION, HEADER, DATA, ACTION_COLUMN };

struct DataEvent {
    std::string tableName;
    std::string pKey;
    std::string headerName;
};

struct EventTypes {
    MouseEventType mouse{MouseEventType::NONE};
    ActionType action{ActionType::NONE};
};

struct Event {
    EventTypes type;
    std::variant<DataEvent, Change> origin;
    Change::colValMap cells;
};

struct CellBoilerPlate {
    const HeaderInfo header;
    const ImVec2& pos;
    const Change* change;
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
    std::shared_ptr<const CompleteDbData> dbData;
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
                                const HeaderVector& headers,
                                const std::vector<float>& splitterPoss,
                                ImVec2& cursor);
    // Draw one column (all cells). The passed `cellDrawer` is invoked for each data cell.
    void drawColumn(const std::string& tableName, const std::size_t i, const std::vector<float>& splitterPoss, ImVec2& cursor);

    // Helper: draw the first column cell (left reserved area) for the current row and update
    // lastEvent
    EventTypes handleFirstColumnIfNeeded(
        const std::string& tableName, const HeaderInfo& headerName, const std::string& pKey, ImVec2& cursor, const Change* change);

    // Helper: draw the last/action column(s) for the current row and update lastEvent
    EventTypes handleLastActionIfNeeded(const std::string& tableName,
                                        const std::vector<float>& splitterPoss,
                                        const std::size_t columnIndex,
                                        ImVec2& cursor,
                                        const Change* change,
                                        const std::string& pKey);

    // Draw a translucent full-row background if this row is the highlighted one
    void drawRowBackgroundIfNeeded(const ImVec2& cursor, const std::vector<float>& splitterPoss, const Change* change);

    // Render insertion changes for the given column (draws rows that are INSERT_ROW)
    void drawInsertionCellsOfColumn(const std::string& tableName,
                                    const HeaderInfo& headerInfo,
                                    const std::size_t i,
                                    const std::vector<float>& splitterPoss,
                                    ImVec2& cursor,
                                    std::size_t& cellIndex);

    // Draw overlay for inserted/updated cell values for given Change
    void drawChangeOverlayIfNeeded(
        Change* ch, const std::string& originalValue, const std::string& headerName, const Rect& r, const EventTypes& event);
    EventTypes drawLastColumnEnter(const ImVec2& pos, const std::vector<float>& splitterPoss, const std::size_t columnIndex);
    EventTypes
    drawActionColumn(const ImVec2& pos, const std::vector<float>& splitterPoss, const std::size_t columnIndex, const Change* change);

    template <typename F, typename... Args>
        requires drawFunction<F, Args...>
    EventTypes drawCellSC(const CellBoilerPlate& cellBoiler, F&& function, Args&&... args) {
        EventTypes result;
        ImVec2 min{cellBoiler.pos.x + headerPos.start.x + PAD_INNER, cellBoiler.pos.y + headerPos.start.y + PAD_INNER};
        ImVec2 max{min.x + cellBoiler.width - PAD_INNER, min.y + rowHeight - PAD_INNER};
        Rect r{min, max};
        ImVec2 size{max.x - min.x, max.y - min.y};

        drawList->PushClipRect(min, max, true);
        result.action = std::forward<F>(function)(cellBoiler, r, std::forward<Args>(args)...);

        ImGui::SetCursorScreenPos(min);
        if (!cellBoiler.enabled) { ImGui::BeginDisabled(); }
        ImGui::InvisibleButton("##cell", size);
        if (!cellBoiler.enabled) { ImGui::EndDisabled(); }

        if (ImGui::IsItemHovered()) {
            drawList->AddRect(min, max, colHoveredGrey);
            result.mouse = MouseEventType::HOVER;

            if constexpr (sizeof...(Args) > 0) {
                auto&& first = std::get<0>(std::forward_as_tuple(args...));
                if (!first.empty()) {
                    // drawList->AddText(min, IM_COL32_WHITE, first.c_str());
                    // ImGui::BeginTooltip();
                    // ImGui::TextUnformatted(first.c_str());
                    // ImGui::EndTooltip();
                }
            }
        }

        if (ImGui::IsItemClicked()) { result.mouse = MouseEventType::CLICK; }

        drawList->PopClipRect();
        return result;
    }

    ActionType drawDataCell(const CellBoilerPlate& cell, const Rect& r, const std::string& value, CellType cellType);
    void drawChangeInCell(const CellBoilerPlate& cell, const Rect& r, ImVec2 textPos, ImU32 col, const std::string& value);
    ActionType drawHeaderCell(const CellBoilerPlate& cell, const Rect& r, const std::string& header);
    ActionType drawActionColumnXSC(const CellBoilerPlate& cell, const Rect& r);
    ActionType drawActionColumnENTER(const CellBoilerPlate& cell, const Rect& r);
    ActionType drawActionColumnEditSC(const CellBoilerPlate& cell, const Rect& r);
    ActionType drawFirstColumnSC(const CellBoilerPlate& cell, const Rect& r);
    ActionType drawInsertionInputField(const CellBoilerPlate& cell, const Rect& r);

  public:
    DbTable(editingData& cEdit, std::string& cSelectedTable, std::unordered_set<std::size_t>& cChangeHighlight, Logger& cLogger)
        : edit(cEdit), selectedTable(cSelectedTable), changeHighlight(cChangeHighlight), logger(cLogger) {}

    void drawTable(const std::string& tableName);
    void setData(std::shared_ptr<const CompleteDbData> newData);
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
    MouseEventType drawSingleChangeOverview(const Change& change, std::size_t* visualDepth, const std::size_t parent, bool childrenShowing);
};

} // namespace Widgets