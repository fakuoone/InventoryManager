#pragma once

#include "dbService.hpp"
#include "userInterface/widgets.hpp"

#include <filesystem>
#include <map>

namespace AutoInv {
class CsvVisualizer;
static constexpr const float INNER_PADDING = 3.0f;
static constexpr const float INNER_TEXT_PADDING = 2.0f;
static constexpr const float OUTER_PADDING = 3.0f;

enum class mappingTypes { HEADER_HEADER };

static std::map<mappingTypes, std::string> mappingStrings = {{mappingTypes::HEADER_HEADER, "HEADER_HEADER"}};

using sourceId = uint32_t;
using destId = uint32_t;

struct MappingDrawing {
    float width;
};

struct Mapping {
    sourceId source;
    destId destination;
    bool operator==(const Mapping& other) const { return other.source == source && other.destination == destination; }
};

struct MappingHash {
    size_t operator()(const Mapping& m) const noexcept {
        size_t h1 = std::hash<sourceId>{}(m.source);
        size_t h2 = std::hash<destId>{}(m.destination);
        return h1 ^ (h2 << 1);
    }
};

struct DestinationDetail {
    tHeaderInfo header;
    destId id;
    bool mappable;
};

class MappingSource {
  private:
    const std::string header;
    const std::string example;
    static inline CsvVisualizer* parentVisualizer;
    sourceId id;

  public:
    MappingSource(const std::string& cHeader, const std::string& cExample, sourceId cId) : header(cHeader), example(cExample), id(cId) {}
    static void setDragHandler(CsvVisualizer* handler);
    const std::string& getHeader();
    void draw(const float width);
    bool beginDrag();
};

class MappingDestination {
  private:
    const std::string table;
    const std::vector<DestinationDetail> headers;
    static inline CsvVisualizer* parentVisualizer;
    bool mappable = true;

  public:
    MappingDestination(const std::string cTable, const std::vector<DestinationDetail> cHeaders, bool cMappable)
        : table(cTable), headers(cHeaders), mappable(cMappable) {}

    static void setDragHandler(CsvVisualizer* handler);
    void draw(const float width);
    bool handleDrag(const DestinationDetail& headerInfo);
};
} // namespace AutoInv