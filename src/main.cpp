#include "changeTracker.hpp"
#include "config.hpp"
#include "dbInterface.hpp"
#include "dbService.hpp"
#include "logger.hpp"
#include "threadPool.hpp"
#include "timing.hpp"
#include "userInterface/app.hpp"

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