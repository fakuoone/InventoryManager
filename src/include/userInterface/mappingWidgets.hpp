#pragma once

#include "autoInv.hpp"
#include "dataTypes.hpp"
#include "dbService.hpp"
#include "userInterface/widgets.hpp"

#include <filesystem>
#include <map>
#include <string_view>

namespace AutoInv {
class CsvMappingVisualizer;
static constexpr float INNER_PADDING = 3.0f;
static constexpr float INNER_TEXT_PADDING = 2.0f;
static constexpr float OUTER_PADDING = 3.0f;

enum class MappingTypes { HEADER_HEADER, HEADER_API, API_HEADER };

enum class DragResult { ALLOWED, SUCCESS, WRONG_TYPE, EXISTING, NOT_MAPPABLE, OTHER };

struct DragState {
    DragResult result;
    bool hovered;
};

static constexpr std::string_view imguiMappingDragString = "MAPPING";

struct MappingDrawing {
    float width;
};

struct DbDestinationDetail {
    std::string table;
    HeaderInfo header;
    MappingIdType id;
    bool mappable = false;
};

struct ApiDestinationDetail {
    bool mappable = false;
    MappingIdType id;
    std::string example;
    std::string attribute;
    DB::TypeCategory dataCategory;
};

struct SourceDetail {
    std::string primaryField; // the main data field (csv source)
    std::string apiSelector;  // for api selector
    std::string example;
    MappingIdType id;
    DB::TypeCategory dataCategory;
};

class MappingSource {
  private:
    static inline CsvMappingVisualizer* parentVisualizer;
    SourceDetail data;
    float singleAttributeHeight;

  public:
    MappingSource(const std::string& cPrimary, const std::string& cApiSelector, const std::string& cExample, DB::TypeCategory cDataType);
    ~MappingSource();
    static void setInteractionHandler(CsvMappingVisualizer* handler);
    const std::string& getAttribute() const;
    float getTotalHeight() const;
    void draw(const float width);
    bool beginDrag() const;
    const SourceDetail& getData() const;
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
    DragState handleDrag(DbDestinationDetail& headerInfo);
    const std::string& getTable() const;
    const std::vector<DbDestinationDetail>& getHeaders() const;
};

class MappingDestinationToApi : public MappingDestination {
  private:
    static constexpr std::string_view API_POPUP = "API";
    ApiDestinationDetail data;
    std::vector<MappingSource> selectedFields;
    void drawPreview(ImVec2 startUp);
    DragState handleDrag(ApiDestinationDetail& detail);
    bool beginDrag();

  public:
    UI::ApiPreviewState* previewData;
    MappingDestinationToApi(ApiDestinationDetail cData, UI::ApiPreviewState* cPreviewData, bool cMappable)
        : MappingDestination(cMappable), data(cData), previewData(cPreviewData) {}

    void draw(float width) override;
    const std::string& getExample() const;
    MappingIdType getId() const;
    void setExample(const std::string& newData);
    void setAttribute(const std::string& newData);
    void removeFields();
    const std::string& getSource() const;
    const std::vector<MappingSource>& getFields() const;
    ApiDestinationDetail& getOrSetData(); // not good
    const MappingSource& addField(MappingSource field);
};

std::string getValueFromJsonCell(const nlohmann::json& value);
void handleEntry(const nlohmann::json& value,
                 const std::string& key,
                 std::vector<MappingSource>* selected,
                 const std::string& path,
                 const std::string& source);
void drawJsonTree(const nlohmann::json& j, std::vector<MappingSource>* selected, const std::string& source, std::string path = "");

} // namespace AutoInv