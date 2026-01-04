#pragma once

#include <future>
#include <unordered_map>

#include "changeTracker.hpp"
#include "dbService.hpp"
#include "logger.hpp"

enum class AppState { RUNNING, ENDING };

class App {
   private:
    DbService& dbService;
    ChangeTracker& changeTracker;
    Logger& logger;
    AppState appState{AppState::RUNNING};
    completeDbData dbData;
    bool dataAvailable{false};

    void changeData(Change<int> change) {
        if (dataAvailable) {
            changeTracker.addChange(change);
        }
    }

   public:
    App(DbService& cDbService, ChangeTracker& cChangeTracker, Logger& cLogger) : dbService(cDbService), changeTracker(cChangeTracker), logger(cLogger) {}

    void init() {}

    void run() {
        auto fCompleteDbData = dbService.startUp();
        while (appState == AppState::RUNNING) {
            if (fCompleteDbData.valid() && fCompleteDbData.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
                dbData = fCompleteDbData.get();
                dataAvailable = true;
                logger.pushLog(Log{"UI got the data."});

                testMakeChanges();
            }
        }
    }

    void testMakeChanges() {
        std::unordered_map<std::string, std::string> testmap;
        changeData(Change{changeType::InsertRow, "categories", 0, testmap, logger});
        testmap.emplace("test", "2");
        testmap.emplace("test2", "3");
        changeData(Change{changeType::UpdateCells, "categories", 0, testmap, logger});
        testmap.emplace("test2", "3");
        changeData(Change{changeType::UpdateCells, "categories", 0, testmap, logger});
    }
};
