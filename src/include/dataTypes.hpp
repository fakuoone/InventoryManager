#pragma once

#include <algorithm>
#include <charconv>
#include <condition_variable>

#include <nlohmann/json.hpp>

namespace UI {
enum class DataState { INIT, DATA_OUTDATED, WAITING_FOR_DATA, DATA_READY };

struct DataStates {
    DataState dbData{DataState::INIT};
    DataState changeData{DataState::INIT};
};

struct ApiPreviewState {
    bool loading = false;
    bool ready = false;
    nlohmann::json fields;
};
} // namespace UI

namespace DB {
enum class DataType { INT16, INT32, INT64, FLOAT, DOUBLE, BOOL, STRING, TEXT, JSON, UNKNOWN };
enum class TypeCategory { INTEGER, FLOATING, BOOLEAN, TEXT, JSON, ANY, OTHER };

template <typename T> struct ProtectedData {
    T data;
    std::mutex mtx;
    std::condition_variable cv;
    bool ready{false};
};

inline DataType toDbType(const std::string& pgTypeRaw) {
    std::string pgType = pgTypeRaw;
    std::transform(pgType.begin(), pgType.end(), pgType.begin(), [](unsigned char c) { return std::tolower(c); });
    if (pgType == "smallint") { return DataType::INT16; }
    if (pgType == "integer" || pgType == "int") { return DataType::INT32; }
    if (pgType == "bigint") { return DataType::INT64; }
    if (pgType == "real") { return DataType::FLOAT; }
    if (pgType == "double precision") { return DataType::DOUBLE; }
    if (pgType == "boolean") { return DataType::BOOL; }
    if (pgType == "text") { return DataType::TEXT; }
    if (pgType.starts_with("character varying") || pgType.starts_with("varchar") || pgType.starts_with("character")) {
        return DataType::STRING;
    }
    if (pgType == "json" || pgType == "jsonb") { return DataType::JSON; }
    return DataType::UNKNOWN;
}

inline TypeCategory getCategory(DataType type) {
    switch (type) {
    case DataType::INT16:
    case DataType::INT32:
    case DataType::INT64:
        return TypeCategory::INTEGER;
    case DataType::FLOAT:
    case DataType::DOUBLE:
        return TypeCategory::FLOATING;
    case DataType::BOOL:
        return TypeCategory::BOOLEAN;
    case DataType::STRING:
    case DataType::TEXT:
        return TypeCategory::TEXT;
    case DataType::JSON:
        return TypeCategory::JSON;
    default:
        return TypeCategory::OTHER;
    }
}

enum class HeaderTypes { PRIMARY_KEY, FOREIGN_KEY, UNIQUE_KEY, DATA };
} // namespace DB

namespace CSV {
struct Data {
    std::vector<std::vector<std::string>> rows;
    std::vector<DB::TypeCategory> columnTypes;
};

inline bool isInteger(const std::string& s) {
    if (s.empty()) { return false; }

    size_t i = 0;
    if (s[0] == '+' || s[0] == '-') {
        if (s.size() == 1) { return false; }
        i = 1;
    }

    for (; i < s.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(s[i]))) { return false; }
    }

    return true;
}

inline bool isFloating(const std::string& s) {
    if (s.empty()) { return false; }

    bool seenDot = false;
    bool seenDigit = false;
    size_t i = 0;

    if (s[0] == '+' || s[0] == '-') {
        if (s.size() == 1) { return false; }
        i = 1;
    }

    for (; i < s.size(); ++i) {
        char c = s[i];
        if (std::isdigit(static_cast<unsigned char>(c))) {
            seenDigit = true;
            continue;
        }

        if (c == '.') {
            if (seenDot) { return false; }
            seenDot = true;
            continue;
        }

        if (c == 'e' || c == 'E') {
            if (!seenDigit) return false;
            ++i;
            if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
            if (i == s.size()) return false;
            for (; i < s.size(); ++i) {
                if (!std::isdigit(static_cast<unsigned char>(s[i]))) return false;
            }
            return true;
        }
        return false;
    }

    return seenDigit && seenDot;
}

inline bool isBoolean(std::string s) {
    for (auto& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return (s == "true" || s == "false");
}

inline bool looksLikeJson(const std::string& s) {
    if (s.size() < 2) { return false; }
    char first = s.front();
    char last = s.back();
    return (first == '{' && last == '}') || (first == '[' && last == ']');
}

inline DB::TypeCategory detectTypeCategory(const std::string& value) {
    if (value.empty()) return DB::TypeCategory::OTHER;
    if (isBoolean(value)) return DB::TypeCategory::BOOLEAN;
    if (isInteger(value)) return DB::TypeCategory::INTEGER;
    if (isFloating(value)) return DB::TypeCategory::FLOATING;
    if (looksLikeJson(value)) return DB::TypeCategory::JSON;

    return DB::TypeCategory::TEXT;
}

inline DB::TypeCategory widenType(DB::TypeCategory a, DB::TypeCategory b) {
    // TODO: Handle all relevant cases
    if (a == DB::TypeCategory::OTHER) { return b; }
    if (b == DB::TypeCategory::OTHER) { return a; }
    if (a == b) { return a; }
    if (a == DB::TypeCategory::TEXT || b == DB::TypeCategory::TEXT) { return DB::TypeCategory::TEXT; }
    if (a == DB::TypeCategory::FLOATING || b == DB::TypeCategory::FLOATING) { return DB::TypeCategory::FLOATING; }
    return DB::TypeCategory::TEXT;
}

inline std::vector<DB::TypeCategory> determineTypes(const std::vector<std::vector<std::string>>& rows) {
    if (rows.empty()) { return std::vector<DB::TypeCategory>{}; }
    std::vector<DB::TypeCategory> resultTypes(rows.front().size(), DB::TypeCategory::OTHER);
    for (std::size_t i = 0; i < rows.size(); i++) {
        if (i == 0) { continue; }
        for (std::size_t j = 0; j < rows[i].size(); j++) {
            resultTypes[j] = widenType(detectTypeCategory(rows[i][j]), resultTypes[j]);
        }
    }
    return resultTypes;
}
} // namespace CSV
