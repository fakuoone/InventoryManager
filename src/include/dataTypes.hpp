#pragma once

#include <algorithm>
#include <charconv>
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
enum class DataType { INT16, INT32, INT64, FLOAT, DOUBLE, NUMERIC, BOOL, STRING, TEXT, UUID, JSON, TIMESTAMP, DATE, BINARY, UNKNOWN };
enum class TypeCategory { INTEGER, FLOATING, NUMERIC, BOOLEAN, TEXT, DATETIME, BINARY, JSON, ANY, OTHER };

inline DataType toDbType(const std::string& pgTypeRaw) {
    std::string pgType = pgTypeRaw;
    std::transform(pgType.begin(), pgType.end(), pgType.begin(), [](unsigned char c) { return std::tolower(c); });
    if (pgType == "smallint") {
        return DataType::INT16;
    }
    if (pgType == "integer" || pgType == "int") {
        return DataType::INT32;
    }
    if (pgType == "bigint") {
        return DataType::INT64;
    }
    if (pgType == "real") {
        return DataType::FLOAT;
    }
    if (pgType == "double precision") {
        return DataType::DOUBLE;
    }
    if (pgType.starts_with("numeric") || pgType.starts_with("decimal")) {
        return DataType::NUMERIC;
    }
    if (pgType == "boolean") {
        return DataType::BOOL;
    }
    if (pgType == "text") {
        return DataType::TEXT;
    }
    if (pgType.starts_with("character varying") || pgType.starts_with("varchar") || pgType.starts_with("character")) {
        return DataType::STRING;
    }
    if (pgType == "uuid") {
        return DataType::UUID;
    }
    if (pgType.starts_with("timestamp")) {
        return DataType::TIMESTAMP;
    }
    if (pgType == "date") {
        return DataType::DATE;
    }
    if (pgType == "json" || pgType == "jsonb") {
        return DataType::JSON;
    }
    if (pgType == "bytea") {
        return DataType::BINARY;
    }
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
    case DataType::NUMERIC:
        return TypeCategory::NUMERIC;
    case DataType::BOOL:
        return TypeCategory::BOOLEAN;
    case DataType::STRING:
    case DataType::TEXT:
    case DataType::UUID:
        return TypeCategory::TEXT;
    case DataType::TIMESTAMP:
    case DataType::DATE:
        return TypeCategory::DATETIME;
    case DataType::BINARY:
        return TypeCategory::BINARY;
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

inline DB::TypeCategory widenType(DB::TypeCategory a, DB::TypeCategory b) {
    // TODO: Handle all relevant cases
    if (a == b) {
        return a;
    }
    if (a == DB::TypeCategory::TEXT || b == DB::TypeCategory::TEXT) {
        return DB::TypeCategory::TEXT;
    }
    return DB::TypeCategory::TEXT;
}

inline DB::TypeCategory detectCsvCategory(const std::string& value) {
    if (value.empty()) {
        return DB::TypeCategory::OTHER;
    }
    if (value == "true" || value == "false" || value == "TRUE" || value == "FALSE") {
        return DB::TypeCategory::BOOLEAN;
    }
    {
        std::int64_t i;
        auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), i);
        if (ec == std::errc() && ptr == value.data() + value.size()) {
            return DB::TypeCategory::NUMERIC;
        }
    }
    {
        double d;
        auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), d);
        if (ec == std::errc() && ptr == value.data() + value.size()) {
            return DB::TypeCategory::NUMERIC;
        }
    }
    return DB::TypeCategory::TEXT;
}

inline std::vector<DB::TypeCategory> determineTypes(const std::vector<std::vector<std::string>>& rows) {
    if (rows.empty()) {
        return std::vector<DB::TypeCategory>{};
    }
    std::vector<DB::TypeCategory> resultTypes(rows.front().size(), DB::TypeCategory::NUMERIC);
    for (std::size_t i = 0; i < rows.size(); i++) {
        if (i == 0) {
            continue;
        }
        for (std::size_t j = 0; j < rows[i].size(); j++) {
            resultTypes[j] = widenType(detectCsvCategory(rows[i][j]), resultTypes[j]);
        }
    }
    return resultTypes;
}
} // namespace CSV
