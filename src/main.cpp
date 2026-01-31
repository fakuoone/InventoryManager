#include "autoInv.hpp"
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
    Change::setLogger(logger);
    Config config{logger};

    ThreadPool pool{5, logger};

    DbInterface dbInterface{logger};
    DbService dbService{dbInterface, pool, config, logger};

    ChangeTracker changeTracker{dbService, logger};

    AutoInv::BomReader bomReader{pool, changeTracker, config, logger};
    AutoInv::OrderReader orderReader{pool, changeTracker, config, logger};

    App app{dbService, changeTracker, config, bomReader, orderReader, logger};

    app.run();
    return 0;
}