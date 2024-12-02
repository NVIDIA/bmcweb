#pragma once

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
                nlohmann::json& itemsArray =
                    asyncResp->res.jsonValue["RelatedItem"];
                // If chassis link fails, fallback to default system URI
                itemsArray.push_back(
                    {{"@odata.id",
                      std::format("/redfish/v1/Systems/{}",
                                  BMCWEB_REDFISH_SYSTEM_URI_NAME)}});
                return;
            }
            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&resp);
            if (data == nullptr)
            {
                return;
            }
            for (const std::string& chassisPath : *data)
            {
                sdbusplus::message::object_path objectPath(chassisPath);
                const std::string chassisId = objectPath.filename();

                // Now check the network adapter link
                crow::connections::systemBus->async_method_call(
                    [asyncResp,
                     chassisId](const boost::system::error_code ec1,
                                std::variant<std::vector<std::string>>& resp1) {
                        if (ec1)
                        {
                            // If network adapter call fails, fallback to
                            // default system URI
                            asyncResp->res.jsonValue["RelatedItem"].push_back(
                                {{"@odata.id",
                                  std::format(
                                      "/redfish/v1/Systems/{}",
                                      BMCWEB_REDFISH_SYSTEM_URI_NAME)}});
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
