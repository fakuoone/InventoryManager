#pragma once

#include "imgui.h"

#include "dbInterface.hpp"
#include "changeTracker.hpp"

#include "logger.hpp"

class DbVisualizer {
   private:
    ChangeTracker& changeTracker;
    const completeDbData& dbData;
    Logger& logger;

    void createColumns(const std::string& table) {
        for (const auto& column : dbData.headers.at(table)) {
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(column.c_str());
        }
        ImGui::TableNextRow();
    }

    void createRows(const std::string& table) {
        if (!dbData.headers.contains(table)) { return; }
        size_t i = 0;
        bool maxNotReached = true;
        while (maxNotReached) {
            for (const auto& header : dbData.headers.at(table)) {
                ImGui::TableNextColumn();
                if (!dbData.tableRows.contains(table)) { return; }
                if (!dbData.tableRows.at(table).contains(header)) { continue; }
                const auto& data = dbData.tableRows.at(table).at(header);
                if (data.size() <= i) {
                    maxNotReached = false;
                    ImGui::TextUnformatted("-");
                } else {
                    maxNotReached = true;
                    ImGui::TextUnformatted(data.at(i).c_str());
                }
            }
            ImGui::TableNextRow();
            ++i;
        }
    }

    void createTableSplitters() {
        if (ImGui::BeginTabBar("MainTabs")) {
            for (const auto& [table, data] : dbData.headers) {
                if (ImGui::BeginTabItem(table.c_str())) {
                    if (ImGui::BeginTable("ColumnsTable", static_cast<int>(data.size()), ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
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
    void run() { createTableSplitters(); }

    DbVisualizer(ChangeTracker& cChangeTracker, const completeDbData& cDbData, Logger& cLogger) : changeTracker(cChangeTracker), dbData(cDbData), logger(cLogger) {}
};