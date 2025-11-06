#pragma once
#include "bmcweb_config.h"

#include "async_resp.hpp"
#include "dbus_singleton.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"

#include <boost/url/format.hpp>
#include <sdbusplus/message/native_types.hpp>
#include <utils/chassis_utils.hpp>
#include <utils/nvidia_chassis_util.hpp>

#include <array>
#include <functional>
#include <variant>
#include <vector>
namespace redfish
{
namespace nvidia_sensor_utils
{

inline void defaultSystemURI(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    nlohmann::json& itemsArray = asyncResp->res.jsonValue["RelatedItem"];
    itemsArray.push_back(
        {{"@odata.id", std::format("/redfish/v1/Systems/{}",
                                   BMCWEB_REDFISH_SYSTEM_URI_NAME)}});
}

inline void handleChassisRedfishURL(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const sdbusplus::message::object_path& chassisPath)
{
    chassis_utils::getRedfishURL(
        chassisPath.str,
        [asyncResp](const bool& status, const std::string& url) {
            nlohmann::json& itemsArray =
                asyncResp->res.jsonValue["RelatedItem"];
            if (!status)
            {
                defaultSystemURI(asyncResp);
                return;
            }
            itemsArray.push_back({nlohmann::json::array({"@odata.id", url})});
        });
}

inline void populateRelatedNetworkAdapterData(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& objPath)
{
    // get parent of the network adapter to construct the URI
    dbus::utility::findAssociations(
        objPath + "/parent_chassis",
        [asyncResp, objPath](const boost::system::error_code ec,
                             const std::vector<std::string>& resp) {
            if (ec)
            {
                // default
                defaultSystemURI(asyncResp);
                return;
            }

            nlohmann::json& itemsArray =
                asyncResp->res.jsonValue["RelatedItem"];
            for (const std::string& chassisPath : resp)
            {
                sdbusplus::message::object_path adapterPath(objPath);
                std::string adapterId = adapterPath.filename();
                sdbusplus::message::object_path objectPath(chassisPath);
                std::string chassisId = objectPath.filename();
                std::string adapterURI =
                    std::format("/redfish/v1/Chassis/{}/NetworkAdapters/{}",
                                chassisId, adapterId);
                itemsArray.push_back({{"@odata.id", adapterURI}});
            }
        });
}

inline void getRelatedNetworkAdapterData(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Sensor get related network adapter item");

    // Check chassis link
    dbus::utility::getProperty<std::vector<std::string>>(
        "xyz.openbmc_project.ObjectMapper", objPath + "/chassis",
        "xyz.openbmc_project.Association", "endpoints",
        [asyncResp, objPath](const boost::system::error_code ec,
                             const std::vector<std::string>& resp) {
            if (ec)
            {
                // If chassis link fails, fallback to default system URI
                defaultSystemURI(asyncResp);
                return;
            }

            for (const std::string& chassisPath : resp)
            {
                sdbusplus::message::object_path objectPath(chassisPath);
                const std::string chassisId = objectPath.filename();

                // Now check the network adapter link
                dbus::utility::getProperty<std::vector<std::string>>(
                    "xyz.openbmc_project.ObjectMapper",
                    objPath + "/network_adapter",
                    "xyz.openbmc_project.Association", "endpoints",
                    [asyncResp, chassisId,
                     objectPath](const boost::system::error_code ec1,
                                 const std::vector<std::string>& resp1) {
                        if (ec1)
                        {
                            // If network adapter call fails,
                            // ensure to pick up the resource from Chassis
                            // interface
                            nvidia_chassis_utils::getChassisRelatedItem(
                                asyncResp, objectPath, chassisId,
                                handleChassisRedfishURL);
                            return;
                        }

                        for (const std::string& adapterPath : resp1)
                        {
                            populateRelatedNetworkAdapterData(asyncResp,
                                                              adapterPath);
                        }
                    });
            }
        });
}

inline void getRelatedItemData(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Sensor get related item");

    // Check fabric switch link
    dbus::utility::getProperty<std::vector<std::string>>(
        "xyz.openbmc_project.ObjectMapper", objPath + "/fabric",
        "xyz.openbmc_project.Association", "endpoints",
        [asyncResp, objPath](const boost::system::error_code& ec,
                             const std::vector<std::string>& resp) {
            if (ec)
            {
                // Check processor link
                dbus::utility::getProperty<std::vector<std::string>>(
                    "xyz.openbmc_project.ObjectMapper", objPath + "/processor",
                    "xyz.openbmc_project.Association", "endpoints",
                    [asyncResp,
                     objPath](const boost::system::error_code& ec1,
                              const std::vector<std::string>& resp1) {
                        nlohmann::json& itemsArray1 =
                            asyncResp->res.jsonValue["RelatedItem"];
                        if (ec1)
                        {
                            // Call to network adapter-related items when
                            // processor check fails
                            nvidia_sensor_utils::getRelatedNetworkAdapterData(
                                asyncResp, objPath);
                            return;
                        }
                        for (const std::string& processorPath : resp1)
                        {
                            sdbusplus::message::object_path objectPath(
                                processorPath);
                            std::string processorId = objectPath.filename();
                            std::string processorURI = std::format(
                                "/redfish/v1/Systems/{}/Processors/{}",
                                BMCWEB_REDFISH_SYSTEM_URI_NAME, processorId);
                            itemsArray1.push_back(
                                {{"@odata.id", processorURI}});
                        }
                    });
                return;
            }
            for (const std::string& fabricPath : resp)
            {
                sdbusplus::message::object_path objectPath(fabricPath);
                const std::string fabricId = objectPath.filename();
                dbus::utility::getProperty<std::vector<std::string>>(
                    "xyz.openbmc_project.ObjectMapper", objPath + "/switch",
                    "xyz.openbmc_project.Association", "endpoints",
                    [asyncResp,
                     fabricId](const boost::system::error_code& ec1,
                               const std::vector<std::string>& resp1) {
                        if (ec1)
                        {
                            return; // no switch = no failures
                        }

                        nlohmann::json& itemsArray1 =
                            asyncResp->res.jsonValue["RelatedItem"];
                        for (const std::string& switchPath : resp1)
                        {
                            sdbusplus::message::object_path objectPath1(
                                switchPath);
                            std::string switchId = objectPath1.filename();
                            std::string switchURI =
                                boost::urls::format(
                                    "/redfish/v1/Fabrics/{}/Switches/{}",
                                    fabricId, switchId)
                                    .buffer();
                            itemsArray1.push_back({{"@odata.id", switchURI}});
                        }
                    });
            }
        });
}

inline const char* toImplementation(const std::string& implementation)
{
    if (implementation ==
        "xyz.openbmc_project.Sensor.Type.ImplementationType.PhysicalSensor")
    {
        return "PhysicalSensor";
    }
    if (implementation ==
        "xyz.openbmc_project.Sensor.Type.ImplementationType.Synthesized")
    {
        return "Synthesized";
    }
    if (implementation ==
        "xyz.openbmc_project.Sensor.Type.ImplementationType.Reported")
    {
        return "Reported";
    }

    return "";
}

inline const char* toReadingBasis(const std::string& readingBasis)
{
    if (readingBasis ==
        "xyz.openbmc_project.Sensor.ReadingBasis.ReadingBasisType.Headroom")
    {
        return "Headroom";
    }

    return "";
}

inline std::string getSensorId(std::string_view sensorName,
                               [[maybe_unused]] std::string_view sensorType)
{
    return std::format("{}", sensorName);
}

inline void setThresholdReadingProperty(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const double readingValue, const std::string& interfaceName,
    const std::string& propertyName, const std::string& serviceName,
    const std::string& objectPath)
{
    sdbusplus::asio::setProperty(
        *crow::connections::systemBus, serviceName, objectPath, interfaceName,
        propertyName, readingValue,
        [asyncResp, serviceName, objectPath, interfaceName,
         propertyName](const boost::system::error_code& ec) {
            if (ec)
            {
                messages::internalError(asyncResp->res);
                return;
            }
            messages::success(asyncResp->res);
        });
}

inline void processSensorThresholdValues(
    const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& serviceName, const std::string& objectPath)
{
    std::optional<nlohmann::json> thresholdsObj;

    if (!json_util::readJsonAction(req, asyncResp->res, "Thresholds",
                                   thresholdsObj))
    {
        return;
    }
    if (thresholdsObj)
    {
        std::optional<nlohmann::json> lowerCritical;
        std::optional<nlohmann::json> upperCritical;
        std::optional<nlohmann::json> upperCaution;
        std::optional<nlohmann::json> lowerCaution;

        if (!redfish::json_util::readJson(
                *thresholdsObj, asyncResp->res, "LowerCritical", lowerCritical,
                "UpperCritical", upperCritical, "UpperCaution", upperCaution,
                "LowerCaution", lowerCaution))
        {
            return;
        }
        if (lowerCritical)
        {
            std::optional<double> readingValue;
            if (redfish::json_util::readJson(*lowerCritical, asyncResp->res,
                                             "Reading", readingValue))
            {
                if (readingValue)
                {
                    nvidia_sensor_utils::setThresholdReadingProperty(
                        asyncResp, *readingValue,
                        "xyz.openbmc_project.Sensor.Threshold.Critical",
                        "CriticalLow", serviceName, objectPath);
                }
            }
        }
        if (upperCritical)
        {
            std::optional<double> readingValue;
            if (redfish::json_util::readJson(*upperCritical, asyncResp->res,
                                             "Reading", readingValue))
            {
                if (readingValue)
                {
                    nvidia_sensor_utils::setThresholdReadingProperty(
                        asyncResp, *readingValue,
                        "xyz.openbmc_project.Sensor.Threshold.Critical",
                        "CriticalHigh", serviceName, objectPath);
                }
            }
        }
        if (upperCaution)
        {
            std::optional<double> readingValue;
            if (redfish::json_util::readJson(*upperCaution, asyncResp->res,
                                             "Reading", readingValue))
            {
                if (readingValue)
                {
                    nvidia_sensor_utils::setThresholdReadingProperty(
                        asyncResp, *readingValue,
                        "xyz.openbmc_project.Sensor.Threshold.Warning",
                        "WarningHigh", serviceName, objectPath);
                }
            }
        }
        if (lowerCaution)
        {
            std::optional<double> readingValue;
            if (redfish::json_util::readJson(*lowerCaution, asyncResp->res,
                                             "Reading", readingValue))
            {
                if (readingValue)
                {
                    nvidia_sensor_utils::setThresholdReadingProperty(
                        asyncResp, *readingValue,
                        "xyz.openbmc_project.Sensor.Threshold.Warning",
                        "WarningLow", serviceName, objectPath);
                }
            }
        }
    }
}

inline void processAsyncSensorThresholdValues(
    const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& serviceName, const std::string& objectPath)
{
    std::optional<double> maxAllowableOperatingValue;
    std::optional<double> minAllowableOperatingValue;
    std::optional<nlohmann::json> thresholdsObj;

    // Read MaxAllowableOperatingValue and MinAllowableOperatingValue
    if (!json_util::readJsonAction(
            req, asyncResp->res, "MaxAllowableOperatingValue",
            maxAllowableOperatingValue, "MinAllowableOperatingValue",
            minAllowableOperatingValue, "Thresholds", thresholdsObj))
    {
        return;
    }

    std::vector<std::tuple<std::string, std::variant<bool, uint32_t, double>>>
        properties;
    std::string propertyName;
    std::string interfaceName;

    if (maxAllowableOperatingValue)
    {
        BMCWEB_LOG_DEBUG("MaxAllowableOperatingValue: {}",
                         *maxAllowableOperatingValue);
        propertyName = "MaxAllowableValue";
        properties.emplace_back(propertyName, *maxAllowableOperatingValue);
        interfaceName = "xyz.openbmc_project.Sensor.Value";
    }

    if (minAllowableOperatingValue)
    {
        BMCWEB_LOG_DEBUG("MinAllowableOperatingValue: {}",
                         *minAllowableOperatingValue);
        propertyName = "MinAllowableValue";
        properties.emplace_back(propertyName, *minAllowableOperatingValue);
        interfaceName = "xyz.openbmc_project.Sensor.Value";
    }

    if (thresholdsObj)
    {
        std::optional<nlohmann::json> lowerCritical;

        if (!redfish::json_util::readJson(*thresholdsObj, asyncResp->res,
                                          "LowerCritical", lowerCritical))
        {
            return;
        }

        if (lowerCritical)
        {
            std::optional<double> readingValue;
            if (redfish::json_util::readJson(*lowerCritical, asyncResp->res,
                                             "Reading", readingValue))
            {
                if (readingValue)
                {
                    BMCWEB_LOG_DEBUG("Thresholds.LowerCritical.Reading: {}",
                                     *readingValue);
                    propertyName = "CriticalLow";
                    properties.emplace_back(propertyName, *readingValue);
                    interfaceName =
                        "xyz.openbmc_project.Sensor.Threshold.Critical";
                }
            }
        }
    }
    if (properties.empty())
    {
        BMCWEB_LOG_ERROR("Patch properties not found");
        messages::propertyMissing(asyncResp->res,
                                  "Threshold patchable properties not found");
        return;
    }

    BMCWEB_LOG_DEBUG("Performing Patch using Set Async Method Call for {}",
                     propertyName);

    nvidia_async_operation_utils::doGenericSetAsyncAndGatherResult(
        asyncResp, std::chrono::seconds(60), serviceName, objectPath,
        interfaceName, propertyName,
        std::variant<std::vector<
            std::tuple<std::string, std::variant<bool, uint32_t, double>>>>(
            properties),
        nvidia_async_operation_utils::PatchThresholdCallback{asyncResp});
}

inline void findSensorServiceAndPathInChassis(
    const std::string& chassisId, const std::string& sensorId,
    std::function<void(const boost::system::error_code& ec, bool chassisFound,
                       std::optional<std::tuple<std::string, std::string,
                                                std::vector<std::string>>>)>&&
        callback)
{
    dbus::utility::getSubTreePaths(
        "/xyz/openbmc_project/inventory", 0,
        std::array<std::string_view, 1>{
            "xyz.openbmc_project.Inventory.Item.Chassis"},
        [chassisId, sensorId, callback = std::move(callback)](
            const boost::system::error_code& ec,
            const dbus::utility::MapperGetSubTreePathsResponse&
                chassisPaths) mutable {
            if (ec)
            {
                callback(ec, false, std::nullopt);
                return;
            }

            bool chassisFound = false;
            for (const std::string& obj : chassisPaths)
            {
                if (obj.ends_with(chassisId))
                {
                    chassisFound = true;
                    break;
                }
            }
            if (!chassisFound)
            {
                callback({}, false, std::nullopt);
                return;
            }

            dbus::utility::getSubTree(
                "/xyz/openbmc_project/sensors", 2,
                std::array<std::string_view, 1>{
                    "xyz.openbmc_project.Sensor.Value"},
                [sensorId, callback = std::move(callback)](
                    const boost::system::error_code& ec2,
                    const dbus::utility::MapperGetSubTreeResponse&
                        subtree) mutable {
                    if (ec2)
                    {
                        callback(ec2, true, std::nullopt);
                        return;
                    }

                    auto sensorIt = std::find_if(
                        subtree.begin(), subtree.end(),
                        [&sensorId](const auto& subtreeObj) {
                            return subtreeObj.first.find(sensorId) !=
                                   std::string::npos;
                        });

                    if (sensorIt != subtree.end() && !sensorIt->second.empty())
                    {
                        const auto& service = sensorIt->second.front();
                        callback({}, true,
                                 std::make_tuple(service.first, sensorIt->first,
                                                 service.second));
                        return;
                    }

                    callback({}, true, std::nullopt);
                });
        });
}

inline void handleSensorGetUsingPath(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& sensorId,
    const std::string& sensorPath,
    const std::function<
        void(const std::shared_ptr<bmcweb::AsyncResp>&, const std::string&,
             const ::dbus::utility::MapperGetObject&)>& handleMapperResponse)
{
    asyncResp->res.jsonValue["@odata.id"] = boost::urls::format(
        "/redfish/v1/Chassis/{}/Sensors/{}", chassisId, sensorId);

    BMCWEB_LOG_DEBUG("Sensor doGet enter");

    constexpr std::array<std::string_view, 1> interfaces = {
        "xyz.openbmc_project.Sensor.Value"};
    ::dbus::utility::getDbusObject(
        sensorPath, interfaces,
        [asyncResp, sensorId, sensorPath, handleMapperResponse](
            const boost::system::error_code& ec,
            const ::dbus::utility::MapperGetObject& subtree) {
            BMCWEB_LOG_DEBUG("respHandler1 enter");
            if (ec == boost::system::errc::io_error)
            {
                BMCWEB_LOG_WARNING("Sensor not found from getSensorPaths");
                messages::resourceNotFound(asyncResp->res, sensorId, "Sensor");
                return;
            }
            if (ec)
            {
                messages::internalError(asyncResp->res);
                BMCWEB_LOG_ERROR(
                    "Sensor getSensorPaths resp_handler: Dbus error {}", ec);
                return;
            }
            handleMapperResponse(asyncResp, sensorPath, subtree);
            BMCWEB_LOG_DEBUG("respHandler1 exit");
        });
}

inline void getChassisSensors(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& chassisPath,
    const std::string& sensorName,
    const std::function<
        void(const std::shared_ptr<bmcweb::AsyncResp>&, const std::string&,
             const ::dbus::utility::MapperGetObject&)>& handleMapperResponse)
{
    // Find the sensor on the chassis
    auto getAllChassisSensors =
        [asyncResp, sensorName, chassisId, handleMapperResponse](
            const boost::system::error_code& ec,
            const std::vector<std::string>& variantEndpoints) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("getAllChassisSensors DBUS error: {}", ec);
                messages::internalError(asyncResp->res);
                return;
            }

            for (const std::string& sensorPath : variantEndpoints)
            {
                sdbusplus::message::object_path path(sensorPath);
                const std::string& sensorId = path.filename();
                if (sensorId.empty())
                {
                    BMCWEB_LOG_ERROR("Failed to find '/' in {}", sensorPath);
                    continue;
                }
                if (sensorId != sensorName)
                {
                    continue;
                }
                /*
                // Get chassis sensors
                const std::shared_ptr<boost::container::flat_set<std::string>>
                    sensorList = std::make_shared<
                        boost::container::flat_set<std::string>>();

                sensorList->emplace(sensorPath);
                processSensorList(asyncResp, sensorList);
                */

                asyncResp->res.jsonValue["Status"]["Health"] = "OK";
                if constexpr (!BMCWEB_DISABLE_HEALTH_ROLLUP)
                {
                    asyncResp->res.jsonValue["Status"]["HealthRollup"] = "OK";
                }
                if constexpr (!BMCWEB_DISABLE_CONDITIONS_ARRAY)
                {
                    asyncResp->res.jsonValue["Status"]["Conditions"] =
                        nlohmann::json::array();
                }

                nvidia_sensor_utils::handleSensorGetUsingPath(
                    asyncResp, chassisId, sensorId, sensorPath,
                    handleMapperResponse);
                // Add related item data
                nvidia_sensor_utils::getRelatedItemData(
                    asyncResp, std::string(sensorPath));
                return;
            }
            messages::resourceNotFound(asyncResp->res, "Sensor", sensorName);
        };
    dbus::utility::getProperty<std::vector<std::string>>(
        "xyz.openbmc_project.ObjectMapper", chassisPath + "/all_sensors",
        "xyz.openbmc_project.Association", "endpoints", getAllChassisSensors);
}

inline void handleSensorGetAfterSetup(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& sensorId,
    const std::function<
        void(const std::shared_ptr<bmcweb::AsyncResp>&, const std::string&,
             const ::dbus::utility::MapperGetObject&)>& handleMapperResponse)
{
    constexpr std::array<std::string_view, 1> chassisInterface = {
        "xyz.openbmc_project.Inventory.Item.Chassis"};

    auto chassisHandler = [asyncResp, chassisId, sensorId,
                           handleMapperResponse](
                              const boost::system::error_code& ec,
                              const std::vector<std::string>& chassisPaths) {
        if (ec)
        {
            BMCWEB_LOG_ERROR("sensors get chassis handler DBUS error: {}", ec);
            messages::internalError(asyncResp->res);
            return;
        }

        for (const std::string& chassisPath : chassisPaths)
        {
            sdbusplus::message::object_path path(chassisPath);
            const std::string& chassisName = path.filename();
            if (chassisName.empty())
            {
                BMCWEB_LOG_ERROR("Failed to find '/' in {}", chassisPath);
                continue;
            }
            if (chassisName != chassisId)
            {
                continue;
            }

            nvidia_sensor_utils::getChassisSensors(
                asyncResp, chassisId, chassisPath, sensorId,
                handleMapperResponse);
            return;
        }
        messages::resourceNotFound(asyncResp->res, "Chassis", chassisId);
    };

    dbus::utility::getSubTreePaths("/xyz/openbmc_project/inventory", 0,
                                   chassisInterface, chassisHandler);
}

inline void handleSensorPatchAfterSetup(
    const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& sensorId)
{
    nvidia_sensor_utils::findSensorServiceAndPathInChassis(
        chassisId, sensorId,
        [&req, asyncResp, sensorId, chassisId](
            const boost::system::error_code& ec, bool chassisFound,
            const std::optional<std::tuple<
                std::string, std::string, std::vector<std::string>>>& svcPath) {
            if (ec)
            {
                messages::internalError(asyncResp->res);
                return;
            }
            if (!chassisFound)
            {
                messages::resourceNotFound(asyncResp->res, "#Chassis",
                                           chassisId);
                return;
            }
            if (!svcPath)
            {
                messages::resourceNotFound(asyncResp->res, "Sensor", sensorId);
                return;
            }

            const auto& [serviceName, objectPath, interfaces] = *svcPath;

            if (std::find(interfaces.begin(), interfaces.end(),
                          "com.nvidia.Async.Set") != interfaces.end())
            {
                nvidia_sensor_utils::processAsyncSensorThresholdValues(
                    req, asyncResp, serviceName, objectPath);
            }
            else
            {
                nvidia_sensor_utils::processSensorThresholdValues(
                    req, asyncResp, serviceName, objectPath);
            }
        });
}

} // namespace nvidia_sensor_utils

} // namespace redfish
