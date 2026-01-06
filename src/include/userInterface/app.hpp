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

    DbVisualizer dbVisualizer;

    void changeData(Change<int> change) {
        if (dataAvailable) { changeTracker.addChange(change); }
    }

    std::future<std::vector<std::size_t>> executeChanges(sqlAction action) { return dbService.requestChangeApplication(changeTracker.getChanges(), action); }

    bool waitForData() {
        if (fCompleteDbData.valid() && fCompleteDbData.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
            dbData = fCompleteDbData.get();
            dataAvailable = true;
            logger.pushLog(Log{"UI got the data."});
            return true;
        }
        return false;
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
            if (dataAvailable) { dbVisualizer.run(dbData); }

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
