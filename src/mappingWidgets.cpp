#include "userInterface/mappingWidgets.hpp"
#include "userInterface/autoInvVisualizer.hpp"

namespace AutoInv {

MappingSource::MappingSource(const std::string& cSelectedField, const std::string& cHeader, const std::string& cExample)
    : data(cSelectedField, cHeader, cExample, parentVisualizer->getNextIdSource()) {}
MappingSource::~MappingSource() {
    parentVisualizer->removeSourceAnchor(data.id);
    parentVisualizer->removeMappingToDbFromSource(data.id);
}

void MappingSource::setInteractionHandler(CsvMappingVisualizer* handler) {
    parentVisualizer = handler;
}

const std::string& MappingSource::getAttribute() const {
    return data.apiSelector;
}

float MappingSource::getTotalHeight() const {
    return 2 * (singleAttributeHeight) + 2 * INNER_PADDING;
}

void MappingSource::draw(float width) {
    // TODO: Fix slight layout issues regarding actualwidth
    // Calc Height
    singleAttributeHeight = ImGui::CalcTextSize(data.primaryField.c_str()).y + 2 * INNER_TEXT_PADDING;
    const float height = getTotalHeight();

    const float anchorRadius = singleAttributeHeight / 2 - INNER_PADDING * 2;
    const float actualWidth = width; //  + 2 * OUTER_PADDING + INNER_PADDING + 2 * anchorRadius;

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
    ImGui::InvisibleButton(data.primaryField.c_str(), ImVec2(width, singleAttributeHeight));

    // hover effect on header
    cursor.x += INNER_PADDING;
    cursor.y += INNER_PADDING;
    bool hovered = ImGui::IsItemHovered();
    bool dragged = beginDrag();
    if (hovered || dragged) {
        // handle drag drop source
        ImU32 colBg = dragged ? Widgets::colSelected.first : Widgets::colGreyBg;
        ImU32 colBorder = dragged ? Widgets::colSelected.second : Widgets::colHoveredGrey;

        drawList->AddRectFilled(cursor, ImVec2(cursor.x + actualWidth - 2 * INNER_PADDING, cursor.y + singleAttributeHeight), colBg, 0.0f);
        drawList->AddRect(cursor, ImVec2(cursor.x + actualWidth - 2 * INNER_PADDING, cursor.y + singleAttributeHeight), colBorder, 0.0f);
    }

    // draw anchor

    const ImVec2 anchorCenter = ImVec2(cursor.x + actualWidth - INNER_PADDING - 2 * anchorRadius, cursor.y + singleAttributeHeight / 2);
    drawList->AddCircleFilled(anchorCenter, anchorRadius, Widgets::colHoveredGrey);

    // store anchor in parent
    parentVisualizer->storeAnchorSource(data.id, anchorCenter);

    // header
    drawList->AddText(ImVec2(cursor.x + INNER_TEXT_PADDING, cursor.y + INNER_TEXT_PADDING),
                      hovered ? IM_COL32(255, 255, 255, 255) : IM_COL32(220, 220, 220, 255),
                      data.apiSelector.empty() ? data.primaryField.c_str() : data.apiSelector.c_str());
    cursor.y += singleAttributeHeight;

    // example
    drawList->PushClipRect(ImVec2(cursor.x + INNER_TEXT_PADDING, cursor.y + INNER_TEXT_PADDING),
                           ImVec2(cursor.x + actualWidth - 2 * INNER_TEXT_PADDING, cursor.y + singleAttributeHeight),
                           true);

    drawList->AddText(
        ImVec2(cursor.x + INNER_TEXT_PADDING, cursor.y + INNER_TEXT_PADDING), IM_COL32(220, 220, 220, 255), data.example.c_str());

    drawList->PopClipRect();
    cursor.y += singleAttributeHeight;

    ImGui::SetCursorScreenPos(cursor);
    ImGui::Dummy(ImVec2(0, OUTER_PADDING));
    ImGui::PopID();
}

bool MappingSource::beginDrag() const {
    if (ImGui::BeginDragDropSource()) {
        ImGui::SetDragDropPayload(imguiMappingDragString.data(), &data, sizeof(data));
        ImGui::EndDragDropSource();
        return true;
    }
    return false;
}

void MappingDestination::setInteractionHandler(CsvMappingVisualizer* handler) {
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
    for (auto& header : headers) {
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

template <typename T> bool handleDragInternal(CsvMappingVisualizer* parentVisualizer, const mappingTypes mapType, T& destination) {
    bool success = false;
    if (ImGui::BeginDragDropTarget()) {
        const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
            imguiMappingDragString.data(), ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
        success = parentVisualizer->handleDrag(destination, payload);
        ImGui::EndDragDropTarget();
    }
    return success;
}

bool MappingDestinationDb::handleDrag(DbDestinationDetail& header) {
    // TODO types need to match
    if (header.header.type == headerType::PRIMARY_KEY) {
        return false;
    }
    if (!parentVisualizer) {
        return false;
    }
    return handleDragInternal<DbDestinationDetail>(parentVisualizer, mappingTypes::HEADER_HEADER, header);
}

bool MappingDestinationToApi::beginDrag() {
    if (ImGui::BeginDragDropSource()) {
        ImGui::SetDragDropPayload(imguiMappingDragString.data(), &data, sizeof(data));
        ImGui::EndDragDropSource();
        return true;
    }
    return false;
}

void MappingDestinationToApi::draw(float width) {
    ImGui::PushID(this);
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    const float widthPadded = width - 2 * OUTER_PADDING;

    // Calc Height
    float dataPointHeight = ImGui::CalcTextSize(data.example.c_str()).y + 2 * INNER_TEXT_PADDING;
    float dataHeight = dataPointHeight;
    if (!selectedFields.empty()) {
        dataHeight = std::max(selectedFields[0].getTotalHeight() * selectedFields.size(), dataHeight);
    }

    ImVec2 begin = ImGui::GetCursorScreenPos();
    begin.x += OUTER_PADDING;
    begin.y += OUTER_PADDING;
    ImVec2 cursor = begin;
    // complete background
    ImVec2 bgRectBegin = cursor;
    ImVec2 bgRectEnd = ImVec2(cursor.x + widthPadded, cursor.y + dataHeight + 2 * INNER_PADDING);
    drawList->AddRectFilled(bgRectBegin, bgRectEnd, Widgets::colGreyBg, 0.0f);
    drawList->AddRect(bgRectBegin, bgRectEnd, IM_COL32(120, 120, 120, 200), 0.0f);

    const float anchorRadius = dataPointHeight / 2 - INNER_PADDING * 2;

    cursor.x += INNER_PADDING;
    cursor.y += INNER_PADDING;

    const ImVec2 anchorCenterLeft = ImVec2(cursor.x + INNER_PADDING + anchorRadius, cursor.y + dataHeight / 2);
    const float cellWidthLeft = (widthPadded * 0.4) - INNER_PADDING;
    const float cellWidthRight = (widthPadded * 0.6) - INNER_PADDING;
    {
        // LEFT side drag destination
        if (data.mappable) {
            drawList->AddCircleFilled(anchorCenterLeft, anchorRadius, Widgets::colHoveredGrey);
        }

        ImGui::SetCursorScreenPos(cursor);
        ImGui::InvisibleButton(data.example.c_str(), ImVec2(cellWidthLeft, dataHeight));
        bool hovered = ImGui::IsItemHovered();

        // 1. click: query api, 2nd click: open selectionpoup
        if (ImGui::IsItemClicked()) {
            if (!previewData->fields.empty()) {
                ImGui::OpenPopup(API_POPUP.data());
            } else if (!data.example.empty()) {
                parentVisualizer->handleApiClick(*this);
            }
        }

        // preview
        ImVec2 previewPos = ImGui::GetWindowPos();
        drawPreview(ImVec2(previewPos.x + OUTER_PADDING, previewPos.y + OUTER_PADDING + dataHeight));

        bool draggedTo = handleDrag(data);

        if ((hovered || draggedTo) && data.mappable) {
            ImU32 colBg = draggedTo ? Widgets::colSelected.first : Widgets::colGreyBg;
            ImU32 colBorder = draggedTo ? Widgets::colSelected.second : Widgets::colHoveredGrey;

            drawList->AddRectFilled(cursor, ImVec2(cursor.x + cellWidthLeft - 2 * INNER_PADDING, cursor.y + dataHeight), colBg, 0.0f);
            drawList->AddRect(cursor, ImVec2(cursor.x + cellWidthLeft - 2 * INNER_PADDING, cursor.y + dataHeight), colBorder, 0.0f);
        }

        drawList->AddText(ImVec2(cursor.x + INNER_PADDING + INNER_TEXT_PADDING + 2 * anchorRadius,
                                 cursor.y + dataHeight / 2 - dataPointHeight / 2 + INNER_TEXT_PADDING),
                          hovered ? IM_COL32(255, 255, 255, 255) : IM_COL32(220, 220, 220, 255),
                          data.example.c_str());

        // store anchor in parent
        parentVisualizer->storeAnchorDest(data.id, anchorCenterLeft);
    }

    {
        cursor.y = begin.y;
        // RIGHT side drag source
        for (MappingSource& source : selectedFields) {
            cursor.x = begin.x + INNER_PADDING * 2 + cellWidthLeft;
            ImGui::SetCursorScreenPos(cursor);
            source.draw(cellWidthRight);
            cursor.y += source.getTotalHeight();
        }
    }

    ImGui::Dummy(ImVec2(0, OUTER_PADDING));
    ImGui::PopID();
}

void MappingDestinationToApi::drawPreview(ImVec2 startUp) {
    ImGui::SetNextWindowSizeConstraints(ImVec2(300, 300), ImVec2(700, 600));
    ImGui::SetNextWindowPos(startUp, ImGuiCond_Appearing);
    if (ImGui::BeginPopup(API_POPUP.data(), ImGuiWindowFlags_AlwaysAutoResize)) {
        drawJsonTree(previewData->fields, &selectedFields, data.attribute);
        ImGui::EndPopup();
    }
}

bool MappingDestinationToApi::handleDrag(ApiDestinationDetail& detail) {
    if (!parentVisualizer) {
        return false;
    }
    return handleDragInternal<ApiDestinationDetail>(parentVisualizer, mappingTypes::HEADER_API, detail);
}

const std::string& MappingDestinationToApi::getExample() const {
    return data.example;
}

MappingIdType MappingDestinationToApi::getId() const {
    return data.id;
}

void MappingDestinationToApi::setExample(const std::string& newData) {
    data.example = newData;
}

void MappingDestinationToApi::setAttribute(const std::string& newData) {
    data.attribute = newData;
}

void MappingDestinationToApi::removeFields() {
    selectedFields.clear();
}

const std::vector<MappingSource>& MappingDestinationToApi::getFields() const {
    return selectedFields;
}

Widgets::MouseEventType isMouseOnLine(const ImVec2& p1, const ImVec2& p2, const float thickness) {
    ImGuiIO& io = ImGui::GetIO();
    const bool equalPoints = p1.x == p2.x && p1.y == p2.y;
    const bool xOutOfRange = (io.MousePos.x >= p1.x && io.MousePos.x >= p2.x) || (io.MousePos.x <= p1.x && io.MousePos.x <= p2.x);
    const bool yOutOfRange = (io.MousePos.y >= p1.y && io.MousePos.y >= p2.y) || (io.MousePos.y <= p1.y && io.MousePos.y <= p2.y);
    if (thickness == 0 || equalPoints || xOutOfRange || yOutOfRange) {
        return Widgets::MouseEventType::NONE;
    }

    float l21 = std::sqrt(std::pow((p2.y - p1.y), 2) + std::pow((p2.x - p1.x), 2));
    float area = std::abs((p2.y - p1.y) * io.MousePos.x - (p2.x - p1.x) * io.MousePos.y + p2.x * p1.y - p2.y * p1.x);

    if (area / l21 < thickness) {
        if (io.MouseClicked[ImGuiMouseButton_Left]) {
            return Widgets::MouseEventType::CLICK;
        }
        return Widgets::MouseEventType::HOVER;
    }
    return Widgets::MouseEventType::NONE;
}

std::string getValueFromJsonCell(const nlohmann::json& value) {
    return value.is_string()    ? value.get<std::string>()
           : value.is_boolean() ? (value.get<bool>() ? "true" : "false")
           : value.is_null()    ? "null"
                                : value.dump();
}

void handleEntry(const nlohmann::json& value,
                 const std::string& key,
                 std::vector<MappingSource>* selected,
                 const std::string& path,
                 const std::string& source) {
    if (value.is_object() || value.is_array()) {
        if (ImGui::TreeNode(key.c_str())) {
            drawJsonTree(value, selected, source, path);
            ImGui::TreePop();
        }
    } else {
        std::string valueStr = getValueFromJsonCell(value);
        std::string label = key + ": " + valueStr;
        bool isSelected = false;
        if (selected) {
            auto it = std::find_if(selected->begin(), selected->end(), [&](const MappingSource& s) { return s.getAttribute() == path; });
            if (it != selected->end()) {
                isSelected = true;
            }
        }
        if (ImGui::Selectable(label.c_str(), isSelected)) {
            if (selected) {
                auto it =
                    std::find_if(selected->begin(), selected->end(), [&](const MappingSource& s) { return s.getAttribute() == path; });
                if (it != selected->end()) {
                    selected->erase(it);
                } else {
                    selected->emplace_back(source, path, valueStr);
                }
            }
        }
    }
}

void drawJsonTree(const nlohmann::json& j, std::vector<MappingSource>* selected, const std::string& source, std::string path) {
    if (j.is_object()) { // OBJECT
        for (auto it = j.begin(); it != j.end(); ++it) {
            const std::string key = it.key();
            const nlohmann::json& value = it.value();
            std::string currentPath = path.empty() ? key : path + "/" + key;
            handleEntry(value, key, selected, currentPath, source);
        }
    } else if (j.is_array()) { // ARRAY
        for (size_t i = 0; i < j.size(); ++i) {
            const nlohmann::json& value = j[i];
            std::string indexLabel = "[" + std::to_string(i) + "]";
            std::string currentPath = path + "/" + std::to_string(i);
            handleEntry(value, indexLabel, selected, currentPath, source);
        }
    }
}

} // namespace AutoInv