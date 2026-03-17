#pragma once

#include <future>

#include "dataTypes.hpp"
#include "threadPool.hpp"

class DbFilter {
  private:
    Logger& logger_;
    ThreadPool& pool_;

    std::shared_ptr<const CompleteDbData> dbData_;
    UI::DataStates& dataStates_;

    const CompleteDbData filterByKeyword(const std::string keyword) {
        if (dataStates_.dbData != UI::DataState::DATA_READY) { return CompleteDbData{}; }
    }

  public:
    DbFilter(ThreadPool& cThreadPool, Logger& cLogger, UI::DataStates& cDataStates)
        : pool_(cThreadPool), logger_(cLogger), dataStates_(cDataStates) {}

    void setData(std::shared_ptr<const CompleteDbData> newData) { dbData_ = newData; }
};
