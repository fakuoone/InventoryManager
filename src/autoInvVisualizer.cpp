
#include "userInterface/autoInvVisualizer.hpp"

namespace AutoInv {

void CsvVisualizer::setData(std::shared_ptr<const completeDbData> newData) {
    dbData = newData;
    // TODO: Reset old mappings etc

    destId id = 0;
    for (const std::string& s : dbData->tables) {
        std::vector<DestinationDetail> headers;
        for (const tHeaderInfo& header : dbData->headers.at(s).data) {
            headers.push_back(DestinationDetail(header, id, header.type != headerType::PRIMARY_KEY));
            id++;
        }
        dbHeaderWidgets.push_back(std::move(MappingDestination(s, std::move(headers), true)));
    }
}

bool CsvVisualizer::handleDrag(destId destination, const ImGuiPayload* payload) {
    if (payload) {
        if (payload->IsDelivery()) {
            sourceId id = *static_cast<const sourceId*>(payload->Data);
            createMapping(id, destination);
        }
        return true;
    }
    return false;
}
} // namespace AutoInv