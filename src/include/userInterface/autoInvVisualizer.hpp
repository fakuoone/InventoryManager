#pragma once

#include "autoInv.hpp"
#include "dbService.hpp"

#include <filesystem>

namespace AutoInv {

class MappingSource {
  private:
    const std::string header;
    const std::string example;

  public:
    MappingSource(const std::string& cHeader, const std::string& cExample) : header(cHeader), example(cExample) {}

    void draw() {
        ImGui::PushID(header.c_str());
        ImGui::TextUnformatted(header.c_str());
        ImGui::TextUnformatted(example.c_str());
        ImGui::PopID();
    }

    void beginDrag() {}
};

class MappingDestination {
  private:
    const std::string table;
    const std::vector<tHeaderInfo> headers;

  public:
    MappingDestination(const std::string cTable, const std::vector<tHeaderInfo> cHeaders) : table(cTable), headers(cHeaders) {}

    void draw() {
        ImGui::PushID(this);

        float startY = ImGui::GetCursorPosY();

        ImGui::BeginGroup();
        for (const auto& header : headers) {
            ImGui::TextUnformatted(header.name.c_str());
        }
        ImGui::EndGroup();

        ImGui::SameLine();
        ImVec2 textSize = ImGui::CalcTextSize(table.c_str());
        float contentHeight = headers.size() * ImGui::GetFrameHeightWithSpacing();
        float centeredY = startY + (contentHeight - textSize.y) * 0.5f;

        ImGui::SetCursorPosY(centeredY);
        ImGui::TextUnformatted(table.c_str());

        ImGui::PopID();
    }
};

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

    void setData(std::shared_ptr<const completeDbData> newData) {
        dbData = newData;
        for (const std::string& s : dbData->tables) {
            dbHeaderWidgets.push_back(std::move(MappingDestination(s, dbData->headers.at(s).data)));
        }
    }
};

template <typename Reader> class CsvVisualizerImpl : public CsvVisualizer {
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
            float leftWidth = avail.x * 0.33f;

            ImGui::BeginChild("CSV", ImVec2(leftWidth, 0), false);
            for (auto& csvHeaderWidget : csvHeaderWidgets) {
                csvHeaderWidget.draw();
            }
            ImGui::EndChild();
            ImGui::SameLine();

            ImGui::BeginChild("DB", ImVec2(0, 0), false);
            for (auto& headerWidget : dbHeaderWidgets) {
                headerWidget.draw();
            }
            ImGui::EndChild();
        }
    }

    void checkNewData() {
        if (reader.dataValid(true)) {
            headers = reader.getHeader();
            firstRow = reader.getFirstRow();
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