#pragma once

enum class DataState { INIT, DATA_OUTDATED, WAITING_FOR_DATA, DATA_READY };

struct DataStates {
    DataState dbData{DataState::INIT};
    DataState changeData{DataState::INIT};
};
