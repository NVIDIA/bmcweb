#pragma once

#include "app.hpp"
#include "query.hpp"
#include "registries/privilege_registry.hpp"
#include "utils/chassis_utils.hpp"

#include <boost/format.hpp>
#include <boost/url/format.hpp>
#include <utils/dbus_utils.hpp>
#include <utils/environment_util.hpp>
#include <utils/json_utils.hpp>
#include <utils/processor_utils.hpp>

#include <memory>
#include <optional>
#include <string>
#include <tuple>
namespace redfish
{

// Map of service name to list of interfaces
using MapperServiceMap =
    std::vector<std::pair<std::string, std::vector<std::string>>>;

inline void
    getfanSpeedsPercent(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                        const std::string& chassisID)
{
    BMCWEB_LOG_DEBUG("Get properties for getFan associated to chassis = {}",
                     chassisID);
    const std::array<std::string, 1> sensorInterfaces = {
        "xyz.openbmc_project.Sensor.Value"};
    crow::connections::systemBus->async_method_call(
        [asyncResp, chassisID](
            const boost::system::error_code ec,
            const std::vector<std::pair<
                std::string,
                std::vector<std::pair<std::string, std::vector<std::string>>>>>&
                sensorsubtree) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG("D-Bus response error on GetSubTree {}", ec);
            if (ec.value() == boost::system::errc::io_error)
            {
                messages::resourceNotFound(asyncResp->res, "Chassis",
                                           chassisID);
                return;
            }
            messages::internalError(asyncResp->res);
            return;
        }
        nlohmann::json& fanList = asyncResp->res.jsonValue["FanSpeedsPercent"];
        fanList = nlohmann::json::array();

        for (const auto& [objectPath, serviceName] : sensorsubtree)
        {
            if (objectPath.empty() || serviceName.size() != 1)
            {
                BMCWEB_LOG_DEBUG("Error getting D-Bus object!");
                messages::internalError(asyncResp->res);
                return;
            }
            const std::string& validPath = objectPath;
            const std::string& connectionName = serviceName[0].first;
            std::vector<std::string> split;
            // Reserve space for
            // /xyz/openbmc_project/sensors/<name>/<subname>
            split.reserve(6);
            boost::algorithm::split(split, validPath, boost::is_any_of("/"));
            if (split.size() < 6)
            {
                BMCWEB_LOG_ERROR("Got path that isn't long enough {}",
                                 validPath);
                continue;
            }
            // These indexes aren't intuitive, as boost::split puts an empty
            // string at the beginning
            const std::string& sensorType = split[4];
            const std::string& sensorName = split[5];
            BMCWEB_LOG_DEBUG("sensorName {} sensorType {}", sensorName,
                             sensorType);
            if (sensorType == "fan" || sensorType == "fan_tach" ||
                sensorType == "fan_pwm")
            {
                // if the sensor does not belong to the same chassis will
                // discard it
                dbus::utility::findAssociations(
                    validPath + "/chassis",
                    [asyncResp, chassisID, &fanList, sensorName, validPath,
                     connectionName](
                        const boost::system::error_code ec,
                        std::variant<std::vector<std::string>>& association) {
                    if (ec)
                    {
                        BMCWEB_LOG_ERROR("{} : {}", validPath, ec.message());
                        return;
                    }
                    std::vector<std::string>* data =
                        std::get_if<std::vector<std::string>>(&association);
                    if (data == nullptr || data->empty())
                    {
                        BMCWEB_LOG_ERROR("{} : No chassis association found",
                                         validPath);
                        return;
                    }
                    std::filesystem::path chassisPath(data->front());
                    std::string sensorChassisID = chassisPath.filename();
                    if (sensorChassisID == chassisID)
                    {
                        crow::connections::systemBus->async_method_call(
                            [asyncResp, chassisID, &fanList,
                             sensorName](const boost::system::error_code ec,
                                         const std::variant<double>& value) {
                            if (ec)
                            {
                                BMCWEB_LOG_DEBUG("Can't get Fan speed!");
                                messages::internalError(asyncResp->res);
                                return;
                            }

                            const double* attributeValue =
                                std::get_if<double>(&value);
                            if (attributeValue == nullptr)
                            {
                                // illegal property
                                messages::internalError(asyncResp->res);
                                return;
                            }
                            std::string tempPath = "/redfish/v1/Chassis/" +
                                                   chassisID + "/Sensors/";
                            fanList.push_back(
                                {{"DeviceName", "Chassis Fan #" + sensorName},
                                 {"SpeedRPM", *attributeValue},
                                 {"DataSourceUri", tempPath + sensorName},
                                 {"@odata.id", tempPath + sensorName}});
                        },
                            connectionName, validPath,
                            "org.freedesktop.DBus.Properties", "Get",
                            "xyz.openbmc_project.Sensor.Value", "Value");
                    }
                });
            }
            else
            {
                BMCWEB_LOG_DEBUG(
                    "This is not a fan-related sensor,sensortype = {}",
                    sensorType);
                continue;
            }
        }
    },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/xyz/openbmc_project/sensors", 0, sensorInterfaces);
}

inline void requestRoutesProcessorEnvironmentMetricsClearOOBSetPoint(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/Processors/<str>/"
        "EnvironmentMetrics/Actions/Oem/NvidiaEnvironmentMetrics.ClearOOBSetPoint")

        .privileges({{"ConfigureComponents"}})
        .methods(boost::beast::http::verb::post)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName,
                   const std::string& processorId) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        const std::array<const char*, 2> interfaces = {
            "xyz.openbmc_project.Inventory.Item.Cpu",
            "xyz.openbmc_project.Inventory.Item.Accelerator"};

        crow::connections::systemBus->async_method_call(
            [asyncResp,
             processorId](const boost::system::error_code ec,
                          const crow::openbmc_mapper::GetSubTreeType& subtree) {
            if (ec)
            {
                messages::internalError(asyncResp->res);
                return;
            }

            // Iterate over all retrieved ObjectPaths.
            for (const std::pair<std::string,
                                 std::vector<std::pair<
                                     std::string, std::vector<std::string>>>>&
                     object : subtree)
            {
                const std::string& path = object.first;
                const std::vector<
                    std::pair<std::string, std::vector<std::string>>>&
                    connectionNames = object.second;

                sdbusplus::message::object_path objPath(path);
                if (objPath.filename() != processorId)
                {
                    continue;
                }

                if (connectionNames.size() < 1)
                {
                    BMCWEB_LOG_ERROR("Got 0 Connection names");
                    continue;
                }

                const std::string& connectionName = connectionNames[0].first;
                const std::vector<std::string>& interfaces =
                    connectionNames[0].second;

                if (std::find(interfaces.begin(), interfaces.end(),
                              "com.nvidia.Common.ClearPowerCap") !=
                    interfaces.end())
                {
                    redfish::chassis_utils::resetPowerLimit(asyncResp, objPath,
                                                            connectionName);
                }

                return;
            }

            messages::resourceNotFound(
                asyncResp->res, "#Processor.v1_20_0.Processor", processorId);
        },
            "xyz.openbmc_project.ObjectMapper",
            "/xyz/openbmc_project/object_mapper",
            "xyz.openbmc_project.ObjectMapper", "GetSubTree",
            "/xyz/openbmc_project/inventory", 0, interfaces);
    });
}

inline void requestRoutesChassisEnvironmentMetricsClearOOBSetPoint(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/EnvironmentMetrics/Actions/Oem"
                      "/NvidiaEnvironmentMetrics.ClearOOBSetPoint/")
        .privileges({{"ConfigureComponents"}})
        .methods(boost::beast::http::verb::post)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& chassisId) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }

        const std::array<const char*, 1> interfaces = {
            "xyz.openbmc_project.Inventory.Item.Chassis"};

        crow::connections::systemBus->async_method_call(
            [asyncResp,
             chassisId](const boost::system::error_code ec,
                        const crow::openbmc_mapper::GetSubTreeType& subtree) {
            if (ec)
            {
                messages::internalError(asyncResp->res);
                return;
            }

            // Iterate over all retrieved ObjectPaths.
            for (const std::pair<std::string,
                                 std::vector<std::pair<
                                     std::string, std::vector<std::string>>>>&
                     object : subtree)
            {
                const std::string& path = object.first;
                const std::vector<
                    std::pair<std::string, std::vector<std::string>>>&
                    connectionNames = object.second;

                sdbusplus::message::object_path objPath(path);
                if (objPath.filename() != chassisId)
                {
                    continue;
                }

                if (connectionNames.size() < 1)
                {
                    BMCWEB_LOG_ERROR("Got 0 Connection names");
                    continue;
                }

                const std::string& connectionName = connectionNames[0].first;
                crow::connections::systemBus->async_method_call(
                    [asyncResp, connectionName,
                     chassisId](const boost::system::error_code& e,
                                std::variant<std::vector<std::string>>& resp) {
                    if (e)
                    {
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    std::vector<std::string>* data =
                        std::get_if<std::vector<std::string>>(&resp);
                    if (data == nullptr)
                    {
                        return;
                    }
                    for (const std::string& ctrlPath : *data)
                    {
                        redfish::chassis_utils::resetPowerLimit(
                            asyncResp, ctrlPath, connectionName);
                    }
                },
                    "xyz.openbmc_project.ObjectMapper",
                    path + "/power_controls", "org.freedesktop.DBus.Properties",
                    "Get", "xyz.openbmc_project.Association", "endpoints");
                return;
            }
            messages::resourceNotFound(asyncResp->res,
                                       "#Chassis.v1_15_0.Chassis", chassisId);
        },
            "xyz.openbmc_project.ObjectMapper",
            "/xyz/openbmc_project/object_mapper",
            "xyz.openbmc_project.ObjectMapper", "GetSubTree",
            "/xyz/openbmc_project/inventory", 0, interfaces);
    });
}

inline void handleEnvironmentMetricsHead(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    auto respHandler = [asyncResp, chassisId](
                           const std::optional<std::string>& validChassisPath) {
        if (!validChassisPath)
        {
            messages::resourceNotFound(asyncResp->res, "Chassis", chassisId);
            return;
        }

        asyncResp->res.addHeader(
            boost::beast::http::field::link,
            "</redfish/v1/JsonSchemas/EnvironmentMetrics/EnvironmentMetrics.json>; rel=describedby");
    };

    redfish::chassis_utils::getValidChassisPath(asyncResp, chassisId,
                                                std::move(respHandler));
}

inline void handleEnvironmentMetricsGet(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    auto respHandler = [asyncResp, chassisId](
                           const std::optional<std::string>& validChassisPath) {
        if (!validChassisPath)
        {
            messages::resourceNotFound(asyncResp->res, "Chassis", chassisId);
            return;
        }

        asyncResp->res.addHeader(
            boost::beast::http::field::link,
            "</redfish/v1/JsonSchemas/EnvironmentMetrics/EnvironmentMetrics.json>; rel=describedby");
        asyncResp->res.jsonValue["@odata.type"] =
            "#EnvironmentMetrics.v1_3_0.EnvironmentMetrics";
        asyncResp->res.jsonValue["Name"] = "Chassis Environment Metrics";
        asyncResp->res.jsonValue["Id"] = "EnvironmentMetrics";
        asyncResp->res.jsonValue["@odata.id"] = boost::urls::format(
            "/redfish/v1/Chassis/{}/EnvironmentMetrics", chassisId);
if constexpr(BMCWEB_NVIDIA_OEM_PROPERTIES){
        asyncResp->res.jsonValue["Oem"]["Nvidia"]["@odata.type"] =
            "#NvidiaEnvironmentMetrics.v1_2_0.NvidiaEnvironmentMetrics";
}
        getfanSpeedsPercent(asyncResp, chassisId);
        redfish::nvidia_env_utils::getPowerWattsEnergyJoules(
            asyncResp, chassisId, *validChassisPath);

        // TODO: Remove interfaces from here
        const std::array<const char*, 2> interfaces = {
            "xyz.openbmc_project.Inventory.Item.Board",
            "xyz.openbmc_project.Inventory.Item.Chassis"};
        redfish::nvidia_env_utils::getPowerAndControlData(asyncResp, chassisId,
                                                          interfaces);
    };

    redfish::chassis_utils::getValidChassisPath(asyncResp, chassisId,
                                                std::move(respHandler));
}

inline void requestRoutesEnvironmentMetrics(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/EnvironmentMetrics/")
        .privileges(redfish::privileges::headEnvironmentMetrics)
        .methods(boost::beast::http::verb::head)(
            std::bind_front(handleEnvironmentMetricsHead, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/EnvironmentMetrics/")
        .privileges(redfish::privileges::getEnvironmentMetrics)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleEnvironmentMetricsGet, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/EnvironmentMetrics/")
        .privileges(redfish::privileges::patchChassis)
        .methods(boost::beast::http::verb::patch)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& chassisId) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        std::optional<nlohmann::json> powerLimit;
        std::optional<nlohmann::json> oem;
        // Read json request
        if (!json_util::readJsonAction(req, asyncResp->res, "PowerLimitWatts",
                                       powerLimit, "Oem", oem))
        {
            return;
        }
        // Update power limit
        if (powerLimit)
        {
            std::optional<int> setPoint;
            if (json_util::readJson(*powerLimit, asyncResp->res, "SetPoint",
                                    setPoint))
            {
                const std::array<const char*, 1> interfaces = {
                    "xyz.openbmc_project.Inventory.Item.Chassis"};

                crow::connections::systemBus->async_method_call(
                    [asyncResp, chassisId, setPoint](
                        const boost::system::error_code ec,
                        const crow::openbmc_mapper::GetSubTreeType& subtree) {
                    if (ec)
                    {
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    // Iterate over all retrieved ObjectPaths.
                    for (const std::pair<
                             std::string,
                             std::vector<std::pair<std::string,
                                                   std::vector<std::string>>>>&
                             object : subtree)
                    {
                        const std::string& path = object.first;
                        const std::vector<
                            std::pair<std::string, std::vector<std::string>>>&
                            connectionNames = object.second;

                        sdbusplus::message::object_path objPath(path);
                        if (objPath.filename() != chassisId)
                        {
                            continue;
                        }

                        if (connectionNames.size() < 1)
                        {
                            BMCWEB_LOG_ERROR("Got 0 Connection names");
                            continue;
                        }

                        const std::string& connectionName =
                            connectionNames[0].first;
                        crow::connections::systemBus->async_method_call(
                            [asyncResp, connectionName, chassisId, setPoint](
                                const boost::system::error_code& e,
                                std::variant<std::vector<std::string>>& resp) {
                            if (e)
                            {
                                messages::internalError(asyncResp->res);
                                return;
                            }
                            std::vector<std::string>* data =
                                std::get_if<std::vector<std::string>>(&resp);
                            if (data == nullptr)
                            {
                                return;
                            }
                            for (const std::string& ctrlPath : *data)
                            {
                                std::string resourceType = "Chassis";
                                redfish::nvidia_env_utils::patchPowerLimit(
                                    asyncResp, chassisId, *setPoint, ctrlPath,
                                    resourceType);
                            }
                        },
                            "xyz.openbmc_project.ObjectMapper",
                            path + "/power_controls",
                            "org.freedesktop.DBus.Properties", "Get",
                            "xyz.openbmc_project.Association", "endpoints");

                        return;
                    }

                    messages::resourceNotFound(
                        asyncResp->res, "#Chassis.v1_15_0.Chassis", chassisId);
                },
                    "xyz.openbmc_project.ObjectMapper",
                    "/xyz/openbmc_project/object_mapper",
                    "xyz.openbmc_project.ObjectMapper", "GetSubTree",
                    "/xyz/openbmc_project/inventory", 0, interfaces);
            }
        }
if constexpr(BMCWEB_NVIDIA_OEM_PROPERTIES){

        if (oem)
        {
            std::optional<nlohmann::json> nvidiaObj;
            redfish::json_util::readJson(*oem, asyncResp->res, "Nvidia",
                                         nvidiaObj);
            if (nvidiaObj)
            {
                std::optional<std::string> powerMode;
                redfish::json_util::readJson(*nvidiaObj, asyncResp->res,
                                             "PowerMode", powerMode);
                if (powerMode)
                {
                    messages::propertyNotWritable(asyncResp->res, "PowerMode");
                }
            }
        }
}
    });
}

inline void requestRoutesProcessorEnvironmentMetrics(App& app)
{
    BMCWEB_ROUTE(
        app, "/redfish/v1/Systems/<str>/Processors/<str>/EnvironmentMetrics")
        .privileges(redfish::privileges::getProcessor)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName,
                   const std::string& processorId) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        std::string envMetricsURI =
            "/redfish/v1/Systems/" +
            std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) + "/Processors/";
        envMetricsURI += processorId;
        envMetricsURI += "/EnvironmentMetrics";
        asyncResp->res.jsonValue["@odata.type"] =
            "#EnvironmentMetrics.v1_3_0.EnvironmentMetrics";
        asyncResp->res.jsonValue["@odata.id"] = envMetricsURI;
        asyncResp->res.jsonValue["Id"] = "Environment Metrics";
        asyncResp->res.jsonValue["Name"] = processorId + " Environment Metrics";

        redfish::nvidia_env_utils::getProcessorEnvironmentMetricsData(
            asyncResp, processorId);
    });

    BMCWEB_ROUTE(
        app, "/redfish/v1/Systems/<str>/Processors/<str>/EnvironmentMetrics")
        .privileges(redfish::privileges::patchProcessor)
        .methods(boost::beast::http::verb::patch)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName,
                   const std::string& processorId) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        std::optional<nlohmann::json> powerLimit;
        std::optional<nlohmann::json> oemObject;
        std::optional<bool> powerLimitPersistency;

        // Read json request
        if (!json_util::readJsonAction(req, asyncResp->res, "PowerLimitWatts",
                                       powerLimit, "Oem", oemObject))
        {
            return;
        }

if constexpr(BMCWEB_NVIDIA_OEM_PROPERTIES){

        // update Edpp Setpoint
        if (std::optional<nlohmann::json> oemNvidiaObject;
            oemObject && json_util::readJson(*oemObject, asyncResp->res,
                                             "Nvidia", oemNvidiaObject))
        {
            if (std::optional<nlohmann::json> EdppObject;
                oemNvidiaObject &&
                json_util::readJson(
                    *oemNvidiaObject, asyncResp->res, "EDPpPercent", EdppObject,
                    "PowerLimitPersistency", powerLimitPersistency))
            {
                if (EdppObject)
                {
                    std::optional<size_t> setPoint;
                    std::optional<bool> persistency;

                    if (!json_util::readJson(*EdppObject, asyncResp->res,
                                             "SetPoint", setPoint,
                                             "Persistency", persistency))
                    {
                        BMCWEB_LOG_ERROR("Cannot read values from Edpp tag");
                        return;
                    }

                    if (setPoint && persistency)
                    {
                        redfish::processor_utils::getProcessorObject(
                            asyncResp, processorId,
                            [setPoint, persistency](
                                const std::shared_ptr<bmcweb::AsyncResp>&
                                    asyncResp,
                                const std::string& processorId,
                                const std::string& objectPath,
                                const MapperServiceMap& serviceMap,
                                [[maybe_unused]] const std::string&
                                    deviceType) {
                            redfish::nvidia_env_utils::patchEdppSetPoint(
                                asyncResp, processorId, *setPoint, *persistency,
                                objectPath, serviceMap);
                        });
                    }
                    else if (setPoint)
                    {
                        redfish::processor_utils::getProcessorObject(
                            asyncResp, processorId,
                            [setPoint](const std::shared_ptr<bmcweb::AsyncResp>&
                                           asyncResp,
                                       const std::string& processorId,
                                       const std::string& objectPath,
                                       const MapperServiceMap& serviceMap,
                                       [[maybe_unused]] const std::string&
                                           deviceType) {
                            redfish::nvidia_env_utils::patchEdppSetPoint(
                                asyncResp, processorId, *setPoint, false,
                                objectPath, serviceMap);
                        });
                    }
                }
            }
        }

}

        // Update power limit
        if (powerLimit)
        {
            std::optional<int> setPoint;
            if (json_util::readJson(*powerLimit, asyncResp->res, "SetPoint",
                                    setPoint))
            {
                const std::array<const char*, 2> interfaces = {
                    "xyz.openbmc_project.Inventory.Item.Cpu",
                    "xyz.openbmc_project.Inventory.Item.Accelerator"};

                bool persistency = false;
                if (powerLimitPersistency)
                {
                    persistency = *powerLimitPersistency;
                }

                crow::connections::systemBus->async_method_call(
                    [asyncResp, processorId, setPoint, persistency](
                        const boost::system::error_code ec,
                        const crow::openbmc_mapper::GetSubTreeType& subtree) {
                    if (ec)
                    {
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    // Iterate over all retrieved ObjectPaths.
                    for (const std::pair<
                             std::string,
                             std::vector<std::pair<std::string,
                                                   std::vector<std::string>>>>&
                             object : subtree)
                    {
                        const std::string& path = object.first;
                        const std::vector<
                            std::pair<std::string, std::vector<std::string>>>&
                            connectionNames = object.second;

                        sdbusplus::message::object_path objPath(path);
                        if (objPath.filename() != processorId)
                        {
                            continue;
                        }

                        if (connectionNames.size() < 1)
                        {
                            BMCWEB_LOG_ERROR("Got 0 Connection names");
                            continue;
                        }

                        const std::vector<std::string>& interfaces =
                            connectionNames[0].second;

                        if (std::find(
                                interfaces.begin(), interfaces.end(),
                                "xyz.openbmc_project.Control.Power.Cap") !=
                            interfaces.end())
                        {
                            std::string resourceType = "Processors";
                            redfish::nvidia_env_utils::patchPowerLimit(
                                asyncResp, processorId, *setPoint, objPath,
                                resourceType, persistency);
                        }

                        return;
                    }

                    messages::resourceNotFound(asyncResp->res,
                                               "#Processor.v1_20_0.Processor",
                                               processorId);
                },
                    "xyz.openbmc_project.ObjectMapper",
                    "/xyz/openbmc_project/object_mapper",
                    "xyz.openbmc_project.ObjectMapper", "GetSubTree",
                    "/xyz/openbmc_project/inventory", 0, interfaces);
            }
        }
    });
}

inline void requestRoutesMemoryEnvironmentMetrics(App& app)
{
    BMCWEB_ROUTE(app,
                 "/redfish/v1/Systems/<str>/Memory/<str>/EnvironmentMetrics")
        .privileges({{"Login"}})
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName,
                   const std::string& dimmId) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        std::string envMetricsURI =
            "/redfish/v1/Systems/" +
            std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) + "/Memory/";
        envMetricsURI += dimmId;
        envMetricsURI += "/EnvironmentMetrics";
        asyncResp->res.jsonValue["@odata.type"] =
            "#EnvironmentMetrics.v1_2_0.EnvironmentMetrics";
        asyncResp->res.jsonValue["@odata.id"] = envMetricsURI;
        asyncResp->res.jsonValue["Id"] = "EnvironmentMetrics";
        asyncResp->res.jsonValue["Name"] = dimmId + " Environment Metrics";
        redfish::nvidia_env_utils::getMemoryEnvironmentMetricsData(asyncResp,
                                                                   dimmId);
    });
}

inline void requestRoutesEdppReset(App& app)
{
    /**
     * Functions triggers appropriate requests on DBus
     */
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/" + std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
            "/Processors/<str>/"
            "EnvironmentMetrics/Actions/Oem/NvidiaEnvironmentMetrics.ResetEDPp")
        .privileges({{"Login"}})
        .methods(boost::beast::http::verb::post)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& processorId) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        redfish::processor_utils::getProcessorObject(
            asyncResp, processorId,
            [](const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
               const std::string& processorId, const std::string& objectPath,
               const MapperServiceMap& serviceMap,
               [[maybe_unused]] const std::string& deviceType) {
            redfish::nvidia_env_utils::postEdppReset(asyncResp, processorId,
                                                     objectPath, serviceMap);
        });
    });
}

} // namespace redfish
