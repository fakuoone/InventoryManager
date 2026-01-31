#pragma once

#include "autoInv.hpp"
#include "dbService.hpp"

#include <filesystem>

namespace AutoInv {

class CsvVisualizer {
  protected:
    DbService& dbService;
    Logger& logger;
    std::shared_ptr<const completeDbData> dbData;
    std::vector<const char*> tableCStrings;

    CsvVisualizer(DbService& cDbService, Logger& cLogger) : dbService(cDbService), logger(cLogger) {}

  public:
    virtual ~CsvVisualizer() = default;
    virtual void run(const bool dbDataFresh) = 0;

    void setData(std::shared_ptr<const completeDbData> newData) {
        dbData = newData;
        for (auto& s : dbData->tables) {
            tableCStrings.push_back(s.c_str());
        }
    }
};

template <typename Reader> class CsvVisualizerImpl : public CsvVisualizer {
  protected:
    Reader& reader;

    CsvVisualizerImpl(DbService& cDbService, Reader& cReader, Logger& cLogger) : CsvVisualizer(cDbService, cLogger), reader(cReader) {}

  public:
    void run(const bool dbDataFresh) override {
        if (!dbDataFresh) {
            return;
        }

        if (reader.isDataReady()) {
            // ImGui::BeginChild();
            const auto& headers = reader.getHeader();
            int currentTable;
            int currentHeader;
            for (const auto& header : headers) {
                ImGui::PushID(header.c_str());
                ImGui::TextUnformatted(header.c_str());
                ImGui::SameLine();
                ImGui::PushID(1);
                ImGui::Combo("Select Table", &currentTable, tableCStrings.data(), tableCStrings.size());
                ImGui::PopID();
                ImGui::SameLine();
                ImGui::PushID(2);
                ImGui::Combo("Select Header", &currentHeader, tableCStrings.data(), tableCStrings.size());
                ImGui::PopID();
                ImGui::PopID();
            }
            // ImGui::EndChild();
        }
    }
};

class BomVisualizer : public CsvVisualizerImpl<BomReader> {
  public:
    BomVisualizer(DbService& cDbService, BomReader& cReader, Logger& cLogger) : CsvVisualizerImpl(cDbService, cReader, cLogger) {}
};

class OrderVisualizer : public CsvVisualizerImpl<OrderReader> {
  public:
    OrderVisualizer(DbService& cDbService, OrderReader& cReader, Logger& cLogger) : CsvVisualizerImpl(cDbService, cReader, cLogger) {}
};

} // namespace AutoInv