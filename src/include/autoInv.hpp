/*
"Reference","Qty","Value","DNP","Exclude from BOM","Exclude from Board","Footprint","Datasheet"
"C1,C9,C42,C43,C51","5","10u","","","","Capacitor_SMD:C_0805_2012Metric_Pad1.18x1.45mm_HandSolder","~"
"C2,C5,C11,C13,C14,C16,C19,C20,C21,C24,C30","11","1u","","","","Capacitor_SMD:C_0805_2012Metric_Pad1.18x1.45mm_HandSolder","~"
"C3,C6,C7,C8,C22,C23,C25,C26,C27,C28,C31,C36,C37,C48,C49,C52,C53","17","100n","","","","Capacitor_SMD:C_0805_2012Metric_Pad1.18x1.45mm_HandSolder","~"
"C4","1","2.5n","","","","Capacitor_SMD:C_0805_2012Metric_Pad1.18x1.45mm_HandSolder","~"
"C10,C17,C29","3","1n","","","","Capacitor_SMD:C_0805_2012Metric_Pad1.18x1.45mm_HandSolder","~"
"C12","1","10u","","","","Capacitor_SMD:C_1206_3216Metric_Pad1.33x1.80mm_HandSolder","~"
"C15,C18","2","4.7u","","","","Capacitor_SMD:C_0805_2012Metric_Pad1.18x1.45mm_HandSolder","~"
"C32,C33,C44","3","10p","","","","Capacitor_SMD:C_0805_2012Metric_Pad1.18x1.45mm_HandSolder","~"
"C34,C35,C40,C41,C45,C46,C47","7","100p","","","","Capacitor_SMD:C_0805_2012Metric_Pad1.18x1.45mm_HandSolder","~"
"C38,C39,C50","3","47p","","","","Capacitor_SMD:C_0805_2012Metric_Pad1.18x1.45mm_HandSolder","~"
"D1,D2,D3","3","1PS79SB30Z","","","","Diode_SMD:D_SOD-523","~"
"D4,D7","2","RFC02MM2STFTR","","","","Diode_SMD:D_SOD-123F","http://www.vishay.com/docs/88503/1n4001.pdf"
"D5,D6","2","1N4148WS","","","","Diode_SMD:D_SOD-323_HandSoldering","https://www.vishay.com/docs/85751/1n4148ws.pdf"
"D8,D9,D10,D14,D15,D16","6","Yellow","","","","LED_SMD:JE2835APAN0001A0000N0000001","http://www.osram-os.com/Graphics/XPic6/00029609_0.pdf/SFh%20460.pdf"
"D11,D12,D13","3","Red","","","","LED_SMD:JE2835APAN0001A0000N0000001","http://www.osram-os.com/Graphics/XPic6/00029609_0.pdf/SFh%20460.pdf"
"D17","1","BZT52B2V4","","","","Diode_SMD:D_SOD-123F","https://diotec.com/tl_files/diotec/files/pdf/datasheets/bzt52b2v4.pdf"
"D18","1","GR","","","","Diode_SMD:D_0805_2012Metric_Pad1.15x1.40mm_HandSolder","~"
*/

#pragma once

#include "changeTracker.hpp"
#include "logger.hpp"
#include "threadPool.hpp"

namespace AutoInv {
std::vector<std::string> parseLine(const std::string& line) {
    // https://stackoverflow.com/users/25450/sastanin
    enum class csvState { UNQUOTED_FIELD, QUOTED_FIELD, QUOTED_QUOTE };

    csvState state = csvState::UNQUOTED_FIELD;
    std::vector<std::string> fields{""};
    size_t i = 0; // index of the current field
    for (char c : line) {
        switch (state) {
        case csvState::UNQUOTED_FIELD:
            switch (c) {
            case ',': // end of field
                fields.push_back("");
                i++;
                break;
            case '"':
                state = csvState::QUOTED_FIELD;
                break;
            default:
                fields[i].push_back(c);
                break;
            }
            break;
        case csvState::QUOTED_FIELD:
            switch (c) {
            case '"':
                state = csvState::QUOTED_QUOTE;
                break;
            default:
                fields[i].push_back(c);
                break;
            }
            break;
        case csvState::QUOTED_QUOTE:
            switch (c) {
            case ',': // , after closing quote
                fields.push_back("");
                i++;
                state = csvState::UNQUOTED_FIELD;
                break;
            case '"': // "" -> "
                fields[i].push_back('"');
                state = csvState::QUOTED_FIELD;
                break;
            default: // end of quote
                state = csvState::UNQUOTED_FIELD;
                break;
            }
            break;
        }
    }
    return fields;
}

std::vector<std::vector<std::string>> readData(std::filesystem::path csv) {
    std::ifstream file(csv);
    std::string line;
    std::pair<std::size_t, std::size_t> colCounts = {0, 0};
    std::size_t i = 0;
    std::vector<std::vector<std::string>> rows;

    while (std::getline(file, line)) {
        std::vector<std::string> row = parseLine(line);
        colCounts.first = row.size();
        rows.push_back(row);
        if (i > 0) {
            if (colCounts.first != colCounts.second) {
                return std::vector<std::vector<std::string>>{};
            }
        }
        colCounts.second = colCounts.first;
        colCounts.first = 0;
        i++;
    }
    return rows;
}

class CsvReader {
  protected:
    ThreadPool& threadPool;
    ChangeTracker& changeTracker;
    Config& config;
    Logger& logger;

    std::vector<std::vector<std::string>> data;
    std::future<void> fRead;

    CsvReader(ThreadPool& cThreadPool,
              ChangeTracker& cChangeTracker,
              Config& cConfig,
              Logger& cLogger)
        : threadPool(cThreadPool), changeTracker(cChangeTracker), config(cConfig), logger(cLogger) {
    }

    virtual ~CsvReader() = default;

  public:
    void run(std::filesystem::path csv) { data = readData(csv); }

    bool isDataReady() {
        if (fRead.valid()) {
            if (fRead.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                return true;
            }
        }
        return false;
    }

    void read(std::filesystem::path csv) { fRead = threadPool.submit(&CsvReader::run, this, csv); }

    const std::vector<std::string>& getHeader() { return data.front(); }
};

class BomReader : public CsvReader {
  private:
  public:
    BomReader(ThreadPool& cPool, ChangeTracker& cChangeTracker, Config& cConfig, Logger& cLogger)
        : CsvReader(cPool, cChangeTracker, cConfig, cLogger) {}
};

class OrderReader : public CsvReader {
  private:
  public:
    OrderReader(ThreadPool& cPool, ChangeTracker& cChangeTracker, Config& cConfig, Logger& cLogger)
        : CsvReader(cPool, cChangeTracker, cConfig, cLogger) {}
};

}; // namespace AutoInv
