
#include "userInterface/autoInvVisualizer.hpp"

namespace AutoInv {

void CsvMappingVisualizer::setData(std::shared_ptr<const completeDbData> newData) {
    dbData = newData;
    // TODO: Reset old mappings etc

    mappingIdType id = 0;
    for (const std::string& s : dbData->tables) {
        std::vector<DestinationDetail> destDetails;
        for (const tHeaderInfo& header : dbData->headers.at(s).data) {
            destDetails.push_back(DestinationDetail(s, header, id, header.type != headerType::PRIMARY_KEY));
            id++;
        }
        dbHeaderWidgets.push_back(std::move(MappingDestination(s, std::move(destDetails), true)));
    }
}

bool CsvMappingVisualizer::handleDrag(const DestinationDetail& destination, const ImGuiPayload* payload) {
    if (payload) {
        const SourceDetail source = *static_cast<const SourceDetail*>(payload->Data);
        if (hasMapping(source, destination)) {
            return false;
        }
        if (payload->IsDelivery()) {
            createMapping(source, destination);
        }
        return true;
    }
    return false;
}
} // namespace AutoInv