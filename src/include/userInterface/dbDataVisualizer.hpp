#pragma once

#include "imgui.h"

#include "dbInterface.hpp"

class DbVisualizer {
   private:
    void createTableSplitters(const completeDbData& dbData) {
        for (const auto& [table, data] : dbData.headers) {
            ImGui::Button(table.c_str());
        }
    }

   public:
    void run(const completeDbData& dbData) { createTableSplitters(dbData); }

    DbVisualizer() {}
};