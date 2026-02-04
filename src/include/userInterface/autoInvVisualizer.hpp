#pragma once

#include "autoInv.hpp"
#include "userInterface/mappingWidgets.hpp"
#include "userInterface/widgets.hpp"

namespace AutoInv {
Widgets::MOUSE_EVENT_TYPE isMouseOnLine(const ImVec2& p1, const ImVec2& p2, const float thickness);

class CsvVisualizer {
  protected:
    DbService& dbService;
    Logger& logger;
    std::shared_ptr<const completeDbData> dbData;
    std::vector<MappingDestination> dbHeaderWidgets;

    CsvVisualizer(DbService& cDbService, Logger& cLogger) : dbService(cDbService), logger(cLogger) {}

  public:
    virtual ~CsvVisualizer() = default;
    virtual void run(const bool dbDataFresh) = 0;
    virtual void createMapping(sourceId source, destId dest) = 0;
    virtual void removeMapping(const Mapping& mapping) = 0;
    virtual void storeAnchorSource(sourceId source, ImVec2 pos) = 0; // TODO: needs to be virtual ?
    virtual void storeAnchorDest(destId dest, ImVec2 pos) = 0;       // TODO: needs to be virtual ?

    void setData(std::shared_ptr<const completeDbData> newData);
    bool handleDrag(destId dest, const ImGuiPayload* payload);
};

template <typename Reader> class CsvVisualizerImpl : public CsvVisualizer {
  private:
    static constexpr const float RIGHT_WIDTH = 300.0f;

  protected:
    Reader& reader;
    std::vector<std::string> headers;
    std::vector<std::string> firstRow;
    std::vector<MappingSource> csvHeaderWidgets;
    std::vector<Mapping> mappings;
    std::unordered_map<sourceId, ImVec2> sourceAnchors;
    std::unordered_map<destId, ImVec2> destAnchors;
    std::unordered_map<Mapping, MappingDrawing, MappingHash> mappingsDrawingInfo;

    CsvVisualizerImpl(DbService& cDbService, Reader& cReader, Logger& cLogger) : CsvVisualizer(cDbService, cLogger), reader(cReader) {}

  public:
    void run(const bool dbDataFresh) override {
        if (!dbDataFresh) {
            return;
        }

        checkNewData();

        if (reader.dataValid(false)) {
            const float rightWidth = RIGHT_WIDTH;
            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            const float leftWidth = ImGui::GetContentRegionAvail().x - rightWidth - spacing;

            // LEFT (maxWidth)
            ImGui::BeginChild("CSV", ImVec2(leftWidth, 0), false, ImGuiWindowFlags_NoScrollbar);
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
            ImVec2 clipMin(std::min(leftMin.x, rightMin.x), std::min(leftMin.y, rightMin.y));
            ImVec2 clipMax(std::max(leftMax.x, rightMax.x), std::max(leftMax.y, rightMax.y));
            ImDrawList* drawlist = ImGui::GetWindowDrawList();
            drawlist->PushClipRect(clipMin, clipMax, true);

            // mappings
            std::vector<Mapping> toRemove;

            for (const Mapping& mapping : mappings) {
                if (drawMapping(mapping, drawlist, mappingsDrawingInfo.at(mapping))) {
                    toRemove.push_back(mapping);
                }
            }
            for (const Mapping& mapping : toRemove) {
                removeMapping(mapping);
            }
            drawlist->PopClipRect();
        }
    }
    void checkNewData() {
        if (reader.dataValid(true)) {
            // TODO clear old data
            headers = reader.getHeader();
            firstRow = reader.getFirstRow();
            MappingSource::setDragHandler(static_cast<CsvVisualizer*>(this));
            MappingDestination::setDragHandler(static_cast<CsvVisualizer*>(this));
            sourceId id = 0;
            for (std::size_t i = 0; i < headers.size(); ++i) {
                csvHeaderWidgets.push_back(std::move(MappingSource(headers[i], firstRow[i], id++)));
            }
        }
    }

    void createMapping(sourceId source, destId dest) override {
        if (std::find_if(mappings.begin(), mappings.end(), [&](const Mapping& m) { return m == Mapping(source, dest); }) !=
            mappings.end()) {
            return;
        }
        Mapping newMapping = Mapping(source, dest);
        mappingsDrawingInfo.insert_or_assign(newMapping, MappingDrawing());
        mappings.emplace_back(std::move(newMapping));
    }

    void removeMapping(const Mapping& mapping) override {
        auto it = std::remove_if(mappings.begin(), mappings.end(), [&](const auto& m) { return m == mapping; });
        if (it != mappings.end()) {
            mappingsDrawingInfo.erase(*it);
            mappings.erase(it, mappings.end());
        }
    }

    void storeAnchorSource(sourceId source, ImVec2 pos) override { sourceAnchors[source] = pos; }

    void storeAnchorDest(destId dest, ImVec2 pos) override { destAnchors[dest] = pos; }

    bool drawMapping(const Mapping& mapping, ImDrawList* drawlist, MappingDrawing& mappingDrawingInfo) {
        if (!sourceAnchors.count(mapping.source) || !destAnchors.count(mapping.destination)) {
            return false;
        }
        ImVec2 start = sourceAnchors.at(mapping.source);
        ImVec2 end = destAnchors.at(mapping.destination);

        Widgets::MOUSE_EVENT_TYPE event = isMouseOnLine(start, end, mappingDrawingInfo.width);
        const float thickness = event != Widgets::MOUSE_EVENT_TYPE::NONE ? 6.0f : 2.0f;
        const ImU32 color = event != Widgets::MOUSE_EVENT_TYPE::NONE ? Widgets::colSelected.first : Widgets::colWhiteSemiOpaque;
        drawlist->AddLine(start, end, color, thickness);
        mappingDrawingInfo.width = thickness;
        return event == Widgets::MOUSE_EVENT_TYPE::CLICK;
    }
};

class BomVisualizer : public CsvVisualizerImpl<BomReader> {
  public:
    BomVisualizer(DbService& cDbService, BomReader& cReader, Logger& cLogger) : CsvVisualizerImpl(cDbService, cReader, cLogger) {}
};

class OrderVisualizer : public CsvVisualizerImpl<OrderReader> {
  public:
    OrderVisualizer(DbService& cDbService, OrderReader& cReader, Logger& cLogger) : CsvVisualizerImpl(cDbService, cReader, cLogger) {}
};

} // namespace AutoInv