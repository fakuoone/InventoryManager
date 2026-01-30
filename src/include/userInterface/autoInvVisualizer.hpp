#pragma once

#include "autoInv.hpp"
#include "dbService.hpp"

#include <filesystem>

namespace AutoInv {

class BomVisualizer {
  private:
    DbService& dbService;
    BomReader& bomReader;
    Logger& logger;

  public:
    BomVisualizer(DbService& cDbService, BomReader& cBomReader, Logger& cLogger)
        : dbService(cDbService), bomReader(cBomReader), logger(cLogger) {};

    void run() {
        if (bomReader.isDataReady()) {
            const std::vector<std::string>& headers = bomReader.getHeader();
            for (const std::string& header : headers) {
                ImGui::TextUnformatted(header.c_str());
                ImGui::SameLine();
            }
        }
    }
};

} // namespace AutoInv