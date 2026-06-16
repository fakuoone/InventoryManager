#pragma once

#include <filesystem>
#include <nlohmann/json.hpp>
#include <unordered_set>

#include "change.hpp"
#include "config.hpp"

class AutoGenInfo {
  private:
    static inline Logger* logger_ = nullptr;
    static inline Config* config_ = nullptr;
    static inline std::filesystem::path csvPath_;
    static inline uint16_t changesTotal_;
    static inline uint16_t addedChanges_;
    static inline std::unordered_set<std::size_t> unexecutedChangeKeys_;

    static void changeExecuted(std::size_t key) {
        if (unexecutedChangeKeys_.contains(key)) { unexecutedChangeKeys_.erase(key); }
    }

  public:
    static void setConfig(Config& c) { config_ = &c; }
    static void setLogger(Logger& l) { logger_ = &l; }

    static void setCsvSource(std::filesystem::path csv) {
        if (!csvPath_.empty()) { return; }
        csvPath_ = std::move(csv);
    }

    static void changeAdded(std::size_t key, bool added) {
        if (added) {
            unexecutedChangeKeys_.insert(key);
            addedChanges_++;
        }
        changesTotal_++;
    }

    static void finish(const Change::chHashV& successfulChanges) {
        if (changesTotal_ == 0 || csvPath_.empty()) { return; }
        // parse archive
        nlohmann::ordered_json archiveJson;
        std::filesystem::path path = config_->getAutoInvArchivePath();

        {
            // open archive
            std::ifstream archive(path);
            if (!archive.is_open()) {
                logger_->pushLog(Log{std::format("ERROR: Could not open archive on path: {}", path.string())});
                return;
            }

            try {
                if (archive.peek() == std::ifstream::traits_type::eof()) {
                    archiveJson = nlohmann::json::array();
                } else {
                    archive >> archiveJson;
                }
                if (!archiveJson.is_array() && !archiveJson.empty()) {
                    logger_->pushLog(Log{std::format("ERROR: Archive is not of type array or empty.")});
                    return;
                }
            } catch (const nlohmann::json::parse_error& e) {
                logger_->pushLog(Log{std::format("ERROR: Could not parse {}", e.what())});
                return;
            }

            // remove all changes that got executed
            for (std::size_t key : successfulChanges) {
                changeExecuted(key);
            }
        }

        // add
        auto now = std::chrono::system_clock::now();
        std::string timestamp = std::format("{:%Y-%m-%d %H:%M:%S}", std::chrono::floor<std::chrono::seconds>(now)); // TODO: timezone?
        nlohmann::ordered_json self = {{"timestamp", timestamp},
                                       {"path", csvPath_},
                                       {"addedChangeCount", addedChanges_},
                                       {"totalChangeCount", changesTotal_},
                                       {"remainingChangeCount", unexecutedChangeKeys_.size()}};

        archiveJson.push_back(self);

        // Write
        std::ofstream archiveWrite(path);
        if (!archiveWrite.is_open()) {
            logger_->pushLog(Log{std::format("ERROR: Could not open archive on path: {}", path.string())});
            return;
        }
        logger_->pushLog(Log{archiveJson.dump()});
        archiveWrite << archiveJson.dump();

        changesTotal_ = 0;
        addedChanges_ = 0;
    }
};