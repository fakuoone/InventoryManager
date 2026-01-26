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

#include "imgui_internal.h"

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
    DbVisualizer(DbService& cDbService, ChangeTracker& cChangeTracker, ChangeExeService& cChangeExe, Logger& cLogger) : dbService(cDbService), changeTracker(cChangeTracker), changeExe(cChangeExe), logger(cLogger) {}

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
            if (ImGui::BeginTabItem("Changes")) {
                drawChangeOverview(dataFresh);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
};