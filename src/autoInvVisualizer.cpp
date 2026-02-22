
#include "userInterface/autoInvVisualizer.hpp"

namespace AutoInv {

void CsvMappingVisualizer::setData(std::shared_ptr<const CompleteDbData> newData) {
    dbData = newData;
    destAnchors = WidgetAnchors();
    sourceAnchors = WidgetAnchors();
    csvHeaderWidgets.clear();
    dbHeaderWidgets.clear();
    mappingsN.clear();
    mappingsToApiWidgets.clear();
    mappingsToApiWidgets.clear();
    mappingsDrawingInfo.clear();

    MappingIdType id = 0;
    for (const std::string& s : dbData->tables) {
        std::vector<DbDestinationDetail> destDetails;
        for (const HeaderInfo& header : dbData->headers.at(s).data) {
            destDetails.push_back(DbDestinationDetail(s, header, id, header.type != DB::HeaderTypes::PRIMARY_KEY));
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
    if (sourceAnchors.largestId == id) { sourceAnchors.largestId--; }
    sourceAnchors.anchors.erase(id);
}

void CsvMappingVisualizer::handleApiClick(MappingDestinationToApi& destination) {
    destination.previewData->loading = true;
    api.fetchExample(destination.getExample(), *destination.previewData);
}

DragResult CsvMappingVisualizer::handleDrag(ApiDestinationDetail& destination, const ImGuiPayload* payload) {
    if (payload) {
        if (!payload->Data) { return DragResult::OTHER; }
        if (std::string_view(payload->DataType) != imguiMappingDragString) { return DragResult::OTHER; }
        const SourceDetail source = *static_cast<const SourceDetail*>(payload->Data);
        if (hasMapping(source, destination.id)) { return DragResult::EXISTING; }
        if (source.dataCategory != destination.dataCategory && destination.dataCategory != DB::TypeCategory::ANY) {
            return DragResult::WRONG_TYPE;
        }
        // SUCCESS PATH
        if (payload->IsDelivery()) {
            logger.pushLog(Log{"PAYLOAD DELIVERED"});
            createMappingToApi(source, destination);
            return DragResult::SUCCESS;
        }
        return DragResult::ALLOWED;
    }
    return DragResult::OTHER;
}

DragResult CsvMappingVisualizer::handleDrag(DbDestinationDetail& destination, const ImGuiPayload* payload) {
    if (payload) {
        if (!payload->Data) { return DragResult::OTHER; }
        if (std::string_view(payload->DataType) != imguiMappingDragString) { return DragResult::OTHER; }
        const SourceDetail source = *static_cast<const SourceDetail*>(payload->Data);
        if (hasMapping(source, destination.id)) { return DragResult::EXISTING; }
        if (source.dataCategory != DB::getCategory(destination.header.dataType)) { return DragResult::WRONG_TYPE; }
        // SUCCESS PATH
        if (payload->IsDelivery()) {
            logger.pushLog(Log{"PAYLOAD DELIVERED"});
            createMappingToDb(source, destination);
            return DragResult::SUCCESS;
        }
        return DragResult::ALLOWED;
    }
    return DragResult::OTHER;
}

} // namespace AutoInv