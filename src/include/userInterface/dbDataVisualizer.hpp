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
    DbService& dbService_;
    ChangeTracker& changeTracker_;
    ChangeExeService& changeExe_;
    Logger& logger_;

    UI::DataStates& dataStates_;

    std::shared_ptr<const CompleteDbData> dbData_;

    std::shared_ptr<uiChangeInfo> uiChanges_;
    EditingData edit_;
    std::string selectedTable_;
    std::unordered_set<std::size_t> changeHighlight_;

    Widgets::DbTable dbTable_{edit_, selectedTable_, changeHighlight_, logger_};
    Widgets::ChangeOverviewer changeOverviewer_{changeTracker_, changeExe_, 60, changeHighlight_, selectedTable_};

    std::unordered_set<std::size_t> clickedChanges_;

    void drawChangeOverview() {
        ImGui::Text("CHANGE OVERVIEW");
        ImGui::BeginDisabled(dataStates_.dbData != UI::DataState::DATA_READY);
        if (ImGui::Button("Execute all")) { changeExe_.requestChangeApplication(SqlAction::EXECUTE); }

        for (const std::size_t rootKey : uiChanges_->roots) {
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
        bool containsKey = clickedChanges_.contains(key);
        const Change& change = uiChanges_->changes.at(key);
        bool isChildrenNotLast = false;
        if (change.hasParent()) {
            isChildrenNotLast = lastChild != key;
        } else if (containsKey && change.hasChildren()) {
            isChildrenNotLast = true;
        }

        if (drawChange(key, treeDepth, parent, isChildrenNotLast) == Widgets::MouseEventType::CLICK) { toggleNode(key); }

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
        if (!clickedChanges_.insert(key).second) { clickedChanges_.erase(key); }
    }

    Widgets::MouseEventType drawChange(const std::size_t key, std::size_t* visualDepth, const std::size_t parent, bool isChildrenNotLast) {
        const Change& change = uiChanges_->changes.at(key);
        return changeOverviewer_.drawSingleChangeOverview(change, visualDepth, parent, isChildrenNotLast);
    }

    void handleTableEvent() {
        const Widgets::Event tableEvent = dbTable_.getEvent();
        const bool handleEvent =
            tableEvent.type.mouse == Widgets::MouseEventType::CLICK || tableEvent.type.action == Widgets::ActionType::EDIT;
        if (!handleEvent) { return; }

        logger_.pushLog(Log{std::format("EVENT occured.")});
        if (std::holds_alternative<Widgets::DataEvent>(tableEvent.origin)) { // NO CHANGE EXISTS ON THIS ROW
            const Widgets::DataEvent event = std::get<Widgets::DataEvent>(tableEvent.origin);
            switch (tableEvent.type.action) {
            case Widgets::ActionType::HEADER: {
                const HeaderVector& header = dbData_->headers.at(event.tableName).data;
                auto it = std::find_if(header.begin(), header.end(), [&](const HeaderInfo& h) { return h.name == event.headerName; });
                if (it != header.end()) { selectedTable_ = it->referencedTable; }
                break;
            }
            case Widgets::ActionType::REMOVE:
                changeTracker_.addChange(Change(Change::colValMap{},
                                                ChangeType::DELETE_ROW,
                                                dbService_.getTable(event.tableName),
                                                static_cast<std::size_t>(std::stoi(event.pKey))));
                break;
            case Widgets::ActionType::EDIT:
                changeTracker_.addChange(Change(tableEvent.cells, ChangeType::UPDATE_CELLS, dbService_.getTable(event.tableName)),
                                         static_cast<std::size_t>(std::stoi(event.pKey)));
                break;
            case Widgets::ActionType::REQUEST_EDIT: {
                const std::size_t pKeyId = static_cast<std::size_t>(std::stoi(event.pKey));
                if (edit_.whichId == pKeyId) {
                    edit_.whichId = INVALID_ID;
                } else {
                    edit_.whichId = pKeyId;
                }
                break;
            }
            case Widgets::ActionType::INSERT:
                changeTracker_.addChange(Change(tableEvent.cells, ChangeType::INSERT_ROW, dbService_.getTable(event.tableName)));
                break;
            default:
                break;
            }
        } else { // CHANGE ALREADY EXISTS ON THIS ROW
            const Change change = std::get<Change>(tableEvent.origin);
            switch (tableEvent.type.action) {
            case Widgets::ActionType::REMOVE:
                changeTracker_.removeChanges(change.getKey());
                break;
            case Widgets::ActionType::EDIT:
                changeTracker_.addChange(change);
                break;
            case Widgets::ActionType::REQUEST_EDIT: {
                const std::size_t pKeyId = change.getRowId();
                if (edit_.whichId == pKeyId) {
                    edit_.whichId = INVALID_ID;
                } else {
                    edit_.whichId = pKeyId;
                }
                break;
            }
            case Widgets::ActionType::SELECT:
                changeTracker_.toggleChangeSelect(change.getKey());
                break;
            default:
                break;
            }
        }
        dbTable_.popEvent();
    }

  public:
    DbVisualizer(
        DbService& cDbService, ChangeTracker& cChangeTracker, ChangeExeService& cChangeExe, Logger& cLogger, UI::DataStates& cDataStates)
        : dbService_(cDbService), changeTracker_(cChangeTracker), changeExe_(cChangeExe), logger_(cLogger), dataStates_(cDataStates) {}

    void setData(std::shared_ptr<const CompleteDbData> newData) {
        dbData_ = newData;
        dbTable_.setData(newData);
    }

    void setChangeData(std::shared_ptr<uiChangeInfo> changeData) {
        uiChanges_ = changeData;
        dbTable_.setChangeData(changeData);
        changeOverviewer_.setChangeData(changeData);
    }

    void run() {
        if (ImGui::BeginTabBar("Main")) {
            ImGuiTabItemFlags flags = 0;
            if (!selectedTable_.empty()) { flags |= ImGuiTabItemFlags_SetSelected; };
            if (ImGui::BeginTabItem("Tables", nullptr, flags)) {
                if (ImGui::BeginTabBar("MainTabs")) {
                    if (dataStates_.dbData == UI::DataState::DATA_OUTDATED || dataStates_.dbData == UI::DataState::DATA_READY) {
                        for (const auto& [table, data] : dbData_->headers) {
                            ImGuiTabItemFlags flagsHeader = ImGuiTabItemFlags_None;
                            if (selectedTable_ == table) {
                                selectedTable_.clear();
                                flagsHeader |= ImGuiTabItemFlags_SetSelected;
                            }
                            if (ImGui::BeginTabItem(table.c_str(), nullptr, flagsHeader)) {
                                ImGui::BeginDisabled(dataStates_.dbData != UI::DataState::DATA_READY);
                                dbTable_.drawTable(table);
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