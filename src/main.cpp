#include <expected>
#include <iostream>

#include "app.hpp"
#include "include/changeTracker.hpp"
#include "include/config.hpp"
#include "include/dbInterface.hpp"
#include "include/dbService.hpp"
#include "include/logger.hpp"
#include "include/threadPool.hpp"
#include "include/timing.hpp"

int main() {
    Config config;

    Logger logger;
    DbInterface dbStorage{config, logger};
    ThreadPool pool{5, logger};  // 2 worker threads

    DbService dbService{dbStorage, pool, logger};

    ChangeTracker changeTracker{dbService, logger};

    App app{dbService, changeTracker, logger};

    app.run();
    return 0;
}