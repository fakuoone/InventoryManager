#pragma once

#include "autoInv.hpp"
#include "dataTypes.hpp"
#include "partApi.hpp"
#include "userInterface/mappingWidgets.hpp"
#include "userInterface/widgets.hpp"

#include "config.hpp"

namespace AutoInv {

Widgets::MouseEventType isMouseOnLine(const ImVec2& p1, const ImVec2& p2, const float thickness);

struct WidgetAnchors {
    MappingIdType largestId = 0;
    std::unordered_map<MappingIdType, ImVec2> anchors;
};

class CsvMappingVisualizer {
  protected:
    DbService& dbService_;
    PartApi& api_;
    Config& config_;
    Logger& logger_;
    std::shared_ptr<const CompleteDbData> dbData_;
    UI::DataStates& dataStates_;

    std::vector<std::string> headers_;
    std::vector<DB::TypeCategory> headerTypes_;

    std::vector<std::string> firstRow_;
    std::vector<MappingDestinationDb> dbHeaderWidgets_;
    std::vector<MappingSource> csvHeaderWidgets_;

    std::vector<MappingNumber> mappingsN_;
    std::atomic<bool> mappingsLoaded; // not a very good solution ?

    std::mutex mtxInit_;
    std::condition_variable cvInit_;
    bool csvDataVisualized_;

    std::vector<MappingDestinationToApi> mappingsToApiWidgets_;
    std::unordered_map<MappingIdType, UI::ApiPreviewState> apiPreviewCache_;

    WidgetAnchors sourceAnchors_;
    WidgetAnchors destAnchors_;
    std::unordered_map<MappingNumber, MappingDrawing, MappingHash> mappingsDrawingInfo_;

    std::array<char, 256> csvBuffer_;

    CsvMappingVisualizer(DbService& cDbService, PartApi& cApi, Config& cConfig, Logger& cLogger, UI::DataStates& cDataStates)
        : dbService_(cDbService), api_(cApi), config_(cConfig), logger_(cLogger), dataStates_(cDataStates) {}

    virtual void createMappingToDb(const SourceDetail& source, const DbDestinationDetail& dest) = 0;
    virtual void createMappingToApi(const SourceDetail& source, ApiDestinationDetail& dest) = 0;

  public:
    virtual ~CsvMappingVisualizer() = default;
    virtual void run() = 0;
    virtual bool hasMapping(MappingIdType dest, const std::optional<SourceDetail>& source = std::nullopt) = 0;
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

    const std::vector<MappingNumber>& getMappings() const;
};

template <typename Reader> class CsvVisualizerImpl : public CsvMappingVisualizer {
  private:
    static constexpr float LEFT_MIN = 200.0f;
    static constexpr float CENTER_MIN = 400.0f;
    static constexpr float RIGHT_MIN = 200.0f;

    std::vector<AutoInv::MappingDestinationToApi>::iterator addApiWidget() {
        const std::size_t newIndex = ++destAnchors_.largestId;
        apiPreviewCache_.insert_or_assign(newIndex, UI::ApiPreviewState{});
        mappingsToApiWidgets_.emplace_back(MappingDestinationToApi(
            ApiDestinationDetail(true, newIndex, "NONE", "API", DB::TypeCategory::ANY), &apiPreviewCache_.at(newIndex), true));
        return std::prev(mappingsToApiWidgets_.end());
    }

    void drawApiWidgets(const float width, ImVec2 popupStartup) {
        for (auto& mapping : mappingsToApiWidgets_) {
            mapping.draw(width);
        }
    }

    void drawHead() {
        bool enterPressed = ImGui::InputText("##edit", csvBuffer_.data(), UI::BUFFER_SIZE, ImGuiInputTextFlags_EnterReturnsTrue);
        if (enterPressed || ImGui::IsItemDeactivatedAfterEdit()) {
            try {
                reader.read(std::filesystem::path(std::string(csvBuffer_.data())));
            } catch (const std::exception& e) { logger_.pushLog(Log{std::format("ERROR reading order: {}", e.what())}); }
        }

        // add api intermediate stage
        ImGui::SameLine();
        const char* btnLabel = "ADD API STAGE";
        if (ImGui::Button(btnLabel)) { addApiWidget(); };

        float buttonWidth = ImGui::CalcTextSize("Commit Mapping").x + ImGui::GetStyle().FramePadding.x * 2.0f;
        float rightEdge = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;

        ImGui::SameLine();
        ImGui::SetCursorPosX(rightEdge - buttonWidth);

        ImGui::BeginDisabled(!hasMappings());
        if (ImGui::Button("Commit Mapping")) { commitMappings(); }
        ImGui::EndDisabled();
    }

    void drawMappingSourceRawCSV(const float width) {
        for (auto& csvHeaderWidget : csvHeaderWidgets_) {
            csvHeaderWidget.draw(width);
        }
    }

    void createMappingToDb(const SourceDetail& source, const DbDestinationDetail& dest) override {
        if (hasMapping(dest.id)) { return; }
        MappingCsvToDb newMappingS =
            MappingCsvToDb(PreciseMapLocation(source.primaryField, source.apiSelector), PreciseMapLocation(dest.table, dest.header.name));
        SourceType sourceType = source.apiSelector.empty() ? SourceType::CSV : SourceType::API; // not very clean, but gets the job done
        MappingNumber newMappingN = MappingNumber{MappingNumberInternal{source.id, dest.id}, std::move(newMappingS), sourceType};
        mappingsDrawingInfo_.insert_or_assign(newMappingN, MappingDrawing());
        mappingsN_.emplace_back(std::move(newMappingN));
        // mappingsSToDb.emplace_back(std::move(newMappingS));
    }

    void createMappingToApi(const SourceDetail& source, ApiDestinationDetail& dest) override {
        if (hasMapping(dest.id)) { return; }
        dest.example = source.example;
        dest.attribute = source.primaryField;
        MappingCsvApi newMappingS = MappingCsvApi(source.primaryField, dest.id);
        MappingNumber newMappingN = MappingNumber{MappingNumberInternal{source.id, dest.id}, std::move(newMappingS), SourceType::API};
        mappingsDrawingInfo_.insert_or_assign(newMappingN, MappingDrawing());
        mappingsN_.emplace_back(std::move(newMappingN));
        // mappingsSToApi.emplace_back(std::move(newMappingS));
    }

    void removeMappingToDb(const MappingNumber& mapping) override {
        // not very efficient, but mappingnumber is small
        while (!mappingsLoaded.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        };
        auto it = std::find(mappingsN_.begin(), mappingsN_.end(), mapping);
        if (it != mappingsN_.end()) {
            mappingsDrawingInfo_.erase(mapping);
            if (std::get_if<MappingCsvApi>(&mapping.usableData)) {
                auto itApi = std::find_if(mappingsToApiWidgets_.begin(),
                                          mappingsToApiWidgets_.end(),
                                          [&](const MappingDestinationToApi& m) { return m.getId() == mapping.uniqueData.destination; });
                if (itApi != mappingsToApiWidgets_.end()) {
                    itApi->setAttribute("API");
                    itApi->setExample("NONE");
                    itApi->eraseSourcesFromParent();
                    mappingsToApiWidgets_.erase(itApi);
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
            mappingsN_.erase(it);
        }
    }

  protected:
    Reader& reader;

    CsvVisualizerImpl(DbService& cDbService, Reader& cReader, PartApi& cApi, Config& cConfig, Logger& cLogger, UI::DataStates& cDataStates)
        : CsvMappingVisualizer(cDbService, cApi, cConfig, cLogger, cDataStates), reader(cReader) {
        MappingSource::setInteractionHandler(static_cast<CsvMappingVisualizer*>(this));
        MappingDestinationDb::setInteractionHandler(static_cast<CsvMappingVisualizer*>(this));
    }

  public:
    void run() override {
        drawHead();

        if (dataStates_.dbData != UI::DataState::DATA_READY) { return; }
        ImGui::BeginChild("READER");

        checkNewData();

        if (reader.dataValid(false) && mappingsLoaded.load(std::memory_order_acquire)) {
            const float SPACING = ImGui::GetStyle().ItemSpacing.x * 10;

            ImVec2 avail = ImGui::GetContentRegionAvail();
            float totalMin = LEFT_MIN + CENTER_MIN + RIGHT_MIN + SPACING * 2;
            float extra = std::max(0.0f, avail.x - totalMin);

            // Keep middle column centered, distribute remaining space amongst all three
            float leftWidth = LEFT_MIN + extra * 0.3f;
            float centerWidth = CENTER_MIN + extra * 0.4f;
            float rightWidth = RIGHT_MIN + extra * 0.3f;

            // adapt layout if no api
            if (mappingsToApiWidgets_.size() == 0) {
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
            for (auto& headerWidget : dbHeaderWidgets_) {
                headerWidget.draw(rightWidth);
            }
            ImGui::EndChild();

            // clipping rect
            ImVec2 clipMin(begin.x, begin.y);
            ImVec2 clipMax(begin.x + avail.x, begin.y + avail.y);

            // mappings
            const MappingNumber* toRemove = nullptr;

            ImDrawList* drawlist = ImGui::GetForegroundDrawList();
            drawlist->PushClipRect(clipMin, clipMax, true);
            for (const MappingNumber& mapping : mappingsN_) {
                if (drawMapping(mapping, drawlist, mappingsDrawingInfo_.at(mapping))) { toRemove = &mapping; }
            }
            drawlist->PopClipRect();

            if (toRemove) { removeMappingToDb(*toRemove); }
        }
        ImGui::EndChild();
    }

    void checkNewData() {
        if (reader.dataValid(true)) {
            headers_ = reader.getHeader();
            headerTypes_ = reader.getHeaderTypes();
            firstRow_ = reader.getFirstRow();
            for (const MappingSource& source : csvHeaderWidgets_) {
                source.eraseFromParent();
            }
            csvHeaderWidgets_.clear();
            for (std::size_t i = 0; i < headers_.size(); ++i) {
                csvHeaderWidgets_.emplace_back(headers_[i], std::string{}, firstRow_[i], headerTypes_[i]);
            }
            {
                std::lock_guard<std::mutex> lockInit(mtxInit_);
                csvDataVisualized_ = true;
            }
            cvInit_.notify_all();
        }
    }

    bool hasMapping(MappingIdType dest, const std::optional<SourceDetail>& source = std::nullopt) override {
        if (source.has_value()) {
            return std::find_if(mappingsN_.begin(), mappingsN_.end(), [&](const MappingNumber& m) {
                       return m.uniqueData == MappingNumberInternal(source->id, dest);
                   }) != mappingsN_.end();
        }

        return std::find_if(mappingsN_.begin(), mappingsN_.end(), [&](const MappingNumber& m) {
                   return m.uniqueData.destination == dest;
               }) != mappingsN_.end();
    }

    void removeMappingToDbFromSource(const MappingIdType sourceId) override {
        auto it =
            std::find_if(mappingsN_.begin(), mappingsN_.end(), [&](const MappingNumber& m) { return m.uniqueData.source == sourceId; });
        if (it == mappingsN_.end()) { return; }

        removeMappingToDb(*it);
    }

    bool drawMapping(const MappingNumber& mapping, ImDrawList* drawlist, MappingDrawing& mappingDrawingInfo) {
        if (!sourceAnchors_.anchors.count(mapping.uniqueData.source) || !destAnchors_.anchors.count(mapping.uniqueData.destination)) {
            return false;
        }
        ImVec2 start = sourceAnchors_.anchors.at(mapping.uniqueData.source);
        ImVec2 end = destAnchors_.anchors.at(mapping.uniqueData.destination);

        Widgets::MouseEventType event = isMouseOnLine(start, end, mappingDrawingInfo.width * 2);
        const float thickness = event != Widgets::MouseEventType::NONE ? 6.0f : 2.0f;
        const ImU32 color = event != Widgets::MouseEventType::NONE ? Widgets::colSelected.first : Widgets::colWhiteSemiOpaque;
        drawlist->AddLine(start, end, color, thickness);
        mappingDrawingInfo.width = thickness;
        return event == Widgets::MouseEventType::CLICK;
    }

    bool hasMappings() {
        return std::count_if(mappingsN_.begin(), mappingsN_.end(), [&](const MappingNumber& m) {
                   return std::holds_alternative<MappingCsvToDb>(m.usableData);
               }) > 1;
    }

    void commitMappings() { reader.setMappingsToDb(mappingsN_); }

    void injectMappings(const std::vector<SerializableMapping>& serializedMappings) {
        // has to run first, will fail if mappings already exist
        // while this runs, mapping display has to be suspended or locked. Interaction has to be disabled
        {
            std::mutex& readerMtx = reader.getMutexRead();
            std::condition_variable& readerCv = reader.getCvRead();
            std::unique_lock<std::mutex> lockReader(readerMtx);
            readerCv.wait(lockReader, [this] { return reader.dataValid(false); });
        }
        {
            std::unique_lock<std::mutex> lockInit(mtxInit_);
            cvInit_.wait(lockInit, [this] { return csvDataVisualized_; });
        }
        for (const auto& mapping : serializedMappings) {
            std::visit(
                [&](const auto& concreteMapping) {
                    using T = std::decay_t<decltype(concreteMapping)>;
                    if constexpr (std::is_same_v<T, MappingCsvToDb>) {
                        // 1. find source-indexes from MappingCsvToDb (2 * PreciseMapLocation)
                        // 2. find destination-indexes from MappingCsvToDb
                        // create Mapping
                        std::vector<DbDestinationDetail>::const_iterator itHeader;
                        auto itDestination =
                            std::find_if(dbHeaderWidgets_.begin(), dbHeaderWidgets_.end(), [&](const MappingDestinationDb& d) {
                                const std::vector<DbDestinationDetail>& headers = d.getHeaders();
                                itHeader = std::find_if(headers.begin(), headers.end(), [&](const DbDestinationDetail& detail) {
                                    return detail.table == concreteMapping.destination.outerIdentifier &&
                                           detail.header.name == concreteMapping.destination.innerIdentifier;
                                });
                                return itHeader != headers.end();
                            });

                        if (itDestination == dbHeaderWidgets_.end()) { return; }

                        if (concreteMapping.source.innerIdentifier.empty()) {
                            // from csv to db
                            auto itSource = std::find_if(csvHeaderWidgets_.begin(), csvHeaderWidgets_.end(), [&](const MappingSource& m) {
                                const SourceDetail& sourceData = m.getData();
                                return sourceData.primaryField == concreteMapping.source.outerIdentifier &&
                                       sourceData.apiSelector == concreteMapping.source.innerIdentifier;
                            });
                            if (itSource != csvHeaderWidgets_.end()) { createMappingToDb(itSource->getData(), *itHeader); }

                        } else {
                            // from api to db
                            auto itSource = std::find_if(
                                mappingsToApiWidgets_.begin(), mappingsToApiWidgets_.end(), [&](const MappingDestinationToApi& m) {
                                    return m.getSource() == concreteMapping.source.outerIdentifier;
                                });

                            if (itSource != mappingsToApiWidgets_.end()) {
                                const MappingSource& added = itSource->addField(MappingSource{concreteMapping.source.outerIdentifier,
                                                                                              concreteMapping.source.innerIdentifier,
                                                                                              "example",
                                                                                              DB::TypeCategory::TEXT});
                                createMappingToDb(added.getData(), *itHeader);
                            }
                        }
                    } else if constexpr (std::is_same_v<T, MappingCsvApi>) {
                        // 1. create widget if it doesnt exist
                        // 2. create mapping from csv to this widget
                        // 2.1 find source-indexes from MappingCsvApi (source string is enough to identify the csv col)
                        // 2.2 find destination-indexes from MappingCsvApi
                        auto it = std::find_if(mappingsToApiWidgets_.begin(),
                                               mappingsToApiWidgets_.end(),
                                               [&](const MappingDestinationToApi& m) { return m.getSource() == concreteMapping.source; });
                        if (it == mappingsToApiWidgets_.end()) { it = addApiWidget(); }

                        auto itSource = std::find_if(csvHeaderWidgets_.begin(), csvHeaderWidgets_.end(), [&](const MappingSource& m) {
                            const SourceDetail& sourceData = m.getData();
                            return sourceData.primaryField == concreteMapping.source;
                        });

                        if (itSource != csvHeaderWidgets_.end()) { createMappingToApi(itSource->getData(), it->getOrSetData()); }
                    }
                },
                mapping.usableData);
        }
        mappingsLoaded.store(true, std::memory_order_release);
    }

    void setDefaultPath(const std::filesystem::path& path) {
        std::string s = path.string();
        if (s.size() > csvBuffer_.max_size()) {
            logger_.pushLog(Log{std::format("ERROR: Path {} is too long.", s)});
            return;
        }
        std::copy(s.begin(), s.end(), csvBuffer_.begin());
        csvBuffer_[s.size()] = 0x0;
    }
};

class BomVisualizer : public CsvVisualizerImpl<ChangeGeneratorFromBom> {
  public:
    BomVisualizer(DbService& cDbService,
                  ChangeGeneratorFromBom& cReader,
                  PartApi& cApi,
                  Config& cConfig,
                  Logger& cLogger,
                  UI::DataStates& cDataStates)
        : CsvVisualizerImpl(cDbService, cReader, cApi, cConfig, cLogger, cDataStates) {}
};

class OrderVisualizer : public CsvVisualizerImpl<ChangeGeneratorFromOrder> {
  public:
    OrderVisualizer(DbService& cDbService,
                    ChangeGeneratorFromOrder& cReader,
                    PartApi& cApi,
                    Config& cConfig,
                    Logger& cLogger,
                    UI::DataStates& cDataStates)
        : CsvVisualizerImpl(cDbService, cReader, cApi, cConfig, cLogger, cDataStates) {}
};

} // namespace AutoInv