#pragma once

#include "autoInv.hpp"
#include "userInterface/mappingWidgets.hpp"

namespace AutoInv {

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

    void setData(std::shared_ptr<const completeDbData> newData);
    bool handleDrag(const ImGuiPayload* payload);
};

template <typename Reader> class CsvVisualizerImpl : public CsvVisualizer {
  private:
    static constexpr const float RIGHT_WIDTH = 300.0f;

  protected:
    Reader& reader;
    std::vector<std::string> headers;
    std::vector<std::string> firstRow;
    std::vector<MappingSource> csvHeaderWidgets;

    CsvVisualizerImpl(DbService& cDbService, Reader& cReader, Logger& cLogger) : CsvVisualizer(cDbService, cLogger), reader(cReader) {}

  public:
    void run(const bool dbDataFresh) override {
        if (!dbDataFresh) {
            return;
        }

        checkNewData();

        if (reader.dataValid(false)) {
            ImVec2 avail = ImGui::GetContentRegionAvail();
            const float rightWidth = RIGHT_WIDTH;
            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            const float leftWidth = avail.x - rightWidth - spacing;

            // LEFT (maxWidth)
            ImGui::BeginChild("CSV", ImVec2(leftWidth, 0), false);
            float maxWidth = 0;
            for (auto& csvHeaderWidget : csvHeaderWidgets) {
                float width = ImGui::CalcTextSize(csvHeaderWidget.getHeader().c_str()).x;
                if (width > maxWidth) {
                    maxWidth = width;
                }
            }

            for (auto& csvHeaderWidget : csvHeaderWidgets) {
                csvHeaderWidget.draw(maxWidth);
            }
            ImGui::EndChild();
            ImGui::SameLine();

            // RIGHT (fixed)
            ImGui::BeginChild("DB", ImVec2(rightWidth, 0), false);
            for (auto& headerWidget : dbHeaderWidgets) {
                headerWidget.draw(ImGui::GetContentRegionAvail().x);
            }
            ImGui::EndChild();
        }
    }

    void checkNewData() {
        if (reader.dataValid(true)) {
            headers = reader.getHeader();
            firstRow = reader.getFirstRow();
            MappingSource::setDragHandler(static_cast<CsvVisualizer*>(this));
            MappingDestination::setDragHandler(static_cast<CsvVisualizer*>(this));
            for (std::size_t i = 0; i < headers.size(); ++i) {
                csvHeaderWidgets.push_back(std::move(MappingSource(headers[i], firstRow[i])));
            }
        }
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