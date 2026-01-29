#include "changeTracker.hpp"
#include "config.hpp"
#include "dbInterface.hpp"
#include "dbService.hpp"
#include "logger.hpp"
#include "threadPool.hpp"
#include "timing.hpp"
#include "userInterface/app.hpp"
#include "bom/bomReader.hpp"

int main() {
    Logger logger;
    Change::setLogger(logger);
    Config config{logger};

    ThreadPool pool{5, logger};

    DbInterface dbInterface{logger};
    DbService dbService{dbInterface, pool, config, logger};

    ChangeTracker changeTracker{dbService, logger};

    BomReader bomReader{pool, changeTracker, config, logger};

    App app{dbService, changeTracker, config, bomReader, logger};

    app.run();
    return 0;
}