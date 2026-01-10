#pragma once

#include <future>

#include "changeTracker.hpp"
#include "config.hpp"
#include "dbService.hpp"
#include "logger.hpp"
#include "changeExeService.hpp"

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

    ChangeExeService changeExe{dbService, changeTracker, logger};
    DbVisualizer dbVisualizer{changeTracker, changeExe, logger};

    AppState appState{AppState::RUNNING};
    std::shared_ptr<const completeDbData> dbData;
    bool dataAvailable{false};

    std::future<Change::chHashV> fApplyChanges;

    void changeData(Change change) {
        if (dataAvailable) { changeTracker.addChange(change); }
    }

    bool waitForData() {
        if (dataAvailable) { return false; }

        auto result = dbService.getCompleteData();
        if (!result) { return false; }

        dbData = *result;
        dbVisualizer.setData(dbData);
        dataAvailable = true;
        return true;
    }

    void drawFpsOverlay() {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const float PAD = 10.0f;

        ImVec2 pos(viewport->WorkPos.x + viewport->WorkSize.x - PAD, viewport->WorkPos.y + viewport->WorkSize.y - PAD);

        ImGui::SetNextWindowPos(pos, ImGuiCond_Always, ImVec2(1.0f, 1.0f));
        ImGui::SetNextWindowBgAlpha(0.35f);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;

        if (ImGui::Begin("FPSOverlay", nullptr, flags)) {
            ImGuiIO& io = ImGui::GetIO();
            ImGui::Text("FPS: %.1f", static_cast<double>(io.Framerate));
            ImGui::Text("Frame: %.3f ms", 1000.0 / static_cast<double>(io.Framerate));
            ImGui::End();
        }
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
        dbService.startUp();

        supplyConfigString();
        while (appState == AppState::RUNNING) {
            if (!imguiCtx.pollEvents()) {
                appState = AppState::ENDING;
                break;
            }

            // UI INDEPENDANT CODE
            if (waitForData()) { testMakeChanges(); }

            if (!imguiCtx.beginFrame()) { continue; }
            if (dataAvailable) {
                dbVisualizer.run();
                // const std::vector& < std::size_tdbVisualizer.getCommitedChanges()
            }

            drawFpsOverlay();

            imguiCtx.endFrame();
        }
    }

    void testMakeChanges() {
        Change::colValMap testmap;
        changeData(Change{testmap, changeType::INSERT_ROW, "categories", logger, 0});
        testmap.emplace("test", "2");
        testmap.emplace("test2", "3");
        changeData(Change{testmap, changeType::UPDATE_CELLS, "categories", logger, 0});
        testmap.emplace("test2", "3");
        changeData(Change{testmap, changeType::UPDATE_CELLS, "categories", logger, 0});
    }
};
