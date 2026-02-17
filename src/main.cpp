#include "autoInv.hpp"
#include "changeTracker.hpp"
#include "config.hpp"
#include "dbInterface.hpp"
#include "dbService.hpp"
#include "logger.hpp"
#include "partApi.hpp"
#include "threadPool.hpp"
#include "timing.hpp"
#include "userInterface/app.hpp"

int main() {
    PartApi::initGlobalCurl();

    Logger logger;
    Change::setLogger(logger);
    Config config{logger};

    ThreadPool pool{5, logger};

    DbInterface dbInterface{logger};
    DbService dbService{dbInterface, pool, config, logger};

    ChangeTracker changeTracker{dbService, logger};

    PartApi api{pool, config, logger};

    AutoInv::ChangeGeneratorFromBom bomReader{pool, changeTracker, dbService, api, config, logger};
    AutoInv::ChangeGeneratorFromOrder orderReader{pool, changeTracker, dbService, api, config, logger};

    App app{config, pool, dbService, changeTracker, api, bomReader, orderReader, logger};

    app.run();

    PartApi::cleanupGlobalCurl();
    return 0;
}