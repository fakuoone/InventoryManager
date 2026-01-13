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
    ChangeTracker& changeTracker;
    ChangeExeService& changeExe;
    Logger& logger;

    std::string primaryKey;
    std::shared_ptr<const completeDbData> dbData;
    // TODO: wahrscheinlich sollte sich app darum kümmern und hier nur eine REferenz? idMappedChanges sollte nur in changeTracker existieren, und hier wird angefragt?
    Change::ctPKMD idMappedChanges;
    Change::chHashM changes;
    Change::chHashV sucChanges;
    bool changesBeingApplied{false};

    editingData edit;

    rowIds drawInsertionChanges(const std::string& table, const std::size_t loopId, const std::size_t id) {
        if (!idMappedChanges.contains(table) || !dbData->headers.contains(table)) { return rowIds{loopId, id}; }
        ImGui::TableNextRow();
        std::size_t localId{id};
        std::size_t i{loopId};
        for (const auto& [_, hash] : idMappedChanges.at(table)) {
            const Change& change = changes.at(hash);
            if (change.getType() == changeType::INSERT_ROW) {
                ++i;
                for (const auto& header : dbData->headers.at(table)) {
                    ImGui::TableNextColumn();
                    const auto& cellChanges = change.getCells();
                    if (header == primaryKey) {
                        std::string pKeyFormat = std::format("({})", std::to_string(change.getRowId()));
                        ImGui::TextUnformatted(pKeyFormat.c_str());
                        continue;
                    }
                    if (!cellChanges.contains(header)) {
                        ImGui::TextUnformatted("-");
                        continue;
                    }
                    ImGui::TextUnformatted(cellChanges.at(header).c_str());
                }
                localId = change.getRowId();
                ImGui::TableNextColumn();
                ImGui::PushID(static_cast<int>(localId));

                if (ImGui::Button("RUN")) {
                    changesBeingApplied = true;
                    changeExe.requestChangeApplication(Change{change}, sqlAction::EXECUTE);
                }
                ImGui::SameLine();
                if (ImGui::Button("x")) { changeTracker.removeChange(change.getHash()); }
                ImGui::PopID();
            }
        }
        return rowIds{loopId + 1, localId + 1};
    }

    void drawUserInputRowFields(const std::string& table) {
        // TODO: Evtl in createRows in den loop integrieren?
        if (!dbData->headers.contains(table)) { return; }
        ImGui::PushID(1);
        Change::colValMap newChangeColVal{};
        ImGui::TableNextRow();
        edit.insertBuffer.resize(dbData->headers.size());
        std::size_t i = 0;
        for (const auto& header : dbData->headers.at(table)) {
            ImGui::TableNextColumn();
            if (!(header == primaryKey)) {
                ImGui::PushID(header.c_str());
                //std::snprintf(edit.buffer, BUFFER_SIZE, "%s", newCellValue.c_str());
                ImGui::InputText("##edit", edit.insertBuffer.at(i).data(), BUFFER_SIZE);
                std::string newValue = newChangeColVal[header] = std::string(edit.insertBuffer.at(i).data());
                ImGui::PopID();
            }
            ++i;
        }
        ImGui::TableNextColumn();
        if (ImGui::Button("ENTER")) { changeTracker.addChange(Change{newChangeColVal, changeType::INSERT_ROW, table, logger, changeTracker.getMaxPKey(table) + 1}); }
        ImGui::PopID();
    }

    void drawCellWithChange(std::expected<const Change*, bool> change, const std::string& originalCell, const std::string& table, const std::string& header, const std::size_t id) {
        std::string newCellValue{};
        changeType cType;
        // Get changed value for column and display it
        if (change.has_value()) {
            cType = ((*change)->getType());
            if (cType == changeType::UPDATE_CELLS) {
                newCellValue = (*change)->getCell(header);
                if (!newCellValue.empty()) {
                    ImGui::TextUnformatted(originalCell.c_str());
                    ImGui::SameLine();
                }
            }
        }

        if (newCellValue.empty()) { newCellValue = originalCell; }

        // Draw editable cell
        if (edit.whichIds.contains(id) && header != primaryKey) {
            ImGui::PushID(header.c_str());
            std::snprintf(edit.editBuffer.data(), BUFFER_SIZE, "%s", newCellValue.c_str());
            if (ImGui::InputText("##edit", edit.editBuffer.data(), BUFFER_SIZE, ImGuiInputTextFlags_EnterReturnsTrue)) {
                Change::colValMap newChangeColVal{{header, std::string(edit.editBuffer.data())}};
                changeTracker.addChange(Change{newChangeColVal, changeType::UPDATE_CELLS, table, logger, id});
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

    std::size_t getIdOfLoopIndex(const std::string& table, const std::size_t row) {
        if (!dbData->tableRows.contains(table)) { return INVALID_ID; }
        const auto& data = dbData->tableRows.at(table);
        if (!data.contains(primaryKey)) { return INVALID_ID; }
        const tStringVector& ids = data.at(primaryKey);
        if (ids.size() <= row) { return INVALID_ID; }
        try {
            return static_cast<std::size_t>(std::stoi(ids.at(row)));
        } catch (const std::exception& e) {
            logger.pushLog(Log{std::format("ERROR: Getting ID of row : value {} is not an integer. Exception: {}", ids.at(row), e.what())});
            return INVALID_ID;
        }
    }

    std::expected<const Change*, bool> getChangeOfRow(const std::string& table, const tStringVector& headers, const std::size_t id) {
        if (!idMappedChanges.contains(table)) { return std::unexpected(false); }
        if (id == INVALID_ID) { return std::unexpected(false); }
        if (idMappedChanges.at(table).contains(id)) {
            const std::size_t changeHash = idMappedChanges.at(table).at(id);
            return &changes.at(changeHash);
        }
        return std::unexpected(false);
    }

    void createColumns(const std::string& table) {
        ImGui::TableNextRow();
        for (const auto& column : dbData->headers.at(table)) {
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(column.c_str());
        }
    }

    void createRows(const std::string& table) {
        // Check data validity
        if (!dbData->headers.contains(table)) { return; }
        if (!dbData->tableRows.contains(table)) { return; }
        bool hasData = false;
        for (const auto& header : dbData->headers.at(table)) {
            if (header.size() > 0) {
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
            auto rowChange = getChangeOfRow(table, dbData->headers.at(table), indexes.pKeyId);
            ImGui::TableNextRow();  // TODO: Das Problem mit der leeren Zeile ist genau hier. Man merkt erst nach dem "nextrow"-AUfruf, dass keine Daten da sind
            // Draw every column for this row index
            ImGui::PushID(static_cast<int>(indexes.pKeyId));
            for (const auto& header : dbData->headers.at(table)) {
                ImGui::TableNextColumn();
                if (!dbData->tableRows.at(table).contains(header)) { continue; }
                // Get data for column
                const auto& data = dbData->tableRows.at(table).at(header);
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
            if (ImGui::Button("x")) {
                if (rowChange.has_value()) {
                    changeTracker.removeChange((*rowChange)->getHash());
                } else {
                    Change::colValMap cvMap{};
                    changeTracker.addChange(Change{cvMap, changeType::DELETE_ROW, table, logger, indexes.pKeyId});
                    if (edit.whichIds.contains(indexes.pKeyId)) { edit.whichIds.erase(indexes.pKeyId); }
                }
            }

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
        ++maxId;
        indexes = drawInsertionChanges(table, indexes.loopId, maxId);
    }

    void createTableSplitters() {
        if (ImGui::BeginTabBar("MainTabs")) {
            for (const auto& [table, data] : dbData->headers) {
                if (ImGui::BeginTabItem(table.c_str())) {
                    if (ImGui::BeginTable("ColumnsTable", static_cast<int>(data.size() + 1), ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                        createColumns(table);
                        createRows(table);
                        ImGui::EndTable();
                    }
                    ImGui::EndTabItem();
                }
            }
            ImGui::EndTabBar();
        }
    }

   public:
    void setData(std::shared_ptr<const completeDbData> newData) { dbData = std::move(newData); }

    void setPrimaryKey(const std::string& key) { primaryKey = key; }
    void run() {
        if (dbData) {
            idMappedChanges = changeTracker.getRowMappedData();
            changes = changeTracker.getChanges();
            createTableSplitters();
            if (changeExe.isChangeApplicationDone()) {
                sucChanges = changeExe.getSuccessfulChanges();
                changesBeingApplied = false;
            }
            if (changesBeingApplied) { ImGui::Dummy(ImVec2{50, 50}); }
        }
    }

    DbVisualizer(ChangeTracker& cChangeTracker, ChangeExeService& cChangeExe, Logger& cLogger) : changeTracker(cChangeTracker), changeExe(cChangeExe), logger(cLogger) {}
};