#pragma once

#include "imgui.h"

#include "dbInterface.hpp"

class DbVisualizer {
   private:
    void createColumns(const tStringVector& columns) {
        for (const auto& column : columns) {
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(column.c_str());
        }
        ImGui::TableNextRow();
    }

    void createRows(const tColumnDataMap& columns) {
        // TODO: SPalten mit den hier vorhandenen abgleichen oder vertrauen? DIe Sortierung muss gleich sein, sonst bugs
        /*
        1. ZEilenahzahl bestimmen (minimum)
        2. äußerer Loop über diese Anzahl
        3. Innerer Loop geht von map-EIntrag zu map-eintrag
        */
        bool maxNotReached = true;
        size_t i = 0;
        while (maxNotReached) {
            for (const auto& [column, data] : columns) {
                if (data.size() - 1 < i) {
                    maxNotReached = false;
                } else {
                    maxNotReached = true;
                    ImGui::TextUnformatted(data.at(i).c_str());
                }
                ImGui::TableNextColumn();
            }
            ImGui::TableNextRow();
            ++i;
        }
    }

    void createTableSplitters(const completeDbData& dbData) {
        // TODO: wenn daten ans ui übergeben werden, sollten sie geprüft werden. nicht zyklisch
        if (ImGui::BeginTabBar("MainTabs")) {
            for (const auto& [table, data] : dbData.headers) {
                if (ImGui::BeginTabItem(table.c_str())) {
                    if (ImGui::BeginTable("ColumnsTable", static_cast<int>(data.size()), ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                        createColumns(data);
                        createRows(dbData.tableRows.at(table));
                        ImGui::EndTable();
                    }
                    ImGui::EndTabItem();
                }
            }
            ImGui::EndTabBar();
        }
    }

   public:
    void run(const completeDbData& dbData) { createTableSplitters(dbData); }

    DbVisualizer() {}
};