#pragma once

#include "autoInv.hpp"
#include "partApi.hpp"
#include "userInterface/mappingWidgets.hpp"
#include "userInterface/uiTypes.hpp"
#include "userInterface/widgets.hpp"

namespace AutoInv {
enum class MappingStage { CSV, API };

Widgets::MOUSE_EVENT_TYPE isMouseOnLine(const ImVec2& p1, const ImVec2& p2, const float thickness);

struct WidgetAnchors {
    mappingIdType largestId = 0;
    std::unordered_map<mappingIdType, ImVec2> anchors;
};

class CsvMappingVisualizer {
  protected:
    DbService& dbService;
    PartApi& api;
    Logger& logger;
    std::shared_ptr<const completeDbData> dbData;
    DataStates& dataStates;

    MappingStage stage = MappingStage::CSV;

    std::vector<std::string> headers;
    std::vector<std::string> firstRow;
    std::vector<MappingDestinationDb> dbHeaderWidgets;
    std::vector<MappingSource> csvHeaderWidgets;

    std::vector<MappingNumber> mappingsN;

    std::vector<MappingDestinationToApi> mappingsToApiWidgets;
    std::unordered_map<mappingIdType, ApiPreviewState> apiPreviewCache;

    WidgetAnchors sourceAnchors;
    WidgetAnchors destAnchors;
    std::unordered_map<MappingNumber, MappingDrawing, MappingHash> mappingsDrawingInfo;

    std::array<char, 256> csvBuffer;

    CsvMappingVisualizer(DbService& cDbService, PartApi& cApi, Logger& cLogger, DataStates& cDataStates)
        : dbService(cDbService), api(cApi), logger(cLogger), dataStates(cDataStates) {}

  public:
    virtual ~CsvMappingVisualizer() = default;
    virtual void run() = 0;
    virtual void createMappingToDb(const SourceDetail& source, const DbDestinationDetail& dest) = 0;
    virtual void createMappingToApi(const SourceDetail& source, const ApiDestinationDetail& dest) = 0;
    virtual bool hasMapping(const SourceDetail& source, mappingIdType dest) = 0;
    virtual void removeMappingToDb(const MappingNumber& mapping) = 0;

    void storeAnchorSource(mappingIdType source, ImVec2 pos);
    void storeAnchorDest(mappingIdType dest, ImVec2 pos);
    mappingIdType getLastIdSource();
    mappingIdType getLastIdDest();

    void setData(std::shared_ptr<const completeDbData> newData);

    void handleApiClick(MappingDestinationToApi& destination);

    bool handleDrag(const DbDestinationDetail& destination, const ImGuiPayload* payload);
    bool handleDrag(const ApiDestinationDetail& destination, const ImGuiPayload* payload);
};

template <typename Reader> class CsvVisualizerImpl : public CsvMappingVisualizer {
  private:
    static constexpr const float RIGHT_WIDTH = 300.0f;
    static constexpr const float CENTER_WIDTH = 300.0f;

    void drawApiWidgets() {
        const ImVec2 avail = ImGui::GetContentRegionAvail();

        for (auto& mapping : mappingsToApiWidgets) {
            mapping.draw(avail.x);
        }
    }

    void drawHead() {
        bool enterPressed = ImGui::InputText("##edit", csvBuffer.data(), BUFFER_SIZE, ImGuiInputTextFlags_EnterReturnsTrue);
        if (enterPressed || ImGui::IsItemDeactivatedAfterEdit()) {
            try {
                reader.read(std::filesystem::path(std::string(csvBuffer.data())));
            } catch (const std::exception& e) {
                logger.pushLog(Log{std::format("ERROR reading order: {}", e.what())});
            }
        }

        // add api intermediate stage
        ImGui::SameLine();
        const char* btnLabel = "ADD API STAGE";
        if (ImGui::Button(btnLabel)) {
            const std::size_t newIndex = ++destAnchors.largestId;
            apiPreviewCache.insert_or_assign(newIndex, ApiPreviewState{});
            mappingsToApiWidgets.emplace_back(
                MappingDestinationToApi(ApiDestinationDetail(true, ++destAnchors.largestId, "API"), apiPreviewCache.at(newIndex), true));
        };

        // Stage selector
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0f);
        int localStage = static_cast<int>(stage);
        ImGui::SliderInt("Stage", &localStage, static_cast<int>(MappingStage::CSV), static_cast<int>(MappingStage::API));
        stage = static_cast<MappingStage>(localStage);

        float buttonWidth = ImGui::CalcTextSize("Commit Mapping").x + ImGui::GetStyle().FramePadding.x * 2.0f;
        float rightEdge = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;

        ImGui::SameLine();
        ImGui::SetCursorPosX(rightEdge - buttonWidth);

        // TODO: Validate mappings
        ImGui::BeginDisabled(!hasMappings());
        if (ImGui::Button("Commit Mapping")) {
            commitMappings();
        }
        ImGui::EndDisabled();
    }

    std::pair<ImVec2, ImVec2> drawMappingSourceRawCSV() {
        float maxWidth = 0;
        for (auto& csvHeaderWidget : csvHeaderWidgets) {
            float width = ImGui::CalcTextSize(csvHeaderWidget.getHeader().c_str()).x;
            if (width > maxWidth) {
                maxWidth = width;
            }
        }
        ImVec2 leftMin = ImGui::GetItemRectMin();
        ImVec2 leftMax = ImGui::GetItemRectMax();

        for (auto& csvHeaderWidget : csvHeaderWidgets) {
            csvHeaderWidget.draw(maxWidth);
        }
        return std::pair(leftMin, leftMax);
    }

    std::pair<ImVec2, ImVec2> drawMappingSourceStage() {
        switch (stage) {
        case MappingStage::CSV:
            return drawMappingSourceRawCSV();
        case MappingStage::API:
            break;
        default:
            break;
        }
        return std::pair(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    }

  protected:
    Reader& reader;

    CsvVisualizerImpl(DbService& cDbService, Reader& cReader, PartApi& cApi, Logger& cLogger, DataStates& cDataStates)
        : CsvMappingVisualizer(cDbService, cApi, cLogger, cDataStates), reader(cReader) {}

  public:
    void run() override {
        drawHead();

        if (dataStates.dbData != DataState::DATA_READY) {
            return;
        }

        checkNewData();

        if (reader.dataValid(false)) {
            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            const float leftWidth = ImGui::GetContentRegionAvail().x - RIGHT_WIDTH - CENTER_WIDTH - spacing;

            // LEFT (maxWidth)
            ImGui::BeginChild("CSV", ImVec2(leftWidth, 0), false, ImGuiWindowFlags_NoScrollbar);
            std::pair left = drawMappingSourceStage();
            ImGui::EndChild();
            ImGui::SameLine();

            ImGui::BeginChild("API", ImVec2(CENTER_WIDTH, 0), false, ImGuiWindowFlags_NoScrollbar);
            drawApiWidgets();
            ImGui::EndChild();
            ImGui::SameLine();

            // RIGHT (fixed)
            ImGui::BeginChild("DB", ImVec2(RIGHT_WIDTH, 0), false, ImGuiWindowFlags_NoScrollbar);
            for (auto& headerWidget : dbHeaderWidgets) {
                headerWidget.draw(ImGui::GetContentRegionAvail().x);
            }
            ImGui::EndChild();

            // clipping rect
            ImVec2 rightMin = ImGui::GetItemRectMin();
            ImVec2 rightMax = ImGui::GetItemRectMax();
            ImVec2 clipMin(std::min(left.first.x, rightMin.x), std::min(left.first.y, rightMin.y));
            ImVec2 clipMax(std::max(left.second.x, rightMax.x), std::max(left.second.y, rightMax.y));
            ImDrawList* drawlist = ImGui::GetWindowDrawList();

            // mappings
            const MappingNumber* toRemove = nullptr;

            drawlist->PushClipRect(clipMin, clipMax, true);
            for (const MappingNumber& mapping : mappingsN) {
                if (drawMapping(mapping, drawlist, mappingsDrawingInfo.at(mapping))) {
                    toRemove = &mapping;
                }
            }
            drawlist->PopClipRect();

            if (toRemove) {
                removeMappingToDb(*toRemove);
            }
        }
    }

    void checkNewData() {
        if (reader.dataValid(true)) {
            // TODO clear old data
            headers = reader.getHeader();
            firstRow = reader.getFirstRow();
            MappingSource::setInteractionHandler(static_cast<CsvMappingVisualizer*>(this));
            MappingDestinationDb::setInteractionHandler(static_cast<CsvMappingVisualizer*>(this));
            mappingIdType id = 0;
            for (std::size_t i = 0; i < headers.size(); ++i) {
                csvHeaderWidgets.push_back(std::move(MappingSource(headers[i], firstRow[i], id++)));
            }
        }
    }

    void createMappingToDb(const SourceDetail& source, const DbDestinationDetail& dest) override {
        if (hasMapping(source, dest.id)) {
            return;
        }
        MappingToDb newMappingS = MappingToDb(source.attribute, PreciseHeader(dest.table, dest.header.name));
        MappingNumber newMappingN = MappingNumber(MappingNumberInternal{source.id, dest.id}, std::move(newMappingS));
        mappingsDrawingInfo.insert_or_assign(newMappingN, MappingDrawing());
        mappingsN.emplace_back(std::move(newMappingN));
        // mappingsSToDb.emplace_back(std::move(newMappingS));
    }

    void createMappingToApi(const SourceDetail& source, const ApiDestinationDetail& dest) override {
        if (hasMapping(source, dest.id)) {
            return;
        }
        MappingCsvApi newMappingS = MappingCsvApi(source.attribute, dest.id);
        MappingNumber newMappingN = MappingNumber(MappingNumberInternal{source.id, dest.id}, std::move(newMappingS));
        mappingsDrawingInfo.insert_or_assign(newMappingN, MappingDrawing());
        mappingsN.emplace_back(std::move(newMappingN));
        // mappingsSToApi.emplace_back(std::move(newMappingS));
    }

    bool hasMapping(const SourceDetail& source, mappingIdType dest) override {
        if (std::find_if(mappingsN.begin(), mappingsN.end(), [&](const MappingNumber& m) {
                return m.uniqueData == MappingNumberInternal(source.id, dest);
            }) != mappingsN.end()) {
            return true;
        }
        return false;
    }

    void removeMappingToDb(const MappingNumber& mapping) override {
        // not very efficient, but mappingnumber is small
        auto it = std::find(mappingsN.begin(), mappingsN.end(), mapping);
        if (it != mappingsN.end()) {
            mappingsDrawingInfo.erase(mapping);
            mappingsN.erase(it);
        }
    }

    bool drawMapping(const MappingNumber& mapping, ImDrawList* drawlist, MappingDrawing& mappingDrawingInfo) {
        if (!sourceAnchors.anchors.count(mapping.uniqueData.source) || !destAnchors.anchors.count(mapping.uniqueData.destination)) {
            return false;
        }
        ImVec2 start = sourceAnchors.anchors.at(mapping.uniqueData.source);
        ImVec2 end = destAnchors.anchors.at(mapping.uniqueData.destination);

        Widgets::MOUSE_EVENT_TYPE event = isMouseOnLine(start, end, mappingDrawingInfo.width * 2);
        const float thickness = event != Widgets::MOUSE_EVENT_TYPE::NONE ? 6.0f : 2.0f;
        const ImU32 color = event != Widgets::MOUSE_EVENT_TYPE::NONE ? Widgets::colSelected.first : Widgets::colWhiteSemiOpaque;
        drawlist->AddLine(start, end, color, thickness);
        mappingDrawingInfo.width = thickness;
        return event == Widgets::MOUSE_EVENT_TYPE::CLICK;
    }

    bool hasMappings() {
        // TODO: only detect mappings to db
        return mappingsN.size() > 2;
    }

    void commitMappings() {

        // TODO: store mappingsToDb separately
        // reader.setMappings(mappingsSToDb);
    }
};

class BomVisualizer : public CsvVisualizerImpl<ChangeGeneratorFromBom> {
  public:
    BomVisualizer(DbService& cDbService, ChangeGeneratorFromBom& cReader, PartApi& cApi, Logger& cLogger, DataStates& cDataStates)
        : CsvVisualizerImpl(cDbService, cReader, cApi, cLogger, cDataStates) {}
};

class OrderVisualizer : public CsvVisualizerImpl<ChangeGeneratorFromOrder> {
  public:
    OrderVisualizer(DbService& cDbService, ChangeGeneratorFromOrder& cReader, PartApi& cApi, Logger& cLogger, DataStates& cDataStates)
        : CsvVisualizerImpl(cDbService, cReader, cApi, cLogger, cDataStates) {}
};

} // namespace AutoInv