#pragma once

#include <nlohmann/json.hpp>

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
