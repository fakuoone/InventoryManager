#pragma once

#include "imgui.h"

#include "dbService.hpp"
#include "dbInterface.hpp"
#include "changeTracker.hpp"
#include "changeExeService.hpp"

#include "logger.hpp"

class DbVisualizer {
   private:
    ChangeTracker& changeTracker;
    ChangeExeService& changeExe;
    Logger& logger;

    std::shared_ptr<const completeDbData> dbData;
    // TODO: wahrscheinlich sollte sich app darum kümmern und hier nur eine REferenz? rowMappedChanges sollte nur in changeTracker existieren, und hier wird angefragt?
    Change::ctRMD rowMappedChanges;
    Change::chHashM changes;
    Change::chHashV sucChanges;
    bool changesBeingApplied{false};

    void drawInsertionChanges(const std::string& table, std::size_t lastRow) {
        if (!rowMappedChanges.contains(table) || !dbData->headers.contains(table)) { return; }
        ImGui::TableNextRow();
        for (const auto& [_, hash] : rowMappedChanges.at(table)) {
            const Change& change = changes.at(hash);
            if (change.getType() == changeType::INSERT_ROW) {
                ++lastRow;
                for (const auto& header : dbData->headers.at(table)) {
                    ImGui::TableNextColumn();
                    const auto& cellChanges = change.getCells();
                    if (!cellChanges.contains(header)) {
                        ImGui::TextUnformatted("NOT PROVIDED");
                        continue;
                    }
                    ImGui::TextUnformatted(cellChanges.at(header).c_str());
                }
                /*
                for (const auto& [column, cell] : change.getCells()) {
                    const tStringVector& headers = dbData->headers.at(table);
                    if (std::find(headers.begin(), headers.end(), column) == headers.end()) { logger.pushLog(Log{std::format("ERROR: Change {} has entry {} for invalid column {}.", hash, cell, column)}); }
                    }
                    */
                ImGui::TableNextColumn();
                ImGui::PushID(static_cast<int>(lastRow));

                if (ImGui::Button("RUN")) {
                    changesBeingApplied = true;
                    changeExe.requestChangeApplication(Change{change}, sqlAction::EXECUTE);
                }
            }

            ImGui::SameLine();
            if (ImGui::Button("x")) { changeTracker.removeChange(change.getHash()); }
            ImGui::PopID();
        }
    }

    void drawChange(const Change& change) {
        switch (change.getType()) {
            case changeType::DELETE_ROW:
                break;
            case changeType::INSERT_ROW:
                break;
            case changeType::UPDATE_CELLS:
                break;

            default:
                break;
        }
    }

    std::expected<const Change*, bool> getChangeOfRow(const std::string& table, const tStringVector& headers, const std::size_t row) {
        if (!rowMappedChanges.contains(table)) { return std::unexpected(false); }
        for (const auto& header : headers) {
            if (header == "id") {
                const auto& data = dbData->tableRows.at(table).at(header);
                if (data.size() <= row) {
                    //logger.pushLog(Log{std::format("ERROR: Getting change of ID {} in table {}. ID does not exist.", row, table)});
                    return std::unexpected(false);
                }

                const std::string& cell = data.at(row);
                std::size_t id;
                try {
                    id = static_cast<std::size_t>(std::stoi(cell));
                } catch (const std::exception& e) {
                    logger.pushLog(Log{std::format("ERROR: Getting change of ID {} in table {}: value {} is not an integer. Exception: {}", row, table, cell, e.what())});
                    return std::unexpected(false);
                }

                if (rowMappedChanges.at(table).contains(id)) {
                    const std::size_t changeHash = rowMappedChanges.at(table).at(id);
                    return &changes.at(changeHash);
                }
                return std::unexpected(false);
            }
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

        size_t i = 0;
        bool maxNotReached = true;
        while (maxNotReached) {
            auto rowChange = getChangeOfRow(table, dbData->headers.at(table), i);
            ImGui::TableNextRow();
            for (const auto& header : dbData->headers.at(table)) {
                ImGui::TableNextColumn();
                if (!dbData->tableRows.at(table).contains(header)) { continue; }
                const auto& data = dbData->tableRows.at(table).at(header);
                if (data.size() <= i + 1) {
                    maxNotReached = false;
                    ImGui::TextUnformatted("-");
                    continue;
                }
                std::string cell = data.at(i);
                if (rowChange.has_value()) {
                    std::string newCellValue = rowChange.value()->getCell();
                    if (!newCellValue.empty()) { cell = std::format("{} | {}", cell, newCellValue); }
                }
                maxNotReached = true;
                ImGui::TextUnformatted(cell.c_str());
            }
            ++i;
        }
        drawInsertionChanges(table, i);
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
    void run() {
        if (dbData) {
            rowMappedChanges = changeTracker.getRowMappedData();
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