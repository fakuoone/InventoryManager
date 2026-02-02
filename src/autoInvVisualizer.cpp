
#include "userInterface/autoInvVisualizer.hpp"

namespace AutoInv {

void CsvVisualizer::setData(std::shared_ptr<const completeDbData> newData) {
    dbData = newData;
    for (const std::string& s : dbData->tables) {
        dbHeaderWidgets.push_back(std::move(MappingDestination(s, dbData->headers.at(s).data, true)));
    }
}

bool CsvVisualizer::handleDrag(const ImGuiPayload* payload) {
    if (payload) {
        if (payload->IsDelivery()) {
            // TODO: Handle data
        }
        return true;
    }
    return false;
}
} // namespace AutoInv