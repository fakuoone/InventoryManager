#pragma once

#include <future>
#include <unordered_map>

#include "changeTracker.hpp"
#include "config.hpp"
#include "dbService.hpp"
#include "logger.hpp"

enum class AppState { RUNNING, ENDING };

class App {
   private:
    DbService& dbService;
    ChangeTracker& changeTracker;
    Config& config;
    Logger& logger;

    AppState appState{AppState::RUNNING};
    completeDbData dbData;
    bool dataAvailable{false};
    bool changesCommitted{false};

    void changeData(Change<int> change) {
        if (dataAvailable) {
            changeTracker.addChange(change);
        }
    }

    std::future<std::vector<std::size_t>> executeChanges(sqlAction action) { return dbService.requestChangeApplication(std::move(changeTracker.getChanges())); }

   public:
    App(DbService& cDbService, ChangeTracker& cChangeTracker, Config& cConfig, Logger& cLogger) : dbService(cDbService), changeTracker(cChangeTracker), config(cConfig), logger(cLogger) {}

    void init() {}

    void supplyConfigString() {
        config.setConfigString("B:/Programmieren/C/InventoryManager/config/database.json");
        dbService.initializeDbInterface(config.getDatabaseString());
    }

    void run() {
        auto fCompleteDbData = dbService.startUp();
        std::future<std::vector<std::size_t>> fApplyChanges;

        supplyConfigString();
        while (appState == AppState::RUNNING) {
            if (fCompleteDbData.valid() && fCompleteDbData.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
                dbData = fCompleteDbData.get();
                dataAvailable = true;
                logger.pushLog(Log{"UI got the data."});

                testMakeChanges();
                fApplyChanges = executeChanges(sqlAction::EXECUTE);
            }
            // TODO: DO this only once
            if (!changesCommitted && fApplyChanges.valid() && fApplyChanges.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
                changeTracker.removeChanges(fApplyChanges.get());
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
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
