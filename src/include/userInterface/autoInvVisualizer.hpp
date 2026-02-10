#pragma once

#include "autoInv.hpp"
#include "userInterface/mappingWidgets.hpp"
#include "userInterface/uiTypes.hpp"
#include "userInterface/widgets.hpp"

namespace AutoInv {
enum class MappingStage { CSV, API };

Widgets::MOUSE_EVENT_TYPE isMouseOnLine(const ImVec2& p1, const ImVec2& p2, const float thickness);

class CsvMappingVisualizer {
  protected:
    DbService& dbService;
    Logger& logger;
    std::shared_ptr<const completeDbData> dbData;
    DataStates& dataStates;

    MappingStage stage = MappingStage::CSV;

    std::vector<MappingDestination> dbHeaderWidgets;
    std::vector<std::string> headers;
    std::vector<std::string> firstRow;
    std::vector<MappingSource> csvHeaderWidgets;
    std::vector<MappingNumber> mappingsN;
    std::vector<MappingCsv> mappingsS;
    std::unordered_map<mappingIdType, ImVec2> sourceAnchors;
    std::unordered_map<mappingIdType, ImVec2> destAnchors;
    std::unordered_map<MappingNumber, MappingDrawing, MappingHash> mappingsDrawingInfo;

    std::array<char, 256> csvBuffer;

    CsvMappingVisualizer(DbService& cDbService, Logger& cLogger, DataStates& cDataStates)
        : dbService(cDbService), logger(cLogger), dataStates(cDataStates) {}

  public:
    virtual ~CsvMappingVisualizer() = default;
    virtual void run() = 0;
    virtual void createMapping(const SourceDetail& source, const DestinationDetail& dest) = 0;
    virtual bool hasMapping(const SourceDetail& source, const DestinationDetail& dest) = 0;
    virtual void removeMapping(const MappingNumber& mapping) = 0;
    virtual void storeAnchorSource(mappingIdType source, ImVec2 pos) = 0; // TODO: needs to be virtual ?
    virtual void storeAnchorDest(mappingIdType dest, ImVec2 pos) = 0;     // TODO: needs to be virtual ?

    void setData(std::shared_ptr<const completeDbData> newData);
    bool handleDrag(const DestinationDetail&, const ImGuiPayload* payload);
};

template <typename Reader> class CsvVisualizerImpl : public CsvMappingVisualizer {
  private:
    static constexpr const float RIGHT_WIDTH = 300.0f;

  protected:
    Reader& reader;

    CsvVisualizerImpl(DbService& cDbService, Reader& cReader, Logger& cLogger, DataStates& cDataStates)
        : CsvMappingVisualizer(cDbService, cLogger, cDataStates), reader(cReader) {}

    void drawHead() {
        bool enterPressed = ImGui::InputText("##edit", csvBuffer.data(), BUFFER_SIZE, ImGuiInputTextFlags_EnterReturnsTrue);
        if (enterPressed || ImGui::IsItemDeactivatedAfterEdit()) {
            try {
                reader.read(std::filesystem::path(std::string(csvBuffer.data())));
            } catch (const std::exception& e) {
                logger.pushLog(Log{std::format("ERROR reading order: {}", e.what())});
            }
        }

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

  public:
    void run() override {
        drawHead();

        if (dataStates.dbData != DataState::DATA_READY) {
            return;
        }

        checkNewData();

        if (reader.dataValid(false)) {
            const float rightWidth = RIGHT_WIDTH;
            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            const float leftWidth = ImGui::GetContentRegionAvail().x - rightWidth - spacing;

            // LEFT (maxWidth)
            ImGui::BeginChild("CSV", ImVec2(leftWidth, 0), false, ImGuiWindowFlags_NoScrollbar);

            std::pair left = drawMappingSourceStage();

            ImGui::EndChild();
            ImGui::SameLine();

            // RIGHT (fixed)
            ImGui::BeginChild("DB", ImVec2(rightWidth, 0), false, ImGuiWindowFlags_NoScrollbar);
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
                removeMapping(*toRemove);
            }
        }
    }

    void checkNewData() {
        if (reader.dataValid(true)) {
            // TODO clear old data
            headers = reader.getHeader();
            firstRow = reader.getFirstRow();
            MappingSource::setDragHandler(static_cast<CsvMappingVisualizer*>(this));
            MappingDestination::setDragHandler(static_cast<CsvMappingVisualizer*>(this));
            mappingIdType id = 0;
            for (std::size_t i = 0; i < headers.size(); ++i) {
                csvHeaderWidgets.push_back(std::move(MappingSource(headers[i], firstRow[i], id++)));
            }
        }
    }

    void createMapping(const SourceDetail& source, const DestinationDetail& dest) override {
        if (hasMapping(source, dest)) {
            return;
        }
        MappingNumber newMappingN = MappingNumber(source.id, dest.id);
        MappingCsv newMappingS = MappingCsv(source.header, PreciseHeader(dest.table, dest.header.name));
        mappingsDrawingInfo.insert_or_assign(newMappingN, MappingDrawing());
        mappingsN.emplace_back(std::move(newMappingN));
        mappingsS.emplace_back(std::move(newMappingS));
    }

    bool hasMapping(const SourceDetail& source, const DestinationDetail& dest) override {
        if (std::find_if(mappingsN.begin(), mappingsN.end(), [&](const MappingNumber& m) {
                return m == MappingNumber(source.id, dest.id);
            }) != mappingsN.end()) {
            return true;
        }
        return false;
    }

    void removeMapping(const MappingNumber& mapping) override {
        auto it = std::find(mappingsN.begin(), mappingsN.end(), mapping);
        if (it != mappingsN.end()) {
            mappingsDrawingInfo.erase(*it);
            mappingsN.erase(it);
            mappingsS.erase(std::distance(mappingsN.begin(), it) + mappingsS.begin()); // assumes both are added simul
        }
    }

    void storeAnchorSource(mappingIdType source, ImVec2 pos) override { sourceAnchors[source] = pos; }

    void storeAnchorDest(mappingIdType dest, ImVec2 pos) override { destAnchors[dest] = pos; }

    bool drawMapping(const MappingNumber& mapping, ImDrawList* drawlist, MappingDrawing& mappingDrawingInfo) {
        if (!sourceAnchors.count(mapping.source) || !destAnchors.count(mapping.destination)) {
            return false;
        }
        ImVec2 start = sourceAnchors.at(mapping.source);
        ImVec2 end = destAnchors.at(mapping.destination);

        Widgets::MOUSE_EVENT_TYPE event = isMouseOnLine(start, end, mappingDrawingInfo.width * 2);
        const float thickness = event != Widgets::MOUSE_EVENT_TYPE::NONE ? 6.0f : 2.0f;
        const ImU32 color = event != Widgets::MOUSE_EVENT_TYPE::NONE ? Widgets::colSelected.first : Widgets::colWhiteSemiOpaque;
        drawlist->AddLine(start, end, color, thickness);
        mappingDrawingInfo.width = thickness;
        return event == Widgets::MOUSE_EVENT_TYPE::CLICK;
    }

    bool hasMappings() { return mappingsS.size() > 2; }

    void commitMappings() { reader.setMappings(mappingsS); }
};

class BomVisualizer : public CsvVisualizerImpl<ChangeGeneratorFromBom> {
  public:
    BomVisualizer(DbService& cDbService, ChangeGeneratorFromBom& cReader, Logger& cLogger, DataStates& cDataStates)
        : CsvVisualizerImpl(cDbService, cReader, cLogger, cDataStates) {}
};

class OrderVisualizer : public CsvVisualizerImpl<ChangeGeneratorFromOrder> {
  public:
    OrderVisualizer(DbService& cDbService, ChangeGeneratorFromOrder& cReader, Logger& cLogger, DataStates& cDataStates)
        : CsvVisualizerImpl(cDbService, cReader, cLogger, cDataStates) {}
};

} // namespace AutoInv