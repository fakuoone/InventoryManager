
#include "userInterface/autoInvVisualizer.hpp"

namespace AutoInv {

void CsvMappingVisualizer::setData(std::shared_ptr<const CompleteDbData> newData) {
    dbData_ = newData;
    // destAnchors = WidgetAnchors();
    // sourceAnchors = WidgetAnchors();
    // csvHeaderWidgets.clear();
    dbHeaderWidgets_.clear();
    // mappingsN.clear();
    // mappingsToApiWidgets.clear();
    // mappingsDrawingInfo.clear();

    MappingIdType id = destAnchors_.largestId + 1;
    for (const std::string& s : dbData_->tables) {
        std::vector<DbDestinationDetail> destDetails;
        for (const HeaderInfo& header : dbData_->headers.at(s).data) {
            destDetails.push_back(DbDestinationDetail(s, header, id, header.type != DB::HeaderTypes::PRIMARY_KEY));
            destAnchors_.anchors[id] = ImVec2();
            id++;
        }
        dbHeaderWidgets_.push_back(MappingDestinationDb(s, std::move(destDetails), true));
    }
    destAnchors_.largestId = id;
}

void CsvMappingVisualizer::storeAnchorSource(MappingIdType source, ImVec2 pos) {
    sourceAnchors_.largestId = std::max(sourceAnchors_.largestId, source);
    sourceAnchors_.anchors[source] = pos;
}

void CsvMappingVisualizer::storeAnchorDest(MappingIdType dest, ImVec2 pos) {
    destAnchors_.largestId = std::max(destAnchors_.largestId, dest);
    destAnchors_.anchors[dest] = pos;
}

MappingIdType CsvMappingVisualizer::getNextIdSource() {
    return ++sourceAnchors_.largestId;
}

MappingIdType CsvMappingVisualizer::getNextIdDest() {
    return ++destAnchors_.largestId;
}

void CsvMappingVisualizer::removeSourceAnchor(MappingIdType id) {
    if (sourceAnchors_.largestId == id) { sourceAnchors_.largestId--; }
    sourceAnchors_.anchors.erase(id);
}

void CsvMappingVisualizer::handleApiClick(MappingDestinationToApi& destination) {
    std::string example = destination.getExample();
    if (example == DEFAULT_EXAMPLE) { return; }
    destination.previewData->loading = true;
    api_.fetchExample(std::move(example), *destination.previewData);
}

DragResult CsvMappingVisualizer::handleDrag(ApiDestinationDetail& destination, const ImGuiPayload* payload) {
    if (payload) {
        if (!payload->Data) { return DragResult::OTHER; }
        if (std::string_view(payload->DataType) != imguiMappingDragString) { return DragResult::OTHER; }
        const SourceDetail source = *static_cast<const SourceDetail*>(payload->Data);
        if (!source.apiSelector.empty()) { return DragResult::NOT_MAPPABLE; }
        if (hasMapping(destination.id)) { return DragResult::EXISTING; }
        if (source.dataCategory != destination.dataCategory && destination.dataCategory != DB::TypeCategory::ANY) {
            return DragResult::WRONG_TYPE;
        }
        // SUCCESS PATH
        if (payload->IsDelivery()) {
            logger_.pushLog(Log{"PAYLOAD DELIVERED"});
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
        if (hasMapping(destination.id)) { return DragResult::EXISTING; }
        if (source.dataCategory != DB::getCategory(destination.header.dataType)) { return DragResult::WRONG_TYPE; }
        // SUCCESS PATH
        if (payload->IsDelivery()) {
            logger_.pushLog(Log{"PAYLOAD DELIVERED"});
            createMappingToDb(source, destination);
            return DragResult::SUCCESS;
        }
        return DragResult::ALLOWED;
    }
    return DragResult::OTHER;
}

const std::vector<MappingNumber>& CsvMappingVisualizer::getMappings() const {
    return mappingsN_;
}

} // namespace AutoInv