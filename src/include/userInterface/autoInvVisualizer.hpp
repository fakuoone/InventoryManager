#pragma once

#include "autoInv.hpp"
#include "dbService.hpp"
#include "userInterface/widgets.hpp"

#include <filesystem>

namespace AutoInv {

class MappingSource {
  private:
    const std::string header;
    const std::string example;

  public:
    MappingSource(const std::string& cHeader, const std::string& cExample) : header(cHeader), example(cExample) {}

    void draw(const float width) {
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
    static constexpr const float INNER_PADDING = 3.0f;
    static constexpr const float OUTER_PADDING = 3.0f;

  public:
    MappingDestination(const std::string cTable, const std::vector<tHeaderInfo> cHeaders) : table(cTable), headers(cHeaders) {}
    void draw(const float width) {
        if (headers.size() == 0) {
            return;
        }
        ImGui::PushID(this);

        ImDrawList* drawList = ImGui::GetWindowDrawList();

        const float widthPadded = width - 2 * OUTER_PADDING;

        // Headerwidth
        float maxHeaderWidth = 0.0f;
        for (const auto& header : headers) {
            maxHeaderWidth = std::max(maxHeaderWidth, ImGui::CalcTextSize(header.name.c_str()).x);
        }
        maxHeaderWidth = std::min(maxHeaderWidth, static_cast<float>(0.5 * widthPadded));

        // Calc Height
        const float headerHeight = ImGui::CalcTextSize(headers.at(0).name.c_str()).y;
        const float height = headers.size() * (headerHeight) + 2 * INNER_PADDING;
        ImVec2 cursor = ImGui::GetCursorScreenPos();
        ImVec2 bgRectBegin = cursor;
        ImVec2 bgRectEnd = ImVec2(cursor.x + widthPadded, cursor.y + height);
        drawList->AddRectFilled(bgRectBegin, bgRectEnd, Widgets::colGreyBg, 0.0f);
        drawList->AddRect(bgRectBegin, bgRectEnd, IM_COL32(120, 120, 120, 200), 0.0f);

        // Headers column
        cursor.x += INNER_PADDING;
        cursor.y += INNER_PADDING;
        for (const auto& header : headers) {
            const float cellWidth = widthPadded / 2 - INNER_PADDING;
            ImGui::SetCursorScreenPos(cursor);
            ImGui::InvisibleButton(header.name.c_str(), ImVec2(cellWidth, headerHeight));
            bool hovered = ImGui::IsItemHovered();

            drawList->AddText(cursor, hovered ? IM_COL32(255, 255, 255, 255) : IM_COL32(220, 220, 220, 255), header.name.c_str());
            drawList->AddRectFilled(
                cursor, ImVec2(cursor.x + widthPadded / 2 - INNER_PADDING, cursor.y + headerHeight), Widgets::colGreyBg, 0.0f);
            if (hovered) {
                drawList->AddRect(
                    cursor, ImVec2(cursor.x + widthPadded / 2 - INNER_PADDING, cursor.y + headerHeight), Widgets::colHoveredGrey, 0.0f);
            }
            cursor.y += headerHeight;
        }

        ImGui::PopID();

        ImGui::Dummy(ImVec2(0, OUTER_PADDING));
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
            const float rightWidth = 420.0f; // fixed DB panel width
            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            const float leftWidth = avail.x - rightWidth - spacing;

            // LEFT (flexible)
            ImGui::BeginChild("CSV", ImVec2(leftWidth, 0), false);
            for (auto& csvHeaderWidget : csvHeaderWidgets) {
                csvHeaderWidget.draw(ImGui::GetContentRegionAvail().x);
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