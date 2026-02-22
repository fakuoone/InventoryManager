#pragma once

#include "pch.hpp"

#include "changeExeService.hpp"
#include "changeTracker.hpp"
#include "dbInterface.hpp"
#include "dbService.hpp"

#include "logger.hpp"

#include "dataTypes.hpp"
#include "userInterface/widgets.hpp"

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

    UI::DataStates& dataStates;

    std::shared_ptr<const CompleteDbData> dbData;

    std::shared_ptr<uiChangeInfo> uiChanges;
    editingData edit;
    std::string selectedTable;
    std::unordered_set<std::size_t> changeHighlight;

    Widgets::DbTable dbTable{edit, selectedTable, changeHighlight, logger};
    Widgets::ChangeOverviewer changeOverviewer{changeTracker, changeExe, 60, changeHighlight, selectedTable};

    std::unordered_set<std::size_t> clickedChanges;

    void drawChangeOverview() {
        ImGui::Text("CHANGE OVERVIEW");
        ImGui::BeginDisabled(dataStates.dbData != UI::DataState::DATA_READY);
        if (ImGui::Button("Execute all")) {
            changeExe.requestChangeApplication(SqlAction::EXECUTE);
        }

        for (const std::size_t rootKey : uiChanges->roots) { // TODO segfault when switching tab before db data is ready
            std::size_t depth = 0;
            drawChangesTree(rootKey, &depth, INVALID_ID, INVALID_ID);
        }

        /*
        for (const auto& [table, _] : uiChanges->idMappedChanges) {
            ImGui::Separator();
            ImGui::TextUnformatted(table.c_str());
            drawTableChangeOverview(table);
        }
        */
        ImGui::EndDisabled();
    }

    void drawChangesTree(const std::size_t key, std::size_t* treeDepth, const std::size_t lastChild, const std::size_t parent) {
        bool containsKey = clickedChanges.contains(key);
        const Change& change = uiChanges->changes.at(key);
        bool isChildrenNotLast = false;
        if (change.hasParent()) {
            isChildrenNotLast = lastChild != key;
        } else if (containsKey && change.hasChildren()) {
            isChildrenNotLast = true;
        }

        if (drawChange(key, treeDepth, parent, isChildrenNotLast) == Widgets::MouseEventType::CLICK) {
            toggleNode(key);
        }

        if (containsKey) {
            (*treeDepth)++;
            const std::vector<std ::size_t>& children = change.getChildren();
            for (std::size_t childKey : children) {
                drawChangesTree(childKey, treeDepth, children.back(), key);
            }
            (*treeDepth)--;
        }
    }

    void toggleNode(std::size_t key) {
        if (!clickedChanges.insert(key).second) {
            clickedChanges.erase(key);
        }
    }

    Widgets::MouseEventType drawChange(const std::size_t key, std::size_t* visualDepth, const std::size_t parent, bool isChildrenNotLast) {
        const Change& change = uiChanges->changes.at(key);
        return changeOverviewer.drawSingleChangeOverview(change, visualDepth, parent, isChildrenNotLast);
    }

    void handleTableEvent() {
        const Widgets::Event tableEvent = dbTable.getEvent();
        const bool handleEvent =
            tableEvent.type.mouse == Widgets::MouseEventType::CLICK || tableEvent.type.action == Widgets::ActionType::EDIT;
        if (!handleEvent) {
            return;
        }

        if (std::holds_alternative<Widgets::DataEvent>(tableEvent.origin)) { // NO CHANGE EXISTS ON THIS ROW
            const Widgets::DataEvent event = std::get<Widgets::DataEvent>(tableEvent.origin);
            switch (tableEvent.type.action) {
            case Widgets::ActionType::HEADER: {
                const HeaderVector& header = dbData->headers.at(event.tableName).data;
                auto it = std::find_if(header.begin(), header.end(), [&](const HeaderInfo& h) { return h.name == event.headerName; });
                if (it != header.end()) {
                    selectedTable = it->referencedTable;
                }
                break;
            }
            case Widgets::ActionType::REMOVE:
                changeTracker.addChange(Change(Change::colValMap{},
                                               ChangeType::DELETE_ROW,
                                               dbService.getTable(event.tableName),
                                               static_cast<std::size_t>(std::stoi(event.pKey))));
                break;
            case Widgets::ActionType::EDIT:
                changeTracker.addChange(Change(tableEvent.cells, ChangeType::UPDATE_CELLS, dbService.getTable(event.tableName)),
                                        static_cast<std::size_t>(std::stoi(event.pKey)));
                break;
            case Widgets::ActionType::REQUEST_EDIT: {
                const std::size_t pKeyId = static_cast<std::size_t>(std::stoi(event.pKey));
                if (edit.whichId == pKeyId) {
                    edit.whichId = INVALID_ID;
                } else {
                    edit.whichId = pKeyId;
                }
                break;
            }
            case Widgets::ActionType::INSERT:
                changeTracker.addChange(Change(tableEvent.cells, ChangeType::INSERT_ROW, dbService.getTable(event.tableName)));
                break;
            default:
                break;
            }
        } else { // CHANGE ALREADY EXISTS ON THIS ROW
            const Change change = std::get<Change>(tableEvent.origin);
            switch (tableEvent.type.action) {
            case Widgets::ActionType::REMOVE:
                changeTracker.removeChanges(change.getKey());
                break;
            case Widgets::ActionType::EDIT:
                changeTracker.addChange(change);
                break;
            case Widgets::ActionType::REQUEST_EDIT: {
                const std::size_t pKeyId = change.getRowId();
                if (edit.whichId == pKeyId) {
                    edit.whichId = INVALID_ID;
                } else {
                    edit.whichId = pKeyId;
                }
                break;
            }
            case Widgets::ActionType::SELECT:
                changeTracker.toggleChangeSelect(change.getKey());
                break;
            default:
                break;
            }
        }
        dbTable.popEvent();
    }

  public:
    DbVisualizer(
        DbService& cDbService, ChangeTracker& cChangeTracker, ChangeExeService& cChangeExe, Logger& cLogger, UI::DataStates& cDataStates)
        : dbService(cDbService), changeTracker(cChangeTracker), changeExe(cChangeExe), logger(cLogger), dataStates(cDataStates) {}

    void setData(std::shared_ptr<const CompleteDbData> newData) {
        dbData = newData;
        dbTable.setData(newData);
    }

    void setChangeData(std::shared_ptr<uiChangeInfo> changeData) {
        uiChanges = changeData;
        dbTable.setChangeData(changeData);
        changeOverviewer.setChangeData(changeData);
    }

    void run() {
        if (ImGui::BeginTabBar("Main")) {
            ImGuiTabItemFlags flags = 0;
            if (!selectedTable.empty()) {
                flags |= ImGuiTabItemFlags_SetSelected;
            };
            if (ImGui::BeginTabItem("Tables", nullptr, flags)) {
                if (ImGui::BeginTabBar("MainTabs")) {
                    if (dataStates.dbData == UI::DataState::DATA_OUTDATED || dataStates.dbData == UI::DataState::DATA_READY) {
                        for (const auto& [table, data] : dbData->headers) {
                            ImGuiTabItemFlags flagsHeader = ImGuiTabItemFlags_None;
                            if (selectedTable == table) {
                                selectedTable.clear();
                                flagsHeader |= ImGuiTabItemFlags_SetSelected;
                            }
                            if (ImGui::BeginTabItem(table.c_str(), nullptr, flagsHeader)) {
                                ImGui::BeginDisabled(dataStates.dbData != UI::DataState::DATA_READY);
                                dbTable.drawTable(table);
                                handleTableEvent();
                                ImGui::EndDisabled();
                                ImGui::EndTabItem();
                            }
                        }
                    }
                    ImGui::EndTabBar();
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Changes")) {
                drawChangeOverview();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
};