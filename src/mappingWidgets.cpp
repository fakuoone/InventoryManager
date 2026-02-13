#include "userInterface/mappingWidgets.hpp"
#include "userInterface/autoInvVisualizer.hpp"

namespace AutoInv {

void MappingSource::setDragHandler(CsvMappingVisualizer* handler) {
    parentVisualizer = handler;
}

const std::string& MappingSource::getHeader() {
    return data.attribute;
}

void MappingSource::draw(float width) {
    // Calc Height
    const float headerHeight = ImGui::CalcTextSize(data.attribute.c_str()).y + 2 * INNER_TEXT_PADDING;
    const float height = 2 * (headerHeight) + 2 * INNER_PADDING;

    const float anchorRadius = headerHeight / 2 - INNER_PADDING * 2;
    const float actualWidth = width + 2 * OUTER_PADDING + INNER_PADDING + 2 * anchorRadius;

    ImGui::PushID(this);

    ImDrawList* drawList = ImGui::GetWindowDrawList();

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
    ImGui::InvisibleButton(data.attribute.c_str(), ImVec2(width, headerHeight));

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

    // draw anchor

    const ImVec2 anchorCenter = ImVec2(cursor.x + actualWidth - INNER_PADDING - 2 * anchorRadius, cursor.y + headerHeight / 2);
    drawList->AddCircleFilled(anchorCenter, anchorRadius, Widgets::colHoveredGrey);

    // store anchor in parent
    parentVisualizer->storeAnchorSource(data.id, anchorCenter);

    // header
    drawList->AddText(ImVec2(cursor.x + INNER_TEXT_PADDING, cursor.y + INNER_TEXT_PADDING),
                      hovered ? IM_COL32(255, 255, 255, 255) : IM_COL32(220, 220, 220, 255),
                      data.attribute.c_str());
    cursor.y += headerHeight;

    // example
    drawList->PushClipRect(ImVec2(cursor.x + INNER_TEXT_PADDING, cursor.y + INNER_TEXT_PADDING),
                           ImVec2(cursor.x + actualWidth - 2 * INNER_TEXT_PADDING, cursor.y + headerHeight),
                           true);

    drawList->AddText(
        ImVec2(cursor.x + INNER_TEXT_PADDING, cursor.y + INNER_TEXT_PADDING), IM_COL32(220, 220, 220, 255), data.example.c_str());

    drawList->PopClipRect();
    cursor.y += headerHeight;

    ImGui::SetCursorScreenPos(cursor);
    ImGui::Dummy(ImVec2(0, OUTER_PADDING));
    ImGui::PopID();
}

bool MappingSource::beginDrag() {
    if (ImGui::BeginDragDropSource()) {
        ImGui::SetDragDropPayload(imguiMappingDragString.data(), &data, sizeof(data));
        ImGui::EndDragDropSource();
        return true;
    }
    return false;
}

void MappingDestination::setDragHandler(CsvMappingVisualizer* handler) {
    parentVisualizer = handler;
}

void MappingDestinationDb::draw(float width) {
    if (headers.size() == 0) {
        return;
    }
    ImGui::PushID(this);

    ImDrawList* drawList = ImGui::GetWindowDrawList();

    const float widthPadded = width - 2 * OUTER_PADDING;

    // Headerwidth
    float maxHeaderWidth = 0.0f;
    for (const auto& header : headers) {
        maxHeaderWidth = std::max(maxHeaderWidth, ImGui::CalcTextSize(header.header.name.c_str()).x);
    }
    maxHeaderWidth = std::min(maxHeaderWidth, static_cast<float>(0.5 * widthPadded));

    // Calc Height
    const float headerHeight = ImGui::CalcTextSize(headers.at(0).header.name.c_str()).y + 2 * INNER_TEXT_PADDING;
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

    const float anchorRadius = headerHeight / 2 - INNER_PADDING * 2;

    cursor.x += INNER_PADDING;
    cursor.y += INNER_PADDING;

    // Headers column
    for (const auto& header : headers) {
        // draw anchor
        const ImVec2 anchorCenter = ImVec2(cursor.x + INNER_PADDING + anchorRadius, cursor.y + headerHeight / 2);
        if (header.mappable) {
            drawList->AddCircleFilled(anchorCenter, anchorRadius, Widgets::colHoveredGrey);
        }

        // draw headers
        const float cellWidth = widthPadded / 2 - INNER_PADDING;
        ImGui::SetCursorScreenPos(cursor);
        ImGui::InvisibleButton(header.header.name.c_str(), ImVec2(cellWidth, headerHeight));
        bool hovered = ImGui::IsItemHovered();
        bool draggedTo = handleDrag(header);
        if ((hovered || draggedTo) && header.mappable) {
            ImU32 colBg = draggedTo ? Widgets::colSelected.first : Widgets::colGreyBg;
            ImU32 colBorder = draggedTo ? Widgets::colSelected.second : Widgets::colHoveredGrey;

            drawList->AddRectFilled(cursor, ImVec2(cursor.x + widthPadded / 2 - 2 * INNER_PADDING, cursor.y + headerHeight), colBg, 0.0f);
            drawList->AddRect(cursor, ImVec2(cursor.x + widthPadded / 2 - 2 * INNER_PADDING, cursor.y + headerHeight), colBorder, 0.0f);
        }

        drawList->AddText(ImVec2(cursor.x + INNER_PADDING + INNER_TEXT_PADDING + 2 * anchorRadius, cursor.y + INNER_TEXT_PADDING),
                          hovered ? IM_COL32(255, 255, 255, 255) : IM_COL32(220, 220, 220, 255),
                          header.header.name.c_str());

        // store anchor in parent
        parentVisualizer->storeAnchorDest(header.id, anchorCenter);

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

template <typename T> bool handleDragInternal(CsvMappingVisualizer* parentVisualizer, const mappingTypes mapType, const T& destination) {
    bool success = false;
    if (ImGui::BeginDragDropTarget()) {
        const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
            imguiMappingDragString.data(), ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
        success = parentVisualizer->handleDrag(destination, payload);
        ImGui::EndDragDropTarget();
    }
    return success;
}

bool MappingDestinationDb::handleDrag(const DbDestinationDetail& header) {
    // TODO types need to match
    if (header.header.type == headerType::PRIMARY_KEY) {
        return false;
    }
    if (!parentVisualizer) {
        return false;
    }
    return handleDragInternal<DbDestinationDetail>(parentVisualizer, mappingTypes::HEADER_HEADER, header);
}

void MappingDestinationToApi::draw(float width) {
    ImGui::PushID(this);
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    const float widthPadded = width - 2 * OUTER_PADDING;

    // Calc Height
    const float dataHeight = ImGui::CalcTextSize(data.dataPoint.c_str()).y + 2 * INNER_TEXT_PADDING;
    ImVec2 begin = ImGui::GetCursorScreenPos();
    begin.x += OUTER_PADDING;
    begin.y += OUTER_PADDING;
    ImVec2 cursor = begin;
    // complete background
    ImVec2 bgRectBegin = cursor;
    ImVec2 bgRectEnd = ImVec2(cursor.x + widthPadded, cursor.y + dataHeight + 2 * INNER_PADDING);
    drawList->AddRectFilled(bgRectBegin, bgRectEnd, Widgets::colGreyBg, 0.0f);
    drawList->AddRect(bgRectBegin, bgRectEnd, IM_COL32(120, 120, 120, 200), 0.0f);

    const float anchorRadius = dataHeight / 2 - INNER_PADDING * 2;

    cursor.x += INNER_PADDING;
    cursor.y += INNER_PADDING;

    const ImVec2 anchorCenter = ImVec2(cursor.x + INNER_PADDING + anchorRadius, cursor.y + dataHeight / 2);

    if (data.mappable) {
        drawList->AddCircleFilled(anchorCenter, anchorRadius, Widgets::colHoveredGrey);
    }

    const float cellWidth = widthPadded / 2 - INNER_PADDING;
    ImGui::SetCursorScreenPos(cursor);
    ImGui::InvisibleButton(data.dataPoint.c_str(), ImVec2(cellWidth, dataHeight));
    bool hovered = ImGui::IsItemHovered();
    bool draggedTo = handleDrag(data);

    if ((hovered || draggedTo) && data.mappable) {
        ImU32 colBg = draggedTo ? Widgets::colSelected.first : Widgets::colGreyBg;
        ImU32 colBorder = draggedTo ? Widgets::colSelected.second : Widgets::colHoveredGrey;

        drawList->AddRectFilled(cursor, ImVec2(cursor.x + widthPadded / 2 - 2 * INNER_PADDING, cursor.y + dataHeight), colBg, 0.0f);
        drawList->AddRect(cursor, ImVec2(cursor.x + widthPadded / 2 - 2 * INNER_PADDING, cursor.y + dataHeight), colBorder, 0.0f);
    }

    drawList->AddText(ImVec2(cursor.x + INNER_PADDING + INNER_TEXT_PADDING + 2 * anchorRadius, cursor.y + INNER_TEXT_PADDING),
                      hovered ? IM_COL32(255, 255, 255, 255) : IM_COL32(220, 220, 220, 255),
                      data.dataPoint.c_str());

    // store anchor in parent
    parentVisualizer->storeAnchorDest(data.id, anchorCenter);

    ImGui::Dummy(ImVec2(0, OUTER_PADDING));
    ImGui::PopID();
}

bool MappingDestinationToApi::handleDrag(const ApiDestinationDetail& detail) {
    if (!parentVisualizer) {
        return false;
    }
    return handleDragInternal<ApiDestinationDetail>(parentVisualizer, mappingTypes::HEADER_API, detail);
}

Widgets::MOUSE_EVENT_TYPE isMouseOnLine(const ImVec2& p1, const ImVec2& p2, const float thickness) {
    ImGuiIO& io = ImGui::GetIO();
    const bool equalPoints = p1.x == p2.x && p1.y == p2.y;
    const bool xOutOfRange = (io.MousePos.x >= p1.x && io.MousePos.x >= p2.x) || (io.MousePos.x <= p1.x && io.MousePos.x <= p2.x);
    const bool yOutOfRange = (io.MousePos.y >= p1.y && io.MousePos.y >= p2.y) || (io.MousePos.y <= p1.y && io.MousePos.y <= p2.y);
    if (thickness == 0 || equalPoints || xOutOfRange || yOutOfRange) {
        return Widgets::MOUSE_EVENT_TYPE::NONE;
    }

    float l21 = std::sqrt(std::pow((p2.y - p1.y), 2) + std::pow((p2.x - p1.x), 2));
    float area = std::abs((p2.y - p1.y) * io.MousePos.x - (p2.x - p1.x) * io.MousePos.y + p2.x * p1.y - p2.y * p1.x);

    if (area / l21 < thickness) {
        if (io.MouseClicked[ImGuiMouseButton_Left]) {
            return Widgets::MOUSE_EVENT_TYPE::CLICK;
        }
        return Widgets::MOUSE_EVENT_TYPE::HOVER;
    }
    return Widgets::MOUSE_EVENT_TYPE::NONE;
}
} // namespace AutoInv