#pragma once

#include "imgui.h"

#include "change.hpp"

#include <unordered_set>

namespace Widgets {
static inline float childSelectTimer = 0;

static constexpr const std::pair<ImU32, ImU32> colValid = std::pair<ImU32, ImU32>{IM_COL32(0, 120, 0, 120), IM_COL32(80, 200, 120, 255)};
static constexpr const std::pair<ImU32, ImU32> colInvalid = std::pair<ImU32, ImU32>{IM_COL32(120, 0, 0, 120), IM_COL32(220, 80, 80, 255)};

static constexpr const std::pair<ImU32, ImU32> colSelected = std::pair<ImU32, ImU32>{IM_COL32(217, 159, 0, 255), IM_COL32(179, 123, 0, 255)};

class DbTable {
   private:
    // TODO: Eigene Tabellenklasse bauen, damit Gestalung beliebig ist
   public:
    DbTable() {}
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