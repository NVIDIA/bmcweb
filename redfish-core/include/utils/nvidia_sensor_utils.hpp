#pragma once
#include <utils/chassis_utils.hpp>
#include <utils/nvidia_chassis_util.hpp>

#include "async_resp.hpp"

namespace redfish
{
namespace nvidia_sensor_utils
{

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

inline void
    defaultSystemURI(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    nlohmann::json& itemsArray = asyncResp->res.jsonValue["RelatedItem"];
    itemsArray.push_back(
        {{"@odata.id", std::format("/redfish/v1/Systems/{}",
                                   BMCWEB_REDFISH_SYSTEM_URI_NAME)}});
}

inline void
    handleChassisRedfishURL(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const sdbusplus::message::object_path& chassisPath)
{
    if (!boost::ends_with(chassisPath.str, BMCWEB_REDFISH_MANAGER_URI_NAME))
    {
        defaultSystemURI(asyncResp);
        return;
    }

    if (!(strlen(PLATFORMDEVICEPREFIX) > 0) ||
        !(boost::starts_with(chassisPath.filename(), PLATFORMDEVICEPREFIX)))
    {
        defaultSystemURI(asyncResp);
        return;
    }

    chassis_utils::getRedfishURL(
        chassisPath.str,
        [asyncResp](const bool& status, const std::string& url) {
        nlohmann::json& itemsArray = asyncResp->res.jsonValue["RelatedItem"];
        if (!status)
        {
            defaultSystemURI(asyncResp);
            return;
        }
        itemsArray.push_back({nlohmann::json::array({"@odata.id", url})});
    });
}

inline void getRelatedNetworkAdapterData(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Sensor get related network adapter item");

    // Check chassis link
    crow::connections::systemBus->async_method_call(
        [asyncResp, objPath](const boost::system::error_code ec,
                             std::variant<std::vector<std::string>>& resp) {
            if (ec)
            {
                // If chassis link fails, fallback to default system URI
            defaultSystemURI(asyncResp);
                return;
            }
            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&resp);
            if (data == nullptr)
            {
            defaultSystemURI(asyncResp);
                return;
            }
            for (const std::string& chassisPath : *data)
            {
                sdbusplus::message::object_path objectPath(chassisPath);
                const std::string chassisId = objectPath.filename();

                // Now check the network adapter link
                crow::connections::systemBus->async_method_call(
                [asyncResp, chassisId,
                 objectPath](const boost::system::error_code ec1,
                                std::variant<std::vector<std::string>>& resp1) {
                        if (ec1)
                        {
                    // If network adapter call fails,
                    // ensure to pick up the resource from Chassis interface
                    redfish::nvidia_chassis_utils::getChassisRelatedItem(
                        asyncResp, objectPath, chassisId,
                        handleChassisRedfishURL);
                            return;
                        }
                        std::vector<std::string>* data1 =
                            std::get_if<std::vector<std::string>>(&resp1);
                        if (data1 == nullptr)
                        {
                            return;
                        }
                        nlohmann::json& itemsArray1 =
                            asyncResp->res.jsonValue["RelatedItem"];
                        for (const std::string& adapterPath : *data1)
                        {
                            sdbusplus::message::object_path objectPath1(
                                adapterPath);
                            std::string adapterId = objectPath1.filename();
                            std::string adapterURI = std::format(
                                "/redfish/v1/Chassis/{}/NetworkAdapters/{}",
                                chassisId, adapterId);
                            itemsArray1.push_back({{"@odata.id", adapterURI}});
                        }
                    },
                    "xyz.openbmc_project.ObjectMapper",
                    objPath + "/network_adapter",
                    "org.freedesktop.DBus.Properties", "Get",
                    "xyz.openbmc_project.Association", "endpoints");
            }
        },
        "xyz.openbmc_project.ObjectMapper", objPath + "/chassis",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}
} // namespace nvidia_sensor_utils
} // namespace redfish
