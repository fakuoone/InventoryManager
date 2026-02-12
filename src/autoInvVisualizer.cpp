
#include "userInterface/autoInvVisualizer.hpp"

namespace AutoInv {

void CsvMappingVisualizer::setData(std::shared_ptr<const completeDbData> newData) {
    dbData = newData;
    // TODO: Reset old mappings etc

    mappingIdType id = 0;
    for (const std::string& s : dbData->tables) {
        std::vector<DbDestinationDetail> destDetails;
        for (const tHeaderInfo& header : dbData->headers.at(s).data) {
            destDetails.push_back(DbDestinationDetail(s, header, id, header.type != headerType::PRIMARY_KEY));
            id++;
        }
        dbHeaderWidgets.push_back(std::move(MappingDestinationDb(s, std::move(destDetails), true)));
    }
}

bool CsvMappingVisualizer::handleDrag(const ApiDestinationDetail& destination, const ImGuiPayload* payload) {
    // TODO: complete function
    if (payload) {
        // const SourceDetail source = *static_cast<const SourceDetail*>(payload->Data);
        return true;
    }
    return false;
}
bool CsvMappingVisualizer::handleDrag(const DbDestinationDetail& destination, const ImGuiPayload* payload) {
    if (payload) {
        const SourceDetail source = *static_cast<const SourceDetail*>(payload->Data);
        if (hasMappingToDb(source, destination)) {
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