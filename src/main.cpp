#include <expected>
#include <iostream>

#include "include/app.hpp"
#include "include/changeTracker.hpp"
#include "include/config.hpp"
#include "include/dbInterface.hpp"
#include "include/dbService.hpp"
#include "include/logger.hpp"
#include "include/threadPool.hpp"
#include "include/timing.hpp"

int main() {
    Logger logger;
    Config config{logger};

    ThreadPool pool{5, logger};

    DbInterface dbStorage{logger};
    DbService dbService{dbStorage, pool, logger};

    ChangeTracker changeTracker{dbService, logger};
    App app{dbService, changeTracker, config, logger};

    app.run();
    return 0;
}