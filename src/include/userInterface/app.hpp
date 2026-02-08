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
#include "userInterface/uiTypes.hpp"

class App {
  private:
    ImGuiDX11Context imguiCtx;

    DbService& dbService;
    ChangeTracker& changeTracker;
    Config& config;
    AutoInv::ChangeGeneratorFromBom& bomReader;
    AutoInv::ChangeGeneratorFromOrder& orderReader;
    Logger& logger;

    DataStates dataStates;

    ChangeExeService changeExe{dbService, changeTracker, logger};
    DbVisualizer dbVisualizer{dbService, changeTracker, changeExe, logger, dataStates};
    AutoInv::BomVisualizer bomVisualizer{dbService, bomReader, logger, dataStates};
    AutoInv::OrderVisualizer orderVisualizer{dbService, orderReader, logger, dataStates};

    std::shared_ptr<const completeDbData> dbData;
    std::shared_ptr<uiChangeInfo> uiChanges;

    std::array<char, 256> csvBuffer;

    bool waitForDbData() {
        if (dataStates.dbData == DataState::DATA_READY) {
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
        bomReader.setData(dbData);
        orderReader.setData(dbData);

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

    void handleDataState() {
        switch (dataStates.dbData) {
        case DataState::INIT:
            dbService.startUp();
            dataStates.dbData = DataState::WAITING_FOR_DATA;
            break;
        case DataState::DATA_OUTDATED: {
            dbService.refetch();
            uiChanges = std::make_shared<uiChangeInfo>(changeTracker.getSnapShot());
            dbVisualizer.setChangeData(uiChanges);
            dataStates.dbData = DataState::WAITING_FOR_DATA;
            break;
        }
        case DataState::WAITING_FOR_DATA:
            if (waitForDbData()) {
                dataStates.dbData = DataState::DATA_READY;
            }
            break;
        case DataState::DATA_READY:
            uiChanges = std::make_shared<uiChangeInfo>(changeTracker.getSnapShot());
            dbVisualizer.setChangeData(uiChanges);
            if (changeExe.isChangeApplicationDone()) {
                changeExe.getSuccessfulChanges();
                dataStates.dbData = DataState::DATA_OUTDATED;
            }
            break;
        default:
            break;
        }
    }

    void initFont(const std::string& font) {
        if (!font.empty()) {
            ImGuiIO& io = ImGui::GetIO();
            ImFont* fontPtr = io.Fonts->AddFontFromFileTTF(font.c_str(), 16.0f);
            IM_ASSERT(fontPtr != nullptr);
        }
    }

    void drawDb() {
        dbVisualizer.run();
        ImVec2 buttonSize = ImGui::CalcTextSize("REFETCH");
        buttonSize.x += ImGui::GetStyle().FramePadding.x * 2.0f;
        buttonSize.y += ImGui::GetStyle().FramePadding.y * 2.0f;

        ImVec2 padding = ImGui::GetStyle().WindowPadding;

        // Top-right corner of the content region
        ImGui::SetCursorPos(ImVec2(ImGui::GetWindowContentRegionMax().x - buttonSize.x - padding.x, padding.y));
        if (ImGui::Button("REFETCH")) {
            dataStates.dbData = DataState::DATA_OUTDATED;
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
        bomVisualizer.run();
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

        float buttonWidth = ImGui::CalcTextSize("Commit Mapping").x + ImGui::GetStyle().FramePadding.x * 2.0f;
        float rightEdge = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;

        ImGui::SameLine();
        ImGui::SetCursorPosX(rightEdge - buttonWidth);

        // TODO: Validate mappings
        ImGui::BeginDisabled(!orderVisualizer.hasMappings());
        if (ImGui::Button("Commit Mapping")) {
            orderVisualizer.commitMappings();
        }
        ImGui::EndDisabled();

        orderVisualizer.run();
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
                break;
            }
            if (!imguiCtx.beginFrame()) {
                continue;
            }

            handleDataState();
            if (ImGui::BeginTabBar("Main")) {
                if (ImGui::BeginTabItem("Database")) {
                    drawDb();
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
