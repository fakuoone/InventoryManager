#pragma once

#include "autoInv.hpp"
#include "dataTypes.hpp"
#include "partApi.hpp"
#include "userInterface/mappingWidgets.hpp"
#include "userInterface/widgets.hpp"

namespace AutoInv {
enum class MappingStage { CSV, API };

Widgets::MouseEventType isMouseOnLine(const ImVec2& p1, const ImVec2& p2, const float thickness);

struct WidgetAnchors {
    MappingIdType largestId = 0;
    std::unordered_map<MappingIdType, ImVec2> anchors;
};

class CsvMappingVisualizer {
  protected:
    DbService& dbService;
    PartApi& api;
    Logger& logger;
    std::shared_ptr<const CompleteDbData> dbData;
    UI::DataStates& dataStates;

    MappingStage stage = MappingStage::CSV;

    std::vector<std::string> headers;
    std::vector<DB::TypeCategory> headerTypes;

    std::vector<std::string> firstRow;
    std::vector<MappingDestinationDb> dbHeaderWidgets;
    std::vector<MappingSource> csvHeaderWidgets;

    std::vector<MappingNumber> mappingsN;

    std::vector<MappingDestinationToApi> mappingsToApiWidgets;
    std::unordered_map<MappingIdType, UI::ApiPreviewState> apiPreviewCache;

    WidgetAnchors sourceAnchors;
    WidgetAnchors destAnchors;
    std::unordered_map<MappingNumber, MappingDrawing, MappingHash> mappingsDrawingInfo;

    std::array<char, 256> csvBuffer;

    CsvMappingVisualizer(DbService& cDbService, PartApi& cApi, Logger& cLogger, UI::DataStates& cDataStates)
        : dbService(cDbService), api(cApi), logger(cLogger), dataStates(cDataStates) {}

  public:
    virtual ~CsvMappingVisualizer() = default;
    virtual void run() = 0;
    virtual void createMappingToDb(const SourceDetail& source, const DbDestinationDetail& dest) = 0;
    virtual void createMappingToApi(const SourceDetail& source, ApiDestinationDetail& dest) = 0;
    virtual bool hasMapping(const SourceDetail& source, MappingIdType dest) = 0;
    virtual void removeMappingToDb(const MappingNumber& mapping) = 0;
    virtual void removeMappingToDbFromSource(const MappingIdType mapping) = 0;

    void storeAnchorSource(MappingIdType source, ImVec2 pos);
    void storeAnchorDest(MappingIdType dest, ImVec2 pos);
    MappingIdType getNextIdSource();
    MappingIdType getNextIdDest();
    void removeSourceAnchor(MappingIdType id);

    void setData(std::shared_ptr<const CompleteDbData> newData);

    void handleApiClick(MappingDestinationToApi& destination);

    DragResult handleDrag(DbDestinationDetail& destination, const ImGuiPayload* payload);
    DragResult handleDrag(ApiDestinationDetail& destination, const ImGuiPayload* payload);
};

template <typename Reader> class CsvVisualizerImpl : public CsvMappingVisualizer {
  private:
    static constexpr float LEFT_MIN = 200.0f;
    static constexpr float CENTER_MIN = 400.0f;
    static constexpr float RIGHT_MIN = 200.0f;

    void drawApiWidgets(const float width, ImVec2 popupStartup) {
        for (auto& mapping : mappingsToApiWidgets) {
            mapping.draw(width);
        }
    }

    void drawHead() {
        bool enterPressed = ImGui::InputText("##edit", csvBuffer.data(), BUFFER_SIZE, ImGuiInputTextFlags_EnterReturnsTrue);
        if (enterPressed || ImGui::IsItemDeactivatedAfterEdit()) {
            try {
                reader.read(std::filesystem::path(std::string(csvBuffer.data())));
            } catch (const std::exception& e) {
                logger.pushLog(Log{std::format("ERROR reading order: {}", e.what())});
            }
        }

        // add api intermediate stage
        ImGui::SameLine();
        const char* btnLabel = "ADD API STAGE";
        if (ImGui::Button(btnLabel)) {
            const std::size_t newIndex = ++destAnchors.largestId;
            apiPreviewCache.insert_or_assign(newIndex, UI::ApiPreviewState{});
            mappingsToApiWidgets.emplace_back(
                MappingDestinationToApi(ApiDestinationDetail(true, ++destAnchors.largestId, "NONE", "API", DB::TypeCategory::ANY),
                                        &apiPreviewCache.at(newIndex),
                                        true));
        };

        // Stage selector
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0f);
        int localStage = static_cast<int>(stage);
        ImGui::SliderInt("Stage", &localStage, static_cast<int>(MappingStage::CSV), static_cast<int>(MappingStage::API));
        stage = static_cast<MappingStage>(localStage);

        float buttonWidth = ImGui::CalcTextSize("Commit Mapping").x + ImGui::GetStyle().FramePadding.x * 2.0f;
        float rightEdge = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;

        ImGui::SameLine();
        ImGui::SetCursorPosX(rightEdge - buttonWidth);

        // TODO: Validate mappings
        ImGui::BeginDisabled(!hasMappings());
        if (ImGui::Button("Commit Mapping")) {
            commitMappings();
        }
        ImGui::EndDisabled();
    }

    void drawMappingSourceRawCSV(const float width) {
        for (auto& csvHeaderWidget : csvHeaderWidgets) {
            csvHeaderWidget.draw(width);
        }
    }

  protected:
    Reader& reader;

    CsvVisualizerImpl(DbService& cDbService, Reader& cReader, PartApi& cApi, Logger& cLogger, UI::DataStates& cDataStates)
        : CsvMappingVisualizer(cDbService, cApi, cLogger, cDataStates), reader(cReader) {
        MappingSource::setInteractionHandler(static_cast<CsvMappingVisualizer*>(this));
        MappingDestinationDb::setInteractionHandler(static_cast<CsvMappingVisualizer*>(this));
    }

  public:
    void run() override {
        drawHead();

        ImGui::BeginChild("READER");
        if (dataStates.dbData != UI::DataState::DATA_READY) {
            return;
        }

        checkNewData();

        if (reader.dataValid(false)) {
            const float SPACING = ImGui::GetStyle().ItemSpacing.x * 10;

            ImVec2 avail = ImGui::GetContentRegionAvail();
            float totalMin = LEFT_MIN + CENTER_MIN + RIGHT_MIN + SPACING * 2;
            float extra = std::max(0.0f, avail.x - totalMin);

            // Keep middle column centered, distribute remaining space amongst all three
            float leftWidth = LEFT_MIN + extra * 0.3f;
            float centerWidth = CENTER_MIN + extra * 0.4f;
            float rightWidth = RIGHT_MIN + extra * 0.3f;

            // adapt layout if no api
            if (mappingsToApiWidgets.size() == 0) {
                leftWidth = avail.x / 2 - SPACING / 2;
                rightWidth = avail.x / 2 - SPACING / 2;
            }

            ImVec2 begin = ImGui::GetCursorScreenPos();

            ImGui::BeginChild("CSV", ImVec2(leftWidth, 0), false, ImGuiWindowFlags_NoScrollbar);
            drawMappingSourceRawCSV(leftWidth);
            ImGui::EndChild();
            ImGui::SameLine();

            ImGui::SetCursorPosX((avail.x - centerWidth) / 2);
            ImGui::BeginChild("API", ImVec2(centerWidth, 0), false, ImGuiWindowFlags_NoScrollbar);
            drawApiWidgets(centerWidth, ImVec2(leftWidth + ImGui::GetStyle().ItemSpacing.x, ImGui::GetStyle().ItemSpacing.y));

            ImGui::EndChild();
            ImGui::SameLine();

            ImGui::SetCursorPosX(avail.x - rightWidth);
            ImGui::BeginChild("DB", ImVec2(rightWidth, 0), false, ImGuiWindowFlags_NoScrollbar);
            for (auto& headerWidget : dbHeaderWidgets) {
                headerWidget.draw(rightWidth);
            }
            ImGui::EndChild();

            // clipping rect
            ImVec2 clipMin(begin.x, begin.y);
            ImVec2 clipMax(begin.x + avail.x, begin.y + avail.y);

            // mappings
            const MappingNumber* toRemove = nullptr;

            ImDrawList* drawlist = ImGui::GetWindowDrawList();
            drawlist->PushClipRect(clipMin, clipMax, true);
            for (const MappingNumber& mapping : mappingsN) {
                if (drawMapping(mapping, drawlist, mappingsDrawingInfo.at(mapping))) {
                    toRemove = &mapping;
                }
            }
            drawlist->PopClipRect();

            if (toRemove) {
                removeMappingToDb(*toRemove);
            }
        }
        ImGui::EndChild();
    }

    void checkNewData() {
        if (reader.dataValid(true)) {
            headers = reader.getHeader();
            headerTypes = reader.getHeaderTypes();
            firstRow = reader.getFirstRow();
            csvHeaderWidgets.clear();
            for (std::size_t i = 0; i < headers.size(); ++i) {
                csvHeaderWidgets.emplace_back(headers[i], std::string{}, firstRow[i], headerTypes[i]);
            }
        }
    }

    void createMappingToDb(const SourceDetail& source, const DbDestinationDetail& dest) override {
        if (hasMapping(source, dest.id)) {
            return;
        }
        MappingCsvToDb newMappingS =
            MappingCsvToDb(PreciseMapLocation(source.primaryField, source.apiSelector), PreciseMapLocation(dest.table, dest.header.name));
        SourceType sourceType = source.apiSelector.empty() ? SourceType::CSV : SourceType::API; // not very clean, but gets the job done
        MappingNumber newMappingN = MappingNumber{MappingNumberInternal{source.id, dest.id}, std::move(newMappingS), sourceType};
        mappingsDrawingInfo.insert_or_assign(newMappingN, MappingDrawing());
        mappingsN.emplace_back(std::move(newMappingN));
        // mappingsSToDb.emplace_back(std::move(newMappingS));
    }

    void createMappingToApi(const SourceDetail& source, ApiDestinationDetail& dest) override {
        if (hasMapping(source, dest.id)) {
            return;
        }
        dest.example = source.example;
        dest.attribute = source.primaryField;
        MappingCsvApi newMappingS = MappingCsvApi(source.apiSelector, dest.id);
        MappingNumber newMappingN = MappingNumber{MappingNumberInternal{source.id, dest.id}, std::move(newMappingS), SourceType::API};
        mappingsDrawingInfo.insert_or_assign(newMappingN, MappingDrawing());
        mappingsN.emplace_back(std::move(newMappingN));
        // mappingsSToApi.emplace_back(std::move(newMappingS));
    }

    bool hasMapping(const SourceDetail& source, MappingIdType dest) override {
        if (std::find_if(mappingsN.begin(), mappingsN.end(), [&](const MappingNumber& m) {
                return m.uniqueData == MappingNumberInternal(source.id, dest);
            }) != mappingsN.end()) {
            return true;
        }
        return false;
    }

    void removeMappingToDbFromSource(const MappingIdType sourceId) override {
        auto it = std::find_if(mappingsN.begin(), mappingsN.end(), [&](const MappingNumber& m) { return m.uniqueData.source == sourceId; });
        if (it == mappingsN.end()) {
            return;
        }

        removeMappingToDb(*it);
    }

    void removeMappingToDb(const MappingNumber& mapping) override {
        // not very efficient, but mappingnumber is small
        auto it = std::find(mappingsN.begin(), mappingsN.end(), mapping);
        if (it != mappingsN.end()) {
            mappingsDrawingInfo.erase(mapping);
            if (std::get_if<MappingCsvApi>(&mapping.usableData)) {
                auto itApi = std::find_if(mappingsToApiWidgets.begin(), mappingsToApiWidgets.end(), [&](const MappingDestinationToApi& m) {
                    return m.getId() == mapping.uniqueData.destination;
                });
                if (itApi != mappingsToApiWidgets.end()) {
                    itApi->setAttribute("API");
                    itApi->setExample("NONE");
                    mappingsToApiWidgets.erase(itApi);
                }
            } else if (auto* mToDb = std::get_if<MappingCsvToDb>(&mapping.usableData)) {
                // NOT THE RIGHT SPOT FOR THIS: find api mapping if it exists to remove the fields from the widget
                // auto it = std::find_if(mappingsToApiWidgets.begin(), mappingsToApiWidgets.end(), [&](MappingDestinationToApi& m) {
                //     if (m.getId() != mapping.uniqueData.source) {
                //         return false;
                //     }
                //     const std::vector<MappingSource>& fields = m.getFields();
                //     auto itApiField = std::find_if(
                //         fields.begin(), fields.end(), [&](const MappingSource& s) { return s.getAttribute() == mToDb->source; });

                //     return itApiField != fields.end();
                // });

                // if (it != mappingsToApiWidgets.end()) {
                //     it->removeFields();
                // }
            }
            mappingsN.erase(it);
        }
    }

    bool drawMapping(const MappingNumber& mapping, ImDrawList* drawlist, MappingDrawing& mappingDrawingInfo) {
        if (!sourceAnchors.anchors.count(mapping.uniqueData.source) || !destAnchors.anchors.count(mapping.uniqueData.destination)) {
            return false;
        }
        ImVec2 start = sourceAnchors.anchors.at(mapping.uniqueData.source);
        ImVec2 end = destAnchors.anchors.at(mapping.uniqueData.destination);

        Widgets::MouseEventType event = isMouseOnLine(start, end, mappingDrawingInfo.width * 2);
        const float thickness = event != Widgets::MouseEventType::NONE ? 6.0f : 2.0f;
        const ImU32 color = event != Widgets::MouseEventType::NONE ? Widgets::colSelected.first : Widgets::colWhiteSemiOpaque;
        drawlist->AddLine(start, end, color, thickness);
        mappingDrawingInfo.width = thickness;
        return event == Widgets::MouseEventType::CLICK;
    }

    bool hasMappings() {
        return std::count_if(mappingsN.begin(), mappingsN.end(), [&](const MappingNumber& m) {
                   return std::holds_alternative<MappingCsvToDb>(m.usableData);
               }) > 1;
    }

    void commitMappings() { reader.setMappingsToDb(mappingsN); }

    void setDefaultPath(const std::filesystem::path& path) {
        std::string s = path.string();
        if (s.size() > csvBuffer.max_size()) {
            logger.pushLog(Log{std::format("ERROR: Path {} is too long.", s)});
            return;
        }
        std::copy(s.begin(), s.end(), csvBuffer.begin());
        csvBuffer[s.size()] = 0x0;
    }
};

class BomVisualizer : public CsvVisualizerImpl<ChangeGeneratorFromBom> {
  public:
    BomVisualizer(DbService& cDbService, ChangeGeneratorFromBom& cReader, PartApi& cApi, Logger& cLogger, UI::DataStates& cDataStates)
        : CsvVisualizerImpl(cDbService, cReader, cApi, cLogger, cDataStates) {}
};

class OrderVisualizer : public CsvVisualizerImpl<ChangeGeneratorFromOrder> {
  public:
    OrderVisualizer(DbService& cDbService, ChangeGeneratorFromOrder& cReader, PartApi& cApi, Logger& cLogger, UI::DataStates& cDataStates)
        : CsvVisualizerImpl(cDbService, cReader, cApi, cLogger, cDataStates) {}
};

} // namespace AutoInv