#pragma once

#include <unordered_set>
#include <array>

#include "imgui.h"

#include "dbService.hpp"
#include "dbInterface.hpp"
#include "changeTracker.hpp"
#include "changeExeService.hpp"

#include "logger.hpp"

constexpr const std::size_t INVALID_ID = std::numeric_limits<std::size_t>::max();
constexpr const std::size_t BUFFER_SIZE = 256;
struct editingData {
    std::unordered_set<std::size_t> whichIds;
    std::vector<std::array<char, BUFFER_SIZE>> insertBuffer;
    std::array<char, BUFFER_SIZE> editBuffer;
};

struct rowIds {
    std::size_t loopId;
    std::size_t pKeyId;
};

class DbVisualizer {
   private:
    DbService& dbService;
    ChangeTracker& changeTracker;
    ChangeExeService& changeExe;
    Logger& logger;

    std::shared_ptr<const completeDbData> dbData;
    std::shared_ptr<uiChangeInfo> uiChanges;
    editingData edit;
    std::string selectedTable;

    void drawInsertionChanges(const std::string& table) {
        if (!uiChanges->idMappedChanges.contains(table) || !dbData->headers.contains(table)) { return; }
        ImGui::TableNextRow();
        if (table == "categories") {
            int a = 0;
            int b = 1;
            b = a + b;
        }
        for (const auto& [_, hash] : uiChanges->idMappedChanges.at(table)) {
            const Change& change = uiChanges->changes.at(hash);
            if (change.getType() == changeType::INSERT_ROW) {
                for (const auto& header : dbData->headers.at(table).data) {
                    ImGui::TableNextColumn();
                    const auto& cellChanges = change.getCells();
                    if (header.type == headerType::PRIMARY_KEY) {
                        std::string pKeyFormat = std::format("({})", std::to_string(change.getRowId()));
                        ImGui::TextUnformatted(pKeyFormat.c_str());
                        continue;
                    }
                    if (!cellChanges.contains(header.name)) {
                        ImGui::TextUnformatted("-");
                        continue;
                    }
                    ImGui::TextUnformatted(cellChanges.at(header.name).c_str());
                }
                ImGui::TableNextColumn();
                ImGui::PushID(static_cast<int>(change.getRowId()));

                if (ImGui::Button("RUN")) { changeExe.requestChangeApplication(Change{change}, sqlAction::EXECUTE); }
                ImGui::SameLine();
                ImGui::BeginDisabled(change.hasParent());

                if (ImGui::Button("x")) { changeTracker.removeChange(change.getKey()); }
                ImGui::EndDisabled();

                ImGui::PopID();
            }
        }
        return;
    }

    void drawUserInputRowFields(const std::string& tableName) {
        // TODO: Evtl in createRows in den loop integrieren?
        if (!dbData->headers.contains(tableName)) { return; }
        ImGui::PushID(1);
        Change::colValMap newChangeColVal{};
        ImGui::TableNextRow();
        edit.insertBuffer.resize(dbData->headers.size());
        std::size_t i = 0;
        for (const auto& header : dbData->headers.at(tableName).data) {
            ImGui::TableNextColumn();
            if (!(header.type == headerType::PRIMARY_KEY)) {
                ImGui::PushID(header.name.c_str());
                //std::snprintf(edit.buffer, BUFFER_SIZE, "%s", newCellValue.c_str());
                ImGui::InputText("##edit", edit.insertBuffer.at(i).data(), BUFFER_SIZE);
                std::string newValue = newChangeColVal[header.name] = std::string(edit.insertBuffer.at(i).data());
                ImGui::PopID();
            }
            ++i;
        }
        ImGui::TableNextColumn();
        if (ImGui::Button("ENTER")) { changeTracker.addChange(Change{newChangeColVal, changeType::INSERT_ROW, dbService.getTable(tableName), changeTracker.getMaxPKey(tableName) + 1}); }
        ImGui::PopID();
    }

    void drawEditableData(const std::string& tableName, const std::string& newCellValue, const tHeaderInfo& header, const changeType& cType, const std::size_t id) {
        if (edit.whichIds.contains(id) && header.type != headerType::PRIMARY_KEY) {
            ImGui::PushID(header.name.c_str());
            std::snprintf(edit.editBuffer.data(), BUFFER_SIZE, "%s", newCellValue.c_str());
            if (ImGui::InputText("##edit", edit.editBuffer.data(), BUFFER_SIZE, ImGuiInputTextFlags_EnterReturnsTrue)) {
                Change::colValMap newChangeColVal{{header.name, std::string(edit.editBuffer.data())}};
                changeTracker.addChange(Change{newChangeColVal, changeType::UPDATE_CELLS, dbService.getTable(tableName), id});
            }
            ImGui::PopID();
        } else {
            if (cType == changeType::DELETE_ROW) {
                ImVec2 pos = ImGui::GetCursorScreenPos();
                ImVec2 textSize = ImGui::CalcTextSize(newCellValue.c_str());
                ImDrawList* drawList = ImGui::GetWindowDrawList();
                drawList->AddRectFilled(pos, ImVec2(pos.x + textSize.x, pos.y + textSize.y), IM_COL32(200, 120, 120, 255), 2.0f);
            }
            ImGui::TextUnformatted(newCellValue.c_str());
        }
    }

    void drawCellWithChange(std::expected<const Change*, bool> change, const std::string& originalCell, const std::string& tableName, const tHeaderInfo& header, const std::size_t id) {
        std::string newCellValue{};
        changeType cType = changeType::NONE;
        // Get changed value for column and display it
        if (change.has_value()) {
            cType = ((*change)->getType());
            if (cType == changeType::UPDATE_CELLS) {
                newCellValue = (*change)->getCell(header.name);
                if (!newCellValue.empty()) {
                    ImGui::TextUnformatted(originalCell.c_str());
                    ImGui::SameLine();
                }
            }
        }

        if (newCellValue.empty()) { newCellValue = originalCell; }

        // Draw editable cell
        drawEditableData(tableName, newCellValue, header, cType, id);
    }

    std::size_t getIdOfLoopIndex(const std::string& table, const std::size_t row) {
        if (!dbData->tableRows.contains(table)) { return INVALID_ID; }
        const auto& data = dbData->tableRows.at(table);

        std::string primaryKey = dbData->headers.at(table).pkey;
        if (!data.contains(primaryKey)) { return INVALID_ID; }
        // convert cell value to id
        const tStringVector& ids = data.at(primaryKey);
        if (ids.size() <= row) { return INVALID_ID; }
        try {
            return static_cast<std::size_t>(std::stoi(ids.at(row)));
        } catch (const std::exception& e) {
            logger.pushLog(Log{std::format("ERROR: Getting ID of row : value {} is not an integer. Exception: {}", ids.at(row), e.what())});
            return INVALID_ID;
        }
    }

    std::expected<const Change*, bool> getChangeOfRow(const std::string& table, const std::size_t id) {
        if (!uiChanges->idMappedChanges.contains(table)) { return std::unexpected(false); }
        if (id == INVALID_ID) { return std::unexpected(false); }
        if (uiChanges->idMappedChanges.at(table).contains(id)) {
            const std::size_t changeKey = uiChanges->idMappedChanges.at(table).at(id);
            return &uiChanges->changes.at(changeKey);
        }
        return std::unexpected(false);
    }

    void createColumns(const std::string& table) {
        ImGui::TableNextRow();
        for (const auto& column : dbData->headers.at(table).data) {
            ImGui::TableNextColumn();
            std::string columnText = column.name;
            switch (column.type) {
                case headerType::PRIMARY_KEY:
                    columnText = std::format("* {}", column.name);
                    break;
                case headerType::FOREIGN_KEY:
                    columnText = std::format("** {}", column.name);
                    break;
                default:
                    break;
            }

            if (column.type == headerType::FOREIGN_KEY) {
                ImGui::Selectable(columnText.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick);
                if (ImGui::IsItemClicked()) {
                    logger.pushLog(Log{std::format("JUMPING to {}", column.referencedTable)});
                    selectedTable = column.referencedTable;
                }
            } else {
                ImGui::TextUnformatted(columnText.c_str());
            }
        }
    }

    void createRows(const std::string& table) {
        // Check data validity
        if (!dbData->headers.contains(table)) { return; }
        if (!dbData->tableRows.contains(table)) { return; }
        bool hasData = false;
        for (const tHeaderInfo& vHeaders : dbData->headers.at(table).data) {
            if (vHeaders.name.size() > 0) {
                hasData = true;
                break;
            }
        }
        if (!hasData) { return; }

        drawUserInputRowFields(table);
        rowIds indexes{0, 0};
        std::size_t maxId = indexes.pKeyId;
        bool maxNotReached = true;
        while (maxNotReached) {
            // Get primary key for row
            indexes.pKeyId = getIdOfLoopIndex(table, indexes.loopId);
            if (indexes.pKeyId == INVALID_ID) { break; }
            if (indexes.pKeyId > maxId) { maxId = indexes.pKeyId; }
            auto rowChange = getChangeOfRow(table, indexes.pKeyId);
            ImGui::TableNextRow();  // TODO: Das Problem mit der leeren Zeile ist genau hier. Man merkt erst nach dem "nextrow"-AUfruf, dass keine Daten da sind
            // Draw every column for this row index
            ImGui::PushID(static_cast<int>(indexes.pKeyId));
            for (const auto& header : dbData->headers.at(table).data) {
                ImGui::TableNextColumn();
                if (!dbData->tableRows.at(table).contains(header.name)) { continue; }
                // Get data for column
                const auto& data = dbData->tableRows.at(table).at(header.name);
                if (data.size() <= indexes.loopId) {
                    maxNotReached = false;
                    if (data.size() == 0) { ImGui::TextUnformatted("-"); }
                    continue;
                }
                // Draw each cell
                std::string cell = data.at(indexes.loopId);
                drawCellWithChange(rowChange, cell, table, header, indexes.pKeyId);
                maxNotReached = true;
            }
            // Exit if all data has been printed
            if (!maxNotReached) {
                ImGui::PopID();
                break;
            }
            // Draw edit options
            ImGui::TableNextColumn();

            // Remove row or (deletion) change affecting it

            bool withParentChange = false;
            if (rowChange.has_value()) { withParentChange = (*rowChange)->hasParent(); }

            ImGui::BeginDisabled(withParentChange);
            if (ImGui::Button("x")) {
                if (rowChange.has_value()) {
                    changeTracker.removeChange((*rowChange)->getKey());
                } else {
                    Change::colValMap cvMap{};
                    changeTracker.addChange(Change{cvMap, changeType::DELETE_ROW, dbService.getTable(table), indexes.pKeyId});
                    if (edit.whichIds.contains(indexes.pKeyId)) { edit.whichIds.erase(indexes.pKeyId); }
                }
            }
            ImGui::EndDisabled();

            // Edit row
            ImGui::SameLine();
            if (rowChange.has_value()) { ImGui::BeginDisabled((*rowChange)->getType() == changeType::DELETE_ROW); }
            if (ImGui::Button("edit")) {
                if (edit.whichIds.contains(indexes.pKeyId)) {
                    edit.whichIds.erase(indexes.pKeyId);
                } else {
                    edit.whichIds.insert(indexes.pKeyId);
                }
            }
            if (rowChange.has_value()) { ImGui::EndDisabled(); }

            ImGui::PopID();
            ++indexes.loopId;
        }
        drawInsertionChanges(table);
    }

    void createTableSplitters() {
        if (ImGui::BeginTabBar("MainTabs")) {
            for (const auto& [table, data] : dbData->headers) {
                ImGuiTabItemFlags flags = 0;
                if (selectedTable == table) flags |= ImGuiTabItemFlags_SetSelected;
                if (ImGui::BeginTabItem(table.c_str(), nullptr, flags)) {
                    if (selectedTable == table) { selectedTable.clear(); }
                    drawTableView(table, data.data);
                    ImGui::EndTabItem();
                }
            }
            ImGui::EndTabBar();
        }
    }

    void drawTableView(const std::string& table, const tHeaderVector& data) {
        if (ImGui::BeginTable("SplitView", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchSame)) {
            ImGui::TableNextColumn();
            ImGui::Text("DATA");
            ImGui::Separator();
            ImGui::BeginChild("Table", ImVec2{0, ImGui::GetContentRegionAvail().y}, false);
            if (ImGui::BeginTable("ColumnsTable", static_cast<int>(data.size() + 1), ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                ImGui::TableNextRow();
                createColumns(table);
                createRows(table);
                ImGui::EndTable();
            }
            ImGui::EndChild();

            ImGui::TableNextColumn();
            drawTableChangeOverview(table);

            ImGui::EndTable();
        }
    }

    void drawChangeOverview() {
        for (const auto& [table, _] : uiChanges->idMappedChanges) {
            ImGui::TextUnformatted(table.c_str());
            drawTableChangeOverview(table);
        }
    }

    void drawTableChangeOverview(const std::string& table) {
        if (!uiChanges->idMappedChanges.contains(table)) { return; }
        ImGui::Text("CHANGE OVERVIEW");
        ImGui::BeginChild("TableChangeOverview", ImVec2{0, ImGui::GetContentRegionAvail().y}, false);
        ImGui::PushID("tableChangeOverview");
        for (const auto& [_, hash] : uiChanges->idMappedChanges.at(table)) {
            const Change& change = uiChanges->changes.at(hash);
            std::size_t id = change.getRowId();
            std::string type;
            switch (change.getType()) {
                case changeType::DELETE_ROW:
                    type = "DELETE";
                    break;
                case changeType::INSERT_ROW:
                    type = "INSERT";
                    break;
                case changeType::UPDATE_CELLS:
                    type = "UPDATE";
                    break;
                default:
                    type = "UNKNOWN";
                    break;
            }
            // visualize change
            ImGui::PushID(static_cast<int>(id));
            ImGui::TextUnformatted(std::format("{}: ", type).c_str());
            ImGui::SameLine();
            ImGui::TextUnformatted(std::format("ID: {}", id).c_str());
            ImGui::SameLine();

            // select change
            bool selected = changeTracker.isChangeSelected(hash);

            if (ImGui::Checkbox("TEST", &selected)) { changeTracker.toggleChangeSelect(hash); }
            ImGui::BeginDisabled(change.hasParent());
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        ImGui::PopID();
        ImGui::EndChild();
    }

   public:
    void setData(std::shared_ptr<const completeDbData> newData) { dbData = newData; }

    void setChangeData(std::shared_ptr<uiChangeInfo> changeData) { uiChanges = changeData; }

    void run() {
        if (ImGui::BeginTabBar("Main")) {
            if (ImGui::BeginTabItem("Tables")) {
                createTableSplitters();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Changes")) {
                drawChangeOverview();
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }

    DbVisualizer(DbService& cDbService, ChangeTracker& cChangeTracker, ChangeExeService& cChangeExe, Logger& cLogger) : dbService(cDbService), changeTracker(cChangeTracker), changeExe(cChangeExe), logger(cLogger) {}
};