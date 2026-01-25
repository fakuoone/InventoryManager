#pragma once

#include "pch.hpp"

#include "dbService.hpp"
#include "dbInterface.hpp"
#include "changeTracker.hpp"
#include "changeExeService.hpp"

#include "userInterface/widgets.hpp"

#include "logger.hpp"

#include <array>
#include <limits>

struct rowIds {
    std::size_t loopId;
    uint32_t pKeyId;
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
    std::unordered_set<std::size_t> changeHighlight;

    Widgets::DbTable dbTable{edit, selectedTable, changeHighlight, logger};  // TODO: Finish this
    Widgets::ChangeOverviewer changeOverviewer{changeTracker, changeExe, uiChanges, 60, changeHighlight, selectedTable};

    void drawInsertionChanges(const std::string& table) {
        if (!uiChanges->idMappedChanges.contains(table) || !dbData->headers.contains(table)) { return; }
        ImGui::TableNextRow();
        ImVec2 rowMin = ImGui::GetCursorScreenPos();

        for (const auto& [_, hash] : uiChanges->idMappedChanges.at(table)) {
            const Change& change = uiChanges->changes.at(hash);
            if (change.getType() == changeType::INSERT_ROW) {
                std::string displayString;
                for (const auto& header : dbData->headers.at(table).data) {
                    ImGui::TableNextColumn();
                    const auto& cellChanges = change.getCells();
                    if (header.type == headerType::PRIMARY_KEY) {
                        displayString = std::format("({})", std::to_string(change.getRowId()));
                    } else {
                        displayString = cellChanges.contains(header.name) ? (cellChanges.at(header.name)) : "";
                    }
                    drawEditableData(table, displayString, header, &change, change.getRowId());
                }
                drawRowHighlights(&change, rowMin);

                ImGui::TableNextColumn();
                ImGui::PushID(static_cast<int>(change.getRowId()));

                ImGui::BeginDisabled(!change.isValid());
                if (ImGui::Button("RUN")) { changeExe.requestChangeApplication(change.getKey(), sqlAction::EXECUTE); }
                ImGui::EndDisabled();

                ImGui::SameLine();
                ImGui::BeginDisabled(change.hasParent());
                if (ImGui::Button("x")) { changeTracker.removeChanges(change.getKey()); }
                ImGui::EndDisabled();

                ImGui::SameLine();
                if (ImGui::Button("EDIT")) {
                    if (edit.whichId == change.getRowId()) {
                        edit.whichId = INVALID_ID;
                    } else {
                        edit.whichId = change.getRowId();
                    }
                }

                ImGui::PopID();
            }
        }
        return;
    }

    void drawUserInputRowFields(const std::string& tableName) {
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
                ImGui::InputText("##edit", edit.insertBuffer.at(i).data(), BUFFER_SIZE);
                std::string newValue = newChangeColVal[header.name] = std::string(edit.insertBuffer.at(i).data());
                ImGui::PopID();
            }
            ++i;
        }
        ImGui::TableNextColumn();
        if (ImGui::Button("ENTER")) { changeTracker.addChange(Change{newChangeColVal, changeType::INSERT_ROW, dbService.getTable(tableName)}); }
        ImGui::PopID();
    }

    void drawEditableData(const std::string& tableName, const std::string& newCellValue, const tHeaderInfo& header, const Change* change, const std::uint32_t id) {
        changeType cType{changeType::NONE};
        bool isUkeyAndHasParent = false;
        if (change) {
            cType = change->getType();
            isUkeyAndHasParent = header.type == headerType::UNIQUE_KEY && change->hasParent();
        }
        if (edit.whichId == id && header.type != headerType::PRIMARY_KEY && !isUkeyAndHasParent) {
            ImGui::PushID(header.name.c_str());
            std::snprintf(edit.editBuffer.data(), BUFFER_SIZE, "%s", newCellValue.c_str());
            bool enterPressed = ImGui::InputText("##edit", edit.editBuffer.data(), BUFFER_SIZE, ImGuiInputTextFlags_EnterReturnsTrue);
            if (enterPressed && ImGui::IsItemEdited()) {
                Change::colValMap newChangeColVal{{header.name, std::string(edit.editBuffer.data())}};
                changeTracker.addChange(Change{newChangeColVal, changeType::UPDATE_CELLS, dbService.getTable(tableName)}, id);
            }
            ImGui::PopID();
        } else {
            if (cType == changeType::DELETE_ROW) {}
            ImGui::TextUnformatted(newCellValue.c_str());
        }
    }

    void drawCellWithChange(std::expected<const Change*, bool> change, const std::string& originalCell, const std::string& tableName, const tHeaderInfo& header, const uint32_t id) {
        std::string newCellValue{};
        const Change* changePtr = nullptr;
        // Get changed value for column and display it
        if (change.has_value()) {
            changePtr = *change;
            if (changePtr->getType() == changeType::UPDATE_CELLS) {
                newCellValue = (*change)->getCell(header.name);
                if (!newCellValue.empty()) {
                    ImGui::TextUnformatted(originalCell.c_str());
                    ImGui::SameLine();
                }
            }
        }
        if (newCellValue.empty()) { newCellValue = originalCell; }
        drawEditableData(tableName, newCellValue, header, changePtr, id);
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

    void drawRowHighlights(const Change* change, ImVec2& rowMin) {
        if (!change) { return; }
        bool valid = change->isValid();
        ImU32 col = valid ? IM_COL32(0, 255, 0, 60) : IM_COL32(255, 0, 0, 60);
        if (changeHighlight.contains(change->getKey())) { col = IM_COL32(217, 159, 0, 255); }
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, col);
        // Border
        if (change->isSelected()) {
            ImVec2 rowMax = ImGui::GetCursorScreenPos();

            // Full table width
            float tableMinX = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMin().x;
            float tableMaxX = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;

            ImGui::GetWindowDrawList()->AddRect(ImVec2(tableMinX, rowMin.y), ImVec2(tableMaxX, rowMax.y), change->isValid() ? Widgets::colValid.second : Widgets::colInvalid.second, 0.0f, 0, 2.0f);
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
            ImGui::TableNextRow();
            ImVec2 rowMin = ImGui::GetCursorScreenPos();
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
            if (rowChange.has_value()) { drawRowHighlights(*rowChange, rowMin); }

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
                    changeTracker.removeChanges((*rowChange)->getKey());
                } else {
                    Change::colValMap cvMap{};
                    changeTracker.addChange(Change{cvMap, changeType::DELETE_ROW, dbService.getTable(table), indexes.pKeyId});
                    if (edit.whichId == indexes.pKeyId) { edit.whichId = INVALID_ID; }
                }
            }
            ImGui::EndDisabled();

            // Edit row
            ImGui::SameLine();
            if (rowChange.has_value()) { ImGui::BeginDisabled((*rowChange)->getType() == changeType::DELETE_ROW); }
            if (ImGui::Button("EDIT")) {
                if (edit.whichId == indexes.pKeyId) {
                    edit.whichId = INVALID_ID;
                } else {
                    edit.whichId = indexes.pKeyId;
                }
            }
            if (rowChange.has_value()) { ImGui::EndDisabled(); }

            ImGui::PopID();
            ++indexes.loopId;
        }
        drawInsertionChanges(table);
    }

    void createTableSplitters(bool dataFresh) {
        if (ImGui::BeginTabBar("MainTabs")) {
            for (const auto& [table, data] : dbData->headers) {
                ImGuiTabItemFlags flags = 0;
                if (selectedTable == table) { flags |= ImGuiTabItemFlags_SetSelected; }
                if (ImGui::BeginTabItem(table.c_str(), nullptr, flags)) {
                    ImGui::BeginDisabled(!dataFresh);
                    if (selectedTable == table) { selectedTable.clear(); }
                    drawTableView(table, data.data);
                    ImGui::EndTabItem();
                    ImGui::EndDisabled();
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

    void drawChangeOverview(bool dataFresh) {
        ImGui::Text("CHANGE OVERVIEW");
        ImGui::BeginDisabled(!dataFresh);
        if (ImGui::Button("Execute all")) { changeExe.requestChangeApplication(sqlAction::EXECUTE); }
        for (const auto& [table, _] : uiChanges->idMappedChanges) {
            ImGui::Separator();
            ImGui::TextUnformatted(table.c_str());
            drawTableChangeOverview(table);
        }
        ImGui::EndDisabled();
    }

    void drawTableChangeOverview(const std::string& table) {
        if (!uiChanges->idMappedChanges.contains(table)) { return; }

        ImGui::PushID(table.c_str());

        for (const auto& [_, hash] : uiChanges->idMappedChanges.at(table)) {
            const Change& change = uiChanges->changes.at(hash);
            changeOverviewer.drawSingleChangeOverview(change);
        }

        ImGui::PopID();
    }

    void handleTableEvent() {
        const Widgets::event tableEvent = dbTable.getEvent();
        const bool handleEvent = tableEvent.type.mouse == Widgets::MOUSE_EVENT_TYPE::CLICK || tableEvent.type.action == Widgets::ACTION_TYPE::EDIT;
        if (!handleEvent) { return; }

        if (std::holds_alternative<Widgets::dataEvent>(tableEvent.origin)) {  // NO CHANGE EXISTS ON THIS ROW
            const Widgets::dataEvent event = std::get<Widgets::dataEvent>(tableEvent.origin);
            switch (tableEvent.type.action) {
                case Widgets::ACTION_TYPE::HEADER: {
                    const tHeaderVector& header = dbData->headers.at(event.tableName).data;
                    auto it = std::find_if(header.begin(), header.end(), [&](const tHeaderInfo& h) {
                        return h.name == event.headerName;
                    });
                    if (it != header.end()) { selectedTable = it->referencedTable; }
                    break;
                }
                case Widgets::ACTION_TYPE::REMOVE:
                    changeTracker.addChange(Change(Change::colValMap{}, changeType::DELETE_ROW, dbService.getTable(event.tableName), static_cast<std::size_t>(std::stoi(event.pKey))));
                    break;
                case Widgets::ACTION_TYPE::EDIT:
                    changeTracker.addChange(Change(tableEvent.cells, changeType::UPDATE_CELLS, dbService.getTable(event.tableName)), static_cast<std::size_t>(std::stoi(event.pKey)));
                    break;
                case Widgets::ACTION_TYPE::REQUEST_EDIT: {
                    const std::size_t pKeyId = static_cast<std::size_t>(std::stoi(event.pKey));
                    if (edit.whichId == pKeyId) {
                        edit.whichId = INVALID_ID;
                    } else {
                        edit.whichId = pKeyId;
                    }
                    break;
                }
                case Widgets::ACTION_TYPE::INSERT:
                    changeTracker.addChange(Change(tableEvent.cells, changeType::INSERT_ROW, dbService.getTable(event.tableName)));
                    break;
                default:
                    break;
            }
        } else {  // CHANGE ALREADY EXISTS ON THIS ROW
            const Change change = std::get<Change>(tableEvent.origin);
            switch (tableEvent.type.action) {
                case Widgets::ACTION_TYPE::REMOVE:
                    changeTracker.removeChanges(change.getKey());
                    break;
                case Widgets::ACTION_TYPE::EDIT:
                    changeTracker.addChange(change);
                    break;
                case Widgets::ACTION_TYPE::REQUEST_EDIT: {
                    const std::size_t pKeyId = change.getRowId();
                    if (edit.whichId == pKeyId) {
                        edit.whichId = INVALID_ID;
                    } else {
                        edit.whichId = pKeyId;
                    }
                    break;
                }
                case Widgets::ACTION_TYPE::SELECT:
                    changeTracker.toggleChangeSelect(change.getKey());
                    break;
                default:
                    break;
            }
        }
        dbTable.popEvent();
    }

   public:
    void setData(std::shared_ptr<const completeDbData> newData) {
        dbData = newData;
        dbTable.setData(newData);
    }

    void setChangeData(std::shared_ptr<uiChangeInfo> changeData) {
        uiChanges = changeData;
        dbTable.setChangeData(changeData);
    }

    void run(bool dataFresh) {
        if (ImGui::BeginTabBar("Main")) {
            ImGuiTabItemFlags flags = 0;
            if (!selectedTable.empty()) { flags |= ImGuiTabItemFlags_SetSelected; };
            if (ImGui::BeginTabItem("Tables", nullptr, flags)) {
                createTableSplitters(dataFresh);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Changes")) {
                drawChangeOverview(dataFresh);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("table dev")) {
                if (ImGui::BeginTabBar("MainTabs")) {
                    for (const auto& [table, data] : dbData->headers) {
                        if (ImGui::BeginTabItem(table.c_str())) {
                            ImGui::BeginDisabled(!dataFresh);
                            dbTable.drawTable(table);
                            handleTableEvent();
                            ImGui::EndDisabled();
                            ImGui::EndTabItem();
                        }
                    }
                    ImGui::EndTabBar();
                }
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }

    DbVisualizer(DbService& cDbService, ChangeTracker& cChangeTracker, ChangeExeService& cChangeExe, Logger& cLogger) : dbService(cDbService), changeTracker(cChangeTracker), changeExe(cChangeExe), logger(cLogger) {}
};