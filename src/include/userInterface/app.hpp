#pragma once

#include <future>
#include <unordered_map>

#include "changeTracker.hpp"
#include "config.hpp"
#include "dbService.hpp"
#include "logger.hpp"
#include "userInterface/dbDataVisualizer.hpp"
#include "userInterface/imGuiDX11Context.hpp"

enum class AppState { RUNNING, ENDING };

class App {
   private:
    ImGuiDX11Context imguiCtx;

    DbService& dbService;
    ChangeTracker& changeTracker;
    Config& config;
    Logger& logger;

    AppState appState{AppState::RUNNING};
    completeDbData dbData;
    std::future<completeDbData> fCompleteDbData;
    bool dataAvailable{false};

    std::future<std::vector<std::size_t>> fApplyChanges;
    bool changesCommitted{false};

    DbVisualizer dbVisualizer{changeTracker, dbData, logger};

    void changeData(Change<int> change) {
        if (dataAvailable) { changeTracker.addChange(change); }
    }

    std::future<std::vector<std::size_t>> executeChanges(sqlAction action) { return dbService.requestChangeApplication(changeTracker.getChanges(), action); }

    bool waitForData() {
        if (fCompleteDbData.valid() && fCompleteDbData.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
            dbData = fCompleteDbData.get();
            if (validateCompleteDbData()) {
                dataAvailable = true;
                return true;
            }
        }
        return false;
    }

    bool validateCompleteDbData() {
        // tablecount matches everywhere
        std::size_t tableCount = dbData.tables.size();
        if (tableCount != dbData.headers.size() || tableCount != dbData.tableRows.size()) {
            logger.pushLog(Log{"ERROR: Table data is mismatching in size."});
            return false;
        }
        // all tables have headers
        for (const auto& table : dbData.tables) {
            if (!dbData.headers.contains(table)) {
                logger.pushLog(Log{std::format("ERROR: Table {} has no header information.", table)});
                return false;
            }
            // columns have the same values as rows have keys
            for (const auto& header : dbData.headers.at(table)) {
                if (!dbData.tableRows.at(table).contains(header)) {
                    logger.pushLog(Log{std::format("ERROR: Table {} has header {} which has no data.", table, header)});
                    return false;
                }
            }
        }
        return true;
    }

    bool waitForChangeApplication() {
        if (!changesCommitted && fApplyChanges.valid() && fApplyChanges.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) { return true; }
        return false;
    }

   public:
    App(DbService& cDbService, ChangeTracker& cChangeTracker, Config& cConfig, Logger& cLogger) : dbService(cDbService), changeTracker(cChangeTracker), config(cConfig), logger(cLogger) {}

    App(const App&) = delete;
    App& operator=(const App&) = delete;
    App(App&&) = delete;
    App& operator=(App&&) = delete;

    void supplyConfigString() {
        std::string dbString = config.setConfigString("B:/Programmieren/C/InventoryManager/config/database.json");
        dbService.initializeDbInterface(dbString);
    }

    void run() {
        fCompleteDbData = dbService.startUp();

        supplyConfigString();
        while (appState == AppState::RUNNING) {
            if (!imguiCtx.pollEvents()) {
                appState = AppState::ENDING;
                break;
            }

            // UI INDEPENDANT CODE
            if (waitForData()) {
                testMakeChanges();
                fApplyChanges = executeChanges(sqlAction::EXECUTE);
            }

            if (waitForChangeApplication()) { changeTracker.removeChanges(fApplyChanges.get()); }

            if (!imguiCtx.beginFrame()) { continue; }
            if (dataAvailable) { dbVisualizer.run(); }

            imguiCtx.endFrame();
        }
    }

    void testMakeChanges() {
        std::unordered_map<std::string, std::string> testmap;
        changeData(Change{changeType::INSERT_ROW, "categories", 0, testmap, logger});
        testmap.emplace("test", "2");
        testmap.emplace("test2", "3");
        changeData(Change{changeType::UPDATE_CELLS, "categories", 0, testmap, logger});
        testmap.emplace("test2", "3");
        changeData(Change{changeType::UPDATE_CELLS, "categories", 0, testmap, logger});
    }
};
