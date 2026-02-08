#pragma once

#include "autoInv.hpp"
#include "dbService.hpp"
#include "userInterface/widgets.hpp"

#include <filesystem>
#include <map>

namespace AutoInv {
class CsvMappingVisualizer;
static constexpr const float INNER_PADDING = 3.0f;
static constexpr const float INNER_TEXT_PADDING = 2.0f;
static constexpr const float OUTER_PADDING = 3.0f;

enum class mappingTypes { HEADER_HEADER };

static std::map<mappingTypes, std::string> mappingStrings = {{mappingTypes::HEADER_HEADER, "HEADER_HEADER"}};

struct MappingDrawing {
    float width;
};

struct DestinationDetail {
    std::string table;
    tHeaderInfo header;
    mappingIdType id;
    bool mappable;
};

struct SourceDetail {
    const std::string header;
    const std::string example;
    mappingIdType id;
};

class MappingSource {
  private:
    static inline CsvMappingVisualizer* parentVisualizer;
    SourceDetail data;

  public:
    MappingSource(const std::string& cHeader, const std::string& cExample, mappingIdType cId) : data(cHeader, cExample, cId) {}
    static void setDragHandler(CsvMappingVisualizer* handler);
    const std::string& getHeader();
    void draw(const float width);
    bool beginDrag();
};

class MappingDestination {
  private:
    const std::string table;
    const std::vector<DestinationDetail> headers;
    static inline CsvMappingVisualizer* parentVisualizer;
    bool mappable = true;

  public:
    MappingDestination(const std::string cTable, const std::vector<DestinationDetail> cHeaders, bool cMappable)
        : table(cTable), headers(cHeaders), mappable(cMappable) {}

    static void setDragHandler(CsvMappingVisualizer* handler);
    void draw(const float width);
    bool handleDrag(const DestinationDetail& headerInfo);
};
} // namespace AutoInv