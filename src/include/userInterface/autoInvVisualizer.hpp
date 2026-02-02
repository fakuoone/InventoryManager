#pragma once

#include "autoInv.hpp"
#include "dbService.hpp"
#include "userInterface/widgets.hpp"

#include <filesystem>
#include <map>

namespace AutoInv {
static constexpr const float INNER_PADDING = 3.0f;
static constexpr const float INNER_TEXT_PADDING = 2.0f;
static constexpr const float OUTER_PADDING = 3.0f;

enum class mappingTypes { HEADER_HEADER };

std::map<mappingTypes, std::string> mappingStrings = {{mappingTypes::HEADER_HEADER, "HEADER_HEADER"}};

class MappingSource {
  private:
    const std::string header;
    const std::string example;

  public:
    MappingSource(const std::string& cHeader, const std::string& cExample) : header(cHeader), example(cExample) {}

    const std::string& getHeader() { return header; }

    void draw(const float width) {
        const float actualWidth = width + 2 * OUTER_PADDING + INNER_PADDING;
        ImGui::PushID(this);

        ImDrawList* drawList = ImGui::GetWindowDrawList();

        // Calc Height
        const float headerHeight = ImGui::CalcTextSize(header.c_str()).y + 2 * INNER_TEXT_PADDING;
        const float height = 2 * (headerHeight) + 2 * INNER_PADDING;
        ImVec2 begin = ImGui::GetCursorScreenPos();
        begin.x += OUTER_PADDING;
        begin.y += OUTER_PADDING;
        ImVec2 cursor = begin;
        // complete background
        ImVec2 bgRectBegin = cursor;
        ImVec2 bgRectEnd = ImVec2(cursor.x + actualWidth, cursor.y + height);
        drawList->AddRectFilled(bgRectBegin, bgRectEnd, Widgets::colGreyBg, 0.0f);
        drawList->AddRect(bgRectBegin, bgRectEnd, IM_COL32(120, 120, 120, 200), 0.0f);

        ImGui::SetCursorScreenPos(cursor);
        ImGui::InvisibleButton(header.c_str(), ImVec2(width, headerHeight));

        // hover effect on header
        cursor.x += INNER_PADDING;
        cursor.y += INNER_PADDING;
        bool hovered = ImGui::IsItemHovered();
        bool dragged = beginDrag();
        if (hovered || dragged) {
            // handle drag drop source
            ImU32 colBg = dragged ? Widgets::colSelected.first : Widgets::colGreyBg;
            ImU32 colBorder = dragged ? Widgets::colSelected.second : Widgets::colHoveredGrey;

            drawList->AddRectFilled(cursor, ImVec2(cursor.x + actualWidth - 2 * INNER_PADDING, cursor.y + headerHeight), colBg, 0.0f);
            drawList->AddRect(cursor, ImVec2(cursor.x + actualWidth - 2 * INNER_PADDING, cursor.y + headerHeight), colBorder, 0.0f);
        }

        // header
        drawList->AddText(ImVec2(cursor.x + INNER_TEXT_PADDING, cursor.y + INNER_TEXT_PADDING),
                          hovered ? IM_COL32(255, 255, 255, 255) : IM_COL32(220, 220, 220, 255),
                          header.c_str());
        cursor.y += headerHeight;

        // example
        drawList->PushClipRect(ImVec2(cursor.x + INNER_TEXT_PADDING, cursor.y + INNER_TEXT_PADDING),
                               ImVec2(cursor.x + actualWidth - 2 * INNER_TEXT_PADDING, cursor.y + headerHeight),
                               true);

        drawList->AddText(
            ImVec2(cursor.x + INNER_TEXT_PADDING, cursor.y + INNER_TEXT_PADDING), IM_COL32(220, 220, 220, 255), example.c_str());

        drawList->PopClipRect();
        cursor.y += headerHeight;

        ImGui::SetCursorScreenPos(cursor);
        ImGui::Dummy(ImVec2(0, OUTER_PADDING));
        ImGui::PopID();
    }

    bool beginDrag() {
        if (ImGui::BeginDragDropSource()) {
            ImGui::SetDragDropPayload(mappingStrings.at(mappingTypes::HEADER_HEADER).c_str(), &header, sizeof(header));
            ImGui::EndDragDropSource();
            return true;
        }
        return false;
    }
};

class MappingDestination {
  private:
    const std::string table;
    const std::vector<tHeaderInfo> headers;

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
        const float headerHeight = ImGui::CalcTextSize(headers.at(0).name.c_str()).y + 2 * INNER_TEXT_PADDING;
        const float height = headers.size() * (headerHeight) + 2 * INNER_PADDING;
        ImVec2 begin = ImGui::GetCursorScreenPos();
        begin.x += OUTER_PADDING;
        begin.y += OUTER_PADDING;
        ImVec2 cursor = begin;
        // complete background
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
            bool draggedTo = handleDrag();
            if (hovered || draggedTo) {
                ImU32 colBg = draggedTo ? Widgets::colSelected.first : Widgets::colGreyBg;
                ImU32 colBorder = draggedTo ? Widgets::colSelected.second : Widgets::colHoveredGrey;

                drawList->AddRectFilled(
                    cursor, ImVec2(cursor.x + widthPadded / 2 - 2 * INNER_PADDING, cursor.y + headerHeight), colBg, 0.0f);
                drawList->AddRect(cursor, ImVec2(cursor.x + widthPadded / 2 - 2 * INNER_PADDING, cursor.y + headerHeight), colBorder, 0.0f);
            }

            drawList->AddText(ImVec2(cursor.x + INNER_TEXT_PADDING, cursor.y + INNER_TEXT_PADDING),
                              hovered ? IM_COL32(255, 255, 255, 255) : IM_COL32(220, 220, 220, 255),
                              header.name.c_str());
            cursor.y += headerHeight;
        }

        ImVec2 end = cursor;
        // Table
        float tableWidth = ImGui::CalcTextSize(table.c_str()).x;
        cursor.x = begin.x + widthPadded - tableWidth - OUTER_PADDING - INNER_PADDING;
        cursor.y = begin.y + (cursor.y + INNER_PADDING - begin.y) / 2 - headerHeight / 2;
        drawList->AddText(cursor, IM_COL32(255, 255, 255, 255), table.c_str());

        drawList->AddLine(ImVec2(begin.x + widthPadded / 2, begin.y),
                          ImVec2(begin.x + widthPadded / 2, end.y + INNER_PADDING),
                          IM_COL32(255, 255, 255, 120),
                          1.0f);

        ImGui::Dummy(ImVec2(0, OUTER_PADDING));
        ImGui::PopID();
    }

    bool handleDrag() {
        // TODO do not allow all types of data (types need to match, no pkeys)
        bool success = false;
        if (ImGui::BeginDragDropTarget()) {
            // Accept payload before delivery
            const ImGuiPayload* payload =
                ImGui::AcceptDragDropPayload(mappingStrings.at(mappingTypes::HEADER_HEADER).c_str(),
                                             ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
            if (payload) {
                success = true;
                if (payload->IsDelivery()) {
                    // TODO: Handle data
                }
            }
            ImGui::EndDragDropTarget();
        }
        return success;
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