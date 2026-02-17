
#include "userInterface/autoInvVisualizer.hpp"

namespace AutoInv {

void CsvMappingVisualizer::setData(std::shared_ptr<const completeDbData> newData) {
    dbData = newData;
    destAnchors = WidgetAnchors();
    sourceAnchors = WidgetAnchors();
    csvHeaderWidgets.clear();
    mappingsN.clear();
    // mappingsSToDb.clear();
    mappingsToApiWidgets.clear();
    mappingsToApiWidgets.clear();
    mappingsDrawingInfo.clear();
    // TODO: Reset old mappings etc

    MappingIdType id = 0;
    for (const std::string& s : dbData->tables) {
        std::vector<DbDestinationDetail> destDetails;
        for (const tHeaderInfo& header : dbData->headers.at(s).data) {
            destDetails.push_back(DbDestinationDetail(s, header, id, header.type != headerType::PRIMARY_KEY));
            destAnchors.anchors[id] = ImVec2();
            id++;
        }
        dbHeaderWidgets.push_back(std::move(MappingDestinationDb(s, std::move(destDetails), true)));
    }
    destAnchors.largestId = id;
}

void CsvMappingVisualizer::storeAnchorSource(MappingIdType source, ImVec2 pos) {
    sourceAnchors.largestId = std::max(sourceAnchors.largestId, source);
    sourceAnchors.anchors[source] = pos;
}

void CsvMappingVisualizer::storeAnchorDest(MappingIdType dest, ImVec2 pos) {
    destAnchors.largestId = std::max(destAnchors.largestId, dest);
    destAnchors.anchors[dest] = pos;
}

MappingIdType CsvMappingVisualizer::getNextIdSource() {
    return ++sourceAnchors.largestId;
}

MappingIdType CsvMappingVisualizer::getNextIdDest() {
    return ++destAnchors.largestId;
}

void CsvMappingVisualizer::removeSourceAnchor(MappingIdType id) {
    if (sourceAnchors.largestId == id) {
        sourceAnchors.largestId--;
    }
    sourceAnchors.anchors.erase(id);
}

void CsvMappingVisualizer::handleApiClick(MappingDestinationToApi& destination) {
    destination.previewData->loading = true;
    api.fetchExample(destination.getExample(), *destination.previewData);
}

bool CsvMappingVisualizer::handleDrag(ApiDestinationDetail& destination, const ImGuiPayload* payload) {
    // TODO: complete function
    if (payload) {
        const SourceDetail source = *static_cast<const SourceDetail*>(payload->Data);
        if (hasMapping(source, destination.id)) {
            return false;
        }
        if (std::string_view(payload->DataType) != imguiMappingDragString) {
            return false;
        }
        if (payload->IsDelivery()) {
            createMappingToApi(source, destination);
        }
        return true;
    }
    return false;
}

bool CsvMappingVisualizer::handleDrag(DbDestinationDetail& destination, const ImGuiPayload* payload) {
    if (payload) {
        const SourceDetail source = *static_cast<const SourceDetail*>(payload->Data);
        if (hasMapping(source, destination.id)) {
            return false;
        }
        if (std::string_view(payload->DataType) != imguiMappingDragString) {
            return false;
        }
        if (payload->IsDelivery()) {
            createMappingToDb(source, destination);
        }
        return true;
    }
    return false;
}

} // namespace AutoInv