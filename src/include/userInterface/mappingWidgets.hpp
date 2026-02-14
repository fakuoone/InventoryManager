#pragma once

#include "autoInv.hpp"
#include "dbService.hpp"
#include "userInterface/uiTypes.hpp"
#include "userInterface/widgets.hpp"

#include <filesystem>
#include <map>
#include <string_view>

namespace AutoInv {
class CsvMappingVisualizer;
static constexpr float INNER_PADDING = 3.0f;
static constexpr float INNER_TEXT_PADDING = 2.0f;
static constexpr float OUTER_PADDING = 3.0f;

enum class mappingTypes { HEADER_HEADER, HEADER_API, API_HEADER };

static constexpr std::string_view imguiMappingDragString = "MAPPING";

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
    mappingIdType id;
    std::string dataPoint;
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
    static void setInteractionHandler(CsvMappingVisualizer* handler);
    const std::string& getHeader();
    void draw(const float width);
    bool beginDrag();
};

class MappingDestination {
  protected:
    static inline CsvMappingVisualizer* parentVisualizer;
    bool mappable = true;

  public:
    static void setInteractionHandler(CsvMappingVisualizer* handler);
    virtual void draw(float width) = 0;

    MappingDestination(bool cMappable) : mappable(cMappable) {}
};

class MappingDestinationDb : public MappingDestination {
  private:
    const std::string table;
    std::vector<DbDestinationDetail> headers;

  public:
    MappingDestinationDb(const std::string cTable, const std::vector<DbDestinationDetail> cHeaders, bool cMappable)
        : MappingDestination(cMappable), table(cTable), headers(cHeaders) {}

    void draw(float width) override;
    bool handleDrag(DbDestinationDetail& headerInfo);
};

class MappingDestinationToApi : public MappingDestination {
  private:
    ApiDestinationDetail data;
    std::string selectedField;

  public:
    ApiPreviewState& previewData;
    MappingDestinationToApi(ApiDestinationDetail cData, ApiPreviewState& cPreviewData, bool cMappable)
        : MappingDestination(cMappable), data(cData), previewData(cPreviewData) {}

    void draw(float width) override;
    void drawPreview(const char* popup);
    bool handleDrag(ApiDestinationDetail& detail);
    const std::string& getDataPoint() const;
    mappingIdType getId() const;
    void setDataPoint(const std::string& newData);
};
} // namespace AutoInv