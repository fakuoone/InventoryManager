#pragma once

#include <future>

#include "autoInv.hpp"
#include "changeExeService.hpp"
#include "changeTracker.hpp"
#include "config.hpp"
#include "dbService.hpp"
#include "logger.hpp"

#include "userInterface/autoInvVisualizer.hpp"
#include "userInterface/dbDataVisualizer.hpp"
#include "userInterface/imGuiDX11Context.hpp"

enum class AppState { INIT, DATA_OUTDATED, WAITING_FOR_DATA, DATA_READY, ENDING };

class App {
  private:
    ImGuiDX11Context imguiCtx;

    DbService& dbService;
    ChangeTracker& changeTracker;
    Config& config;
    AutoInv::ChangeGeneratorFromBom& bomReader;
    AutoInv::ChangeGeneratorFromOrder& orderReader;
    Logger& logger;

    ChangeExeService changeExe{dbService, changeTracker, logger};
    DbVisualizer dbVisualizer{dbService, changeTracker, changeExe, logger};
    AutoInv::BomVisualizer bomVisualizer{dbService, bomReader, logger};
    AutoInv::OrderVisualizer orderVisualizer{dbService, orderReader, logger};

    AppState appState{AppState::INIT};
    std::shared_ptr<const completeDbData> dbData;
    bool dataAvailable{false};

    std::shared_ptr<uiChangeInfo> uiChanges;

    std::array<char, 256> csvBuffer;

    bool waitForData() {
        if (dataAvailable) {
            return false;
        }

        auto result = dbService.getCompleteData();
        if (!result) {
            return false;
        }

        dbData = *result;
        dbVisualizer.setData(dbData);
        bomVisualizer.setData(dbData);
        orderVisualizer.setData(dbData);

        changeTracker.setMaxPKeys(dbData->maxPKeys);
        if (!uiChanges) {
            uiChanges = std::make_shared<uiChangeInfo>();
        }

        return true;
    }

    void drawFpsOverlay() {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const float PAD = 10.0f;

        ImVec2 pos(viewport->WorkPos.x + viewport->WorkSize.x - PAD, viewport->WorkPos.y + viewport->WorkSize.y - PAD);

        ImGui::SetNextWindowPos(pos, ImGuiCond_Always, ImVec2(1.0f, 1.0f));
        ImGui::SetNextWindowBgAlpha(0.35f);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing |
                                 ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;

        if (ImGui::Begin("FPSOverlay", nullptr, flags)) {
            ImGuiIO& io = ImGui::GetIO();
            // if (io.Framerate < 239) { logger.pushLog(Log{std::format("FPS:
            // {}", io.Framerate)}); }
            ImGui::Text("FPS: %.1f", static_cast<double>(io.Framerate));
            ImGui::Text("Frame: %.3f ms", 1000.0 / static_cast<double>(io.Framerate));
            ImGui::End();
        }
    }

    bool handleAppState() {
        switch (appState) {
        case AppState::INIT:
            dataAvailable = false;
            dbService.startUp();
            appState = AppState::WAITING_FOR_DATA;
            break;
        case AppState::DATA_OUTDATED: {
            // TEST: request data anyways
            dataAvailable = false;
            uiChanges = std::make_shared<uiChangeInfo>(changeTracker.getSnapShot());
            dbVisualizer.setChangeData(uiChanges);
            dbVisualizer.run(false);
            ImVec2 buttonSize = ImGui::CalcTextSize("REFETCH");
            buttonSize.x += ImGui::GetStyle().FramePadding.x * 2.0f;
            buttonSize.y += ImGui::GetStyle().FramePadding.y * 2.0f;

            ImVec2 padding = ImGui::GetStyle().WindowPadding;

            // Top-right corner of the content region
            ImGui::SetCursorPos(ImVec2(ImGui::GetWindowContentRegionMax().x - buttonSize.x - padding.x, padding.y));
            if (ImGui::Button("REFETCH")) {
                dbService.refetch();
                appState = AppState::WAITING_FOR_DATA;
            }
            break;
        }
        case AppState::WAITING_FOR_DATA:
            if (waitForData()) {
                dataAvailable = true;
                appState = AppState::DATA_READY;
            }
            break;
        case AppState::DATA_READY:
            uiChanges = std::make_shared<uiChangeInfo>(changeTracker.getSnapShot());
            dbVisualizer.setChangeData(uiChanges);
            dbVisualizer.run(true);
            if (changeExe.isChangeApplicationDone()) {
                changeExe.getSuccessfulChanges();
                appState = AppState::DATA_OUTDATED;
            }
            break;
        case AppState::ENDING:
            return false;
        default:
            break;
        }
        return true;
    }

    void initFont(const std::string& font) {
        if (!font.empty()) {
            ImGuiIO& io = ImGui::GetIO();
            ImFont* fontPtr = io.Fonts->AddFontFromFileTTF(font.c_str(), 16.0f);
            IM_ASSERT(fontPtr != nullptr);
        }
    }

    void showBom() {
        bool enterPressed = ImGui::InputText("##edit", csvBuffer.data(), BUFFER_SIZE, ImGuiInputTextFlags_EnterReturnsTrue);
        if (enterPressed || ImGui::IsItemDeactivatedAfterEdit()) {
            try {
                bomReader.read(std::filesystem::path(std::string(csvBuffer.data())));
            } catch (const std::exception& e) {
                logger.pushLog(Log{std::format("ERROR reading bom: {}", e.what())});
            }
        }
        bomVisualizer.run(dataAvailable);
    }

    void showOrder() {
        bool enterPressed = ImGui::InputText("##edit", csvBuffer.data(), BUFFER_SIZE, ImGuiInputTextFlags_EnterReturnsTrue);
        if (enterPressed || ImGui::IsItemDeactivatedAfterEdit()) {
            try {
                orderReader.read(std::filesystem::path(std::string(csvBuffer.data())));
            } catch (const std::exception& e) {
                logger.pushLog(Log{std::format("ERROR reading order: {}", e.what())});
            }
        }

        // TODO: Validate mappings
        if (true) {
            float buttonWidth = ImGui::CalcTextSize("Commit Mapping").x + ImGui::GetStyle().FramePadding.x * 2.0f;
            float rightEdge = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;

            ImGui::SameLine();
            ImGui::SetCursorPosX(rightEdge - buttonWidth);

            if (ImGui::Button("Commit Mapping")) {
                orderVisualizer.commitMappings();
            }
        }

        orderVisualizer.run(dataAvailable);
    }

  public:
    App(DbService& cDbService,
        ChangeTracker& cChangeTracker,
        Config& cConfig,
        AutoInv::ChangeGeneratorFromBom& cBomReader,
        AutoInv::ChangeGeneratorFromOrder& cOrderReader,

        Logger& cLogger)
        : dbService(cDbService), changeTracker(cChangeTracker), config(cConfig), bomReader(cBomReader), orderReader(cOrderReader),
          logger(cLogger) {}

    App(const App&) = delete;
    App& operator=(const App&) = delete;
    App(App&&) = delete;
    App& operator=(App&&) = delete;

    void supplyConfigString() {
        std::string dbString = config.setConfigString(std::filesystem::path{}); // OPTIONAL USER SUPPLIED CONFIG PATH
        initFont(config.getFont());
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
            if (!imguiCtx.beginFrame()) {
                continue;
            }

            // TODO: Fix this weird main loop
            if (ImGui::BeginTabBar("Main")) {
                if (ImGui::BeginTabItem("Database")) {
                    running = handleAppState();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("BOM")) {
                    showBom();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Order")) {
                    showOrder();
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }

            ImGui::ShowMetricsWindow();
            drawFpsOverlay();
            imguiCtx.endFrame();
        }
    }
};
