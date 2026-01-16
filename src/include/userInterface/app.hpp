#pragma once

#include <future>

#include "changeTracker.hpp"
#include "config.hpp"
#include "dbService.hpp"
#include "logger.hpp"
#include "changeExeService.hpp"

#include "userInterface/dbDataVisualizer.hpp"
#include "userInterface/imGuiDX11Context.hpp"

enum class AppState { DATA_OUTDATED, WAITING_FOR_DATA, DATA_READY, ENDING };

class App {
   private:
    ImGuiDX11Context imguiCtx;

    DbService& dbService;
    ChangeTracker& changeTracker;
    Config& config;
    Logger& logger;

    ChangeExeService changeExe{dbService, changeTracker, logger};
    DbVisualizer dbVisualizer{dbService, changeTracker, changeExe, logger};

    AppState appState{AppState::DATA_OUTDATED};
    std::shared_ptr<const completeDbData> dbData;
    bool dataAvailable{false};

    std::shared_ptr<uiChangeInfo> uiChanges;

    bool waitForData() {
        if (dataAvailable) { return false; }

        auto result = dbService.getCompleteData();
        if (!result) { return false; }

        dbData = *result;
        dbVisualizer.setData(dbData);
        changeTracker.setMaxPKeys(dbData->maxPKeys);
        if (!uiChanges) { uiChanges = std::make_shared<uiChangeInfo>(); }

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
            if (io.Framerate < 239) { logger.pushLog(Log{std::format("FPS: {}", io.Framerate)}); }
            ImGui::Text("FPS: %.1f", static_cast<double>(io.Framerate));
            ImGui::Text("Frame: %.3f ms", 1000.0 / static_cast<double>(io.Framerate));
            ImGui::End();
        }
    }

    bool handleAppState() {
        switch (appState) {
            case AppState::DATA_OUTDATED:
                dbService.startUp();
                appState = AppState::WAITING_FOR_DATA;
                break;
            case AppState::WAITING_FOR_DATA:
                if (waitForData()) { appState = AppState::DATA_READY; }
                break;
            case AppState::DATA_READY:
                if (false) { appState = AppState::DATA_OUTDATED; }
                break;
            case AppState::ENDING:
                return false;
            default:
                break;
        }
        return true;
    }

   public:
    App(DbService& cDbService, ChangeTracker& cChangeTracker, Config& cConfig, Logger& cLogger) : dbService(cDbService), changeTracker(cChangeTracker), config(cConfig), logger(cLogger) {}

    App(const App&) = delete;
    App& operator=(const App&) = delete;
    App(App&&) = delete;
    App& operator=(App&&) = delete;

    void supplyConfigString() {
        std::string dbString = config.setConfigString(std::filesystem::path{});  // OPTIONAL USER SUPPLIED CONFIG PATH
        dbService.initializeDbInterface(dbString);
    }

    void run() {
        supplyConfigString();
        bool running = true;
        while (running) {
            if (!imguiCtx.pollEvents()) {
                appState = AppState::ENDING;
                break;
            }

            running = handleAppState();

            if (!imguiCtx.beginFrame()) { continue; }
            if (appState == AppState::DATA_READY) {
                uiChanges = std::make_shared<uiChangeInfo>(changeTracker.getSnapShot());
                dbVisualizer.setChangeData(uiChanges);
                dbVisualizer.run();
                if (changeExe.isChangeApplicationDone()) { changeExe.getSuccessfulChanges(); }
            }

            drawFpsOverlay();

            imguiCtx.endFrame();
        }
    }
};
