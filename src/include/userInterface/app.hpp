#pragma once

#include <future>

#include "autoInv.hpp"
#include "changeExeService.hpp"
#include "changeTracker.hpp"
#include "config.hpp"
#include "dbFilter.hpp"
#include "dbService.hpp"
#include "logger.hpp"

#include "dataTypes.hpp"
#include "userInterface/autoInvVisualizer.hpp"
#include "userInterface/dbDataVisualizer.hpp"
#include "userInterface/imGuiDX11Context.hpp"

class App {
  private:
    ImGuiDX11Context imguiCtx_;

    Config& config_;
    ThreadPool& pool_;
    DbService& dbService_;
    ChangeTracker& changeTracker_;
    PartApi& api_;
    AutoInv::ChangeGeneratorFromBom& bomReader_;
    AutoInv::ChangeGeneratorFromOrder& orderReader_;
    Logger& logger_;

    UI::DataStates dataStates_;

    ChangeExeService changeExe_{dbService_, changeTracker_, logger_};
    DbVisualizer dbVisualizer_{dbService_, changeTracker_, changeExe_, logger_, dataStates_};

    AutoInv::BomVisualizer bomVisualizer_{dbService_, bomReader_, api_, config_, logger_, dataStates_};
    AutoInv::OrderVisualizer orderVisualizer_{dbService_, orderReader_, api_, config_, logger_, dataStates_};

    DbFilter dbFilter_{pool_, logger_, dataStates_};

    std::shared_ptr<const CompleteDbData> dbData_;
    std::shared_ptr<uiChangeInfo> uiChanges_;

    bool waitForDbData() {
        if (dataStates_.dbData == UI::DataState::DATA_READY) { return false; }

        auto result = dbService_.getCompleteData();
        if (!result) { return false; }

        dbData_ = *result;
        dbVisualizer_.setData(dbData_);
        bomVisualizer_.setData(dbData_);
        orderVisualizer_.setData(dbData_);
        bomReader_.setData(dbData_);
        orderReader_.setData(dbData_);
        dbFilter_.setData(dbData_);

        changeTracker_.setMaxPKeys(dbData_->maxPKeys);
        if (!uiChanges_) { uiChanges_ = std::make_shared<uiChangeInfo>(); }

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
        switch (dataStates_.dbData) {
        case UI::DataState::INIT:
            dbService_.startUp();
            dataStates_.dbData = UI::DataState::WAITING_FOR_DATA;
            break;
        case UI::DataState::DATA_OUTDATED: {
            dbService_.refetch();
            uiChanges_ = std::make_shared<uiChangeInfo>(changeTracker_.getSnapShot());
            dbVisualizer_.setChangeData(uiChanges_);
            dataStates_.dbData = UI::DataState::WAITING_FOR_DATA;
            break;
        }
        case UI::DataState::WAITING_FOR_DATA:
            if (waitForDbData()) { dataStates_.dbData = UI::DataState::DATA_READY; }
            break;
        case UI::DataState::DATA_READY:
            uiChanges_ = std::make_shared<uiChangeInfo>(changeTracker_.getSnapShot());
            dbVisualizer_.setChangeData(uiChanges_);
            if (changeExe_.isChangeApplicationDone()) {
                changeExe_.getSuccessfulChanges();
                dataStates_.dbData = UI::DataState::DATA_OUTDATED;
            }
            break;
        default:
            break;
        }
    }

    void initFont(const std::string& font) {
        if (!font.empty()) {
            ImGuiIO& io = ImGui::GetIO();
            io.Fonts->AddFontFromFileTTF(font.c_str(), 16.0f);
        }
    }

    void drawDb() {
        dbVisualizer_.run();
        ImVec2 buttonSize = ImGui::CalcTextSize("REFETCH");
        buttonSize.x += ImGui::GetStyle().FramePadding.x * 2.0f;
        buttonSize.y += ImGui::GetStyle().FramePadding.y * 2.0f;

        ImVec2 padding = ImGui::GetStyle().WindowPadding;

        // Top-right corner of the content region
        ImGui::SetCursorPos(ImVec2(ImGui::GetWindowContentRegionMax().x - buttonSize.x - padding.x, padding.y));
        if (ImGui::Button("REFETCH")) { dataStates_.dbData = UI::DataState::DATA_OUTDATED; }
    }

    void showBom() { bomVisualizer_.run(); }

    void showOrder() { orderVisualizer_.run(); }

  public:
    App(Config& cConfig,
        ThreadPool& cPool,
        DbService& cDbService,
        ChangeTracker& cChangeTracker,
        PartApi& cPartApi,
        AutoInv::ChangeGeneratorFromBom& cBomReader,
        AutoInv::ChangeGeneratorFromOrder& cOrderReader,
        Logger& cLogger)
        : config_(cConfig), pool_(cPool), dbService_(cDbService), changeTracker_(cChangeTracker), api_(cPartApi), bomReader_(cBomReader),
          orderReader_(cOrderReader), logger_(cLogger) {}

    ~App() { config_.saveMappings(bomVisualizer_.getMappings(), orderVisualizer_.getMappings()); }

    App(const App&) = delete;
    App& operator=(const App&) = delete;
    App(App&&) = delete;
    App& operator=(App&&) = delete;

    void supplyConfigString() {
        std::string dbString = config_.setConfigString(std::filesystem::path{}); // OPTIONAL USER SUPPLIED CONFIG PATH
        initFont(config_.getFont());
        dbService_.initializeDbInterface(dbString);
        bomVisualizer_.setDefaultPath(config_.getCsvPathBom());
        orderVisualizer_.setDefaultPath(config_.getCsvPathOrder());

        AutoInv::LoadedMappings loaded = config_.readMappings();
        pool_.submit(AutoInv::BomVisualizer::injectMappings, &bomVisualizer_, loaded.bom);
        pool_.submit(AutoInv::OrderVisualizer::injectMappings, &orderVisualizer_, loaded.order);
    }

    void run() {
        supplyConfigString();
        bool running = true;
        while (running) {
            if (!imguiCtx_.pollEvents()) { break; }
            if (!imguiCtx_.beginFrame()) { continue; }

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
            imguiCtx_.endFrame();
        }
    }
};
