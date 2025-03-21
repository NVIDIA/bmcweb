#include "webserver_run.hpp"

#include "bmcweb_config.h"

#include "app.hpp"
#include "dbus_monitor.hpp"
#include "dbus_singleton.hpp"
#include "event_service_manager.hpp"
#include "google/google_service_root.hpp"
#include "hostname_monitor.hpp"
#include "ibm/management_console_rest.hpp"
#include "image_upload.hpp"
#include "kvm_websocket.hpp"
#include "logging.hpp"
#include "login_routes.hpp"
#include "nvidia/nvidia_mfg_saving.hpp"
#include "obmc_console.hpp"
#include "openbmc_dbus_rest.hpp"
#include "redfish.hpp"
#include "redfish_aggregator.hpp"
#include "user_monitor.hpp"
#include "vm_websocket.hpp"
#include "webassets.hpp"

#include <boost/asio/io_context.hpp>
#include <dump_offload.hpp>
#include <sdbusplus/asio/connection.hpp>
#include <watchdog.hpp>

#include <memory>

int run()
{
    auto io = std::make_shared<boost::asio::io_context>();
    App app(io);

    sdbusplus::asio::connection systemBus(*io);
    crow::connections::systemBus = &systemBus;

    // Enable SystemD service watchdog kicking. Service file has timeout of 60s.
    crow::watchdog::ServiceWD watchdog(30, io);

    // Static assets need to be initialized before Authorization, because auth
    // needs to build the whitelist from the static routes

    if constexpr (BMCWEB_STATIC_HOSTING)
    {
        crow::webassets::requestRoutes(app);
    }

    if constexpr (BMCWEB_KVM)
    {
        crow::obmc_kvm::requestRoutes(app);
    }

    if constexpr (BMCWEB_REDFISH)
    {
        redfish::RedfishService redfish(app);

        // Create EventServiceManager instance and initialize Config
        redfish::EventServiceManager::getInstance(&*io);

        if constexpr (BMCWEB_REDFISH_AGGREGATION)
        {
            // Create RedfishAggregator instance and initialize Config
            redfish::RedfishAggregator::getInstance(&*io);
        }
    }

    if constexpr (BMCWEB_REST)
    {
        crow::dbus_monitor::requestRoutes(app);
        crow::image_upload::requestRoutes(app);
        crow::openbmc_mapper::requestRoutes(app);
    }

    if constexpr (BMCWEB_HOST_SERIAL_SOCKET)
    {
        crow::obmc_console::requestRoutes(app);
    }

    crow::obmc_vm::requestRoutes(app);

    if constexpr (BMCWEB_IBM_MANAGEMENT_CONSOLE)
    {
        crow::ibm_mc::requestRoutes(app);
    }

    if constexpr (BMCWEB_GOOGLE_API)
    {
        crow::google_api::requestRoutes(app);
    }

    crow::login_routes::requestRoutes(app);

    if constexpr (!BMCWEB_REDFISH_DBUS_LOG)
    {
        int rc = redfish::EventServiceManager::startEventLogMonitor(*io);
        if (rc != 0)
        {
            BMCWEB_LOG_ERROR("Redfish event handler setup failed...");
            return rc;
        }
    }

    if constexpr (!BMCWEB_INSECURE_DISABLE_SSL)
    {
        if (persistent_data::getConfig().isTLSAuthEnabled())
        {
            BMCWEB_LOG_INFO("Start Hostname Monitor Service...");
            crow::hostname_monitor::registerHostnameSignal();
        }
    }

    if constexpr (BMCWEB_REDFISH_DUMP_LOG)
    {
        crow::obmc_dump::requestRoutes(app);
    }

#ifdef BMCWEB_ENABLE_SHMEM_PLATFORM_METRICS
    tal::TelemetryAggregator::namespaceInit(tal::ProcessType::Client);
#endif

    bmcweb::registerUserRemovedSignal();
    if constexpr (BMCWEB_NVIDIA_CBC_MFG_FIX)
    {
        nvidia_mfg_saving::requestRoutes(app);
    }

    app.run();
    io->run();

    crow::connections::systemBus = nullptr;

    return 0;
}
