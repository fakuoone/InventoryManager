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

enum class mappingTypes { HEADER_HEADER, HEADER_API, API_HEADERR };

static std::map<mappingTypes, std::string> mappingStrings = {{mappingTypes::HEADER_HEADER, "HEADER_HEADER"}};

struct MappingDrawing {
    float width;
};

struct DbDestinationDetail {
    std::string table;
    tHeaderInfo header;
    mappingIdType id;
    bool mappable = false;
};

struct ApiDestinationDetail {
    bool mappable = false;
};

struct SourceDetail {
    const std::string attribute;
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
  protected:
    static inline CsvMappingVisualizer* parentVisualizer;
    bool mappable = true;

  public:
    static void setDragHandler(CsvMappingVisualizer* handler);
    virtual void draw(float width) = 0;

    MappingDestination(bool cMappable) : mappable(cMappable) {}
};

class MappingDestinationDb : public MappingDestination {
  private:
    const std::string table;
    const std::vector<DbDestinationDetail> headers;

  public:
    MappingDestinationDb(const std::string cTable, const std::vector<DbDestinationDetail> cHeaders, bool cMappable)
        : MappingDestination(cMappable), table(cTable), headers(cHeaders) {}

    void draw(float width) override;
    bool handleDrag(const DbDestinationDetail& headerInfo);
};

class MappingDestinationToApi : public MappingDestination {
  private:
    ApiDestinationDetail destination;

  public:
    MappingDestinationToApi(bool cMappable) : MappingDestination(cMappable) {}
    void draw(float width) override;
    bool handleDrag(const ApiDestinationDetail& detail);
};
} // namespace AutoInv