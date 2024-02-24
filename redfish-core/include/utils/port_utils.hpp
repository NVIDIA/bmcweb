#pragma once

namespace redfish
{
namespace port_utils
{

const std::string nvlinkToken = "NVLink";
// Get PCIe device link speed generation
/*inline std::string getLinkSpeedGeneration(double currentSpeed, size_t width)
{

    std::string speedGen;

    // validate width
    if ((width == INT_MAX) || (width == 0))
    {
        BMCWEB_LOG_DEBUG << "Invalid width data";
        return speedGen;
    }

    // speed per lane
    auto speed = currentSpeed / static_cast<double>(width);

    // PCIe Link Speed Caluculation
    //https://en.wikipedia.org/wiki/PCI_Express#cite_note-both-directions-51(pciegenx
    //transfer rates) Gen1 Gbps = 2.5 * (8/10) Gen2 Gbps = 5 * (8/10) Gen3 Gbps
=
    //8 * (128/130) Gen4 Gbps = 16 * (128/130) Gen5 Gbps = 32 * (128/130) Gen6
    //Gbps = 64 * (242/256)

    // pcie speeds and generation map
    std::map<double, std::string> pcieSpeedGenMap = {
        {2, "Gen1"},      {4, "Gen2"},      {7.877, "Gen3"},
        {15.754, "Gen4"}, {31.508, "Gen5"}, {60.5, "Gen6"},
    };

    if ((pcieSpeedGenMap.find(speed) != pcieSpeedGenMap.end()))
    {
        speedGen = pcieSpeedGenMap[speed];
    }

    return speedGen;
}*/

// Get PCIe device link speed generation
inline std::string getLinkSpeedGeneration(double speed)
{
    if (speed == 2.5)
    {
        return "Gen1";
    }
    if (speed == 5)
    {
        return "Gen2";
    }
    if (speed == 8)
    {
        return "Gen3";
    }
    if (speed == 16)
    {
        return "Gen4";
    }
    if (speed == 32)
    {
        return "Gen5";
    }
    if (speed == 64)
    {
        return "Gen6";
    }
    // Unknown or others
    return "";
}

inline std::string getLinkStatusType(const std::string& linkStatusType)
{
    if (linkStatusType ==
        "xyz.openbmc_project.Inventory.Item.Port.LinkStatusType.LinkDown")
    {
        return "LinkDown";
    }
    if (linkStatusType ==
        "xyz.openbmc_project.Inventory.Item.Port.LinkStatusType.LinkUp")
    {
        return "LinkUp";
    }
    if (linkStatusType ==
        "xyz.openbmc_project.Inventory.Item.Port.LinkStatusType.NoLink")
    {
        return "NoLink";
    }
    if (linkStatusType ==
        "xyz.openbmc_project.Inventory.Item.Port.LinkStatusType.Starting")
    {
        return "Starting";
    }
    if (linkStatusType ==
        "xyz.openbmc_project.Inventory.Item.Port.LinkStatusType.Training")
    {
        return "Training";
    }
    // Unknown or others
    return "";
}

inline std::string getPortProtocol(const std::string& portProtocol)
{
    if (portProtocol ==
        "xyz.openbmc_project.Inventory.Item.Port.PortProtocol.Ethernet")
    {
        return "Ethernet";
    }
    if (portProtocol ==
        "xyz.openbmc_project.Inventory.Item.Port.PortProtocol.FC")
    {
        return "FC";
    }
    if (portProtocol ==
        "xyz.openbmc_project.Inventory.Item.Port.PortProtocol.NVLink")
    {
        return "NVLink";
    }
    if (portProtocol ==
        "xyz.openbmc_project.Inventory.Item.Port.PortProtocol.PCIe")
    {
        return "PCIe";
    }
    if (portProtocol ==
        "xyz.openbmc_project.Inventory.Item.Port.PortProtocol.OEM")
    {
        return "OEM";
    }
    if (portProtocol ==
        "xyz.openbmc_project.Inventory.Item.Port.PortProtocol.PCIe")
    {
        return "PCIe";
    }
    if (portProtocol ==
        "xyz.openbmc_project.Inventory.Item.Port.PortProtocol.NVLink.C2C")
    {
        return "NVLink.C2C";
    }

    // Unknown or others
    return "";
}

inline std::string getLinkStates(const std::string& linkState)
{
    if (linkState ==
        "xyz.openbmc_project.Inventory.Item.Port.LinkStates.Enabled")
    {
        return "Enabled";
    }
    if (linkState ==
        "xyz.openbmc_project.Inventory.Item.Port.LinkStates.Disabled")
    {
        return "Disabled";
    }
    // Unknown or others
    return "";
}

inline std::string getPortType(const std::string& portType)
{
    if (portType ==
        "xyz.openbmc_project.Inventory.Item.Port.PortType.BidirectionalPort")
    {
        return "BidirectionalPort";
    }
    if (portType ==
        "xyz.openbmc_project.Inventory.Item.Port.PortType.DownstreamPort")
    {
        return "DownstreamPort";
    }
    if (portType ==
        "xyz.openbmc_project.Inventory.Item.Port.PortType.InterswitchPort")
    {
        return "InterswitchPort";
    }
    if (portType ==
        "xyz.openbmc_project.Inventory.Item.Port.PortType.ManagementPort")
    {
        return "ManagementPort";
    }
    if (portType ==
        "xyz.openbmc_project.Inventory.Item.Port.PortType.UnconfiguredPort")
    {
        return "UnconfiguredPort";
    }
    if (portType ==
        "xyz.openbmc_project.Inventory.Item.Port.PortType.UpstreamPort")
    {
        return "UpstreamPort";
    }
    // Unknown or others
    return "";
}

/**
 * @brief Get all switch info by requesting data
 * from the given D-Bus object.
 *
 * @param[in,out]   asyncResp   Async HTTP response.
 * @param[in]       service     D-Bus service to query.
 * @param[in]       objPath     D-Bus object to query.
 */
inline void getPortData(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                        const std::string& service, const std::string& objPath)
{
    BMCWEB_LOG_DEBUG << "Get Port Data";
    using PropertyType = std::variant<std::string, bool, uint8_t, uint16_t, double,
                                      size_t, std::vector<std::string>>;
    using PropertiesMap = boost::container::flat_map<std::string, PropertyType>;
    // Get interface properties
    crow::connections::systemBus->async_method_call(
        [asyncResp{asyncResp}](const boost::system::error_code ec,
                               const PropertiesMap& properties) {
            if (ec)
            {
                messages::internalError(asyncResp->res);
                return;
            }
#ifdef BMCWEB_ENABLE_NVIDIA_OEM_PROPERTIES
            asyncResp->res.jsonValue["Oem"]["Nvidia"]["@odata.type"] =
                "#NvidiaPort.v1_0_0.NvidiaPort";
#endif // BMCWEB_ENABLE_NVIDIA_OEM_PROPERTIES
            for (const auto& property : properties)
            {
                const std::string& propertyName = property.first;
                if (propertyName == "Type")
                {
                    const std::string* value =
                        std::get_if<std::string>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_DEBUG << "Null value returned "
                                            "for port type";
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res.jsonValue["PortType"] = getPortType(*value);
                }

#ifdef BMCWEB_ENABLE_NVIDIA_OEM_PROPERTIES
                else if (propertyName == "TXWidth")
                {
                    const uint16_t* value =
                        std::get_if<uint16_t>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_DEBUG << "Null value returned "
                                            "for TXWidth";
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    asyncResp->res.jsonValue["Oem"]["Nvidia"]["TXWidth"] =
                        *value;
                }
                else if (propertyName == "RXWidth")
                {
                    const uint16_t* value =
                        std::get_if<uint16_t>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_DEBUG << "Null value returned "
                                            "for RXWidth";
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res.jsonValue["Oem"]["Nvidia"]["RXWidth"] =
                        *value;
                }
#endif // BMCWEB_ENABLE_NVIDIA_OEM_PROPERTIES
                else if (propertyName == "Protocol")
                {
                    const std::string* value =
                        std::get_if<std::string>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_DEBUG << "Null value returned "
                                            "for protocol type";
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    std::string portProtocol = getPortProtocol(*value);
                    if (portProtocol.find(nvlinkToken) != std::string::npos &&
                        portProtocol.size() > nvlinkToken.size())
                    {
                        asyncResp->res.jsonValue["PortProtocol"] = nvlinkToken;
                        std::string expandPortName = portProtocol.substr(nvlinkToken.size() + 1);
                        asyncResp->res.jsonValue["Oem"]["Nvidia"]["PortProtocol"] = expandPortName;
                    }
                    else
                    {
                        asyncResp->res.jsonValue["PortProtocol"] = portProtocol;
                    }
                }
                else if (propertyName == "LinkStatus")
                {
                    const std::string* value =
                        std::get_if<std::string>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_DEBUG << "Null value returned "
                                            "for link status";
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res.jsonValue["LinkStatus"] =
                        getLinkStatusType(*value);
                }
                else if (propertyName == "LinkState")
                {
                    const std::string* value =
                        std::get_if<std::string>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_DEBUG << "Null value returned "
                                            "for link state";
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res.jsonValue["LinkState"] =
                        getLinkStates(*value);
                }
                else if (propertyName == "CurrentSpeed")
                {
                    const double* value = std::get_if<double>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_DEBUG << "Null value returned "
                                            "for CurrentSpeed";
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res.jsonValue["CurrentSpeedGbps"] = *value;
                }
                else if (propertyName == "MaxSpeed")
                {
                    const double* value = std::get_if<double>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_DEBUG << "Null value returned "
                                            "for MaxSpeed";
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res.jsonValue["MaxSpeedGbps"] = *value;
                }
                else if ((propertyName == "Width") ||
                         (propertyName == "ActiveWidth"))
                {
                    const size_t* value = std::get_if<size_t>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_DEBUG << "Null value returned "
                                            "for Width or ActiveWidth";
                        messages::internalError(asyncResp->res);
                        return;
                    }
		    if(*value == INT_MAX)
		    {
			    asyncResp->res.jsonValue[propertyName] = 0;
		    }
	            else
		    {
			    asyncResp->res.jsonValue[propertyName] = *value;
		    }
                }
                else if (propertyName == "CurrentPowerState")
                {
                    const std::string* state =
                        std::get_if<std::string>(&property.second);
                    if (*state ==
                        "xyz.openbmc_project.State.Chassis.PowerState.On")
                    {
                        asyncResp->res.jsonValue["Status"]["State"] = "Enabled";
                    }
                    else if (*state ==
                             "xyz.openbmc_project.State.Chassis.PowerState.Off")
                    {
                        asyncResp->res.jsonValue["Status"]["State"] =
                            "StandbyOffline";
                    }
                }
            }
        },
        service, objPath, "org.freedesktop.DBus.Properties", "GetAll", "");

    asyncResp->res.jsonValue["Status"]["Health"] = "OK";
#ifndef BMCWEB_DISABLE_HEALTH_ROLLUP
    asyncResp->res.jsonValue["Status"]["HealthRollup"] = "OK";
#endif // BMCWEB_DISABLE_HEALTH_ROLLUP
}

/**
 * @brief Get all cpu info by requesting data
 * from the given D-Bus object.
 *
 * @param[in,out]   asyncResp   Async HTTP response.
 * @param[in]       service     D-Bus service to query.
 * @param[in]       objPath     D-Bus object to query.
 */
inline void getCpuPortData(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                           const std::string& service,
                           const std::string& objPath)
{
    BMCWEB_LOG_DEBUG << "Get CPU Port Data";
    using PropertyType = std::variant<std::string>;
    using PropertiesMap = boost::container::flat_map<std::string, PropertyType>;

    // Get interface properties
    crow::connections::systemBus->async_method_call(
        [asyncResp{asyncResp}](const boost::system::error_code ec,
                               const PropertiesMap& properties) {
            if (ec)
            {
                messages::internalError(asyncResp->res);
                return;
            }

            for (const auto& property : properties)
            {
                const std::string& propertyName = property.first;
                if (propertyName == "Type")
                {
                    const std::string* value =
                        std::get_if<std::string>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_DEBUG << "Null value returned "
                                            "for port type";
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res.jsonValue["PortType"] = getPortType(*value);
                }
                else if (propertyName == "Protocol")
                {
                    const std::string* value =
                        std::get_if<std::string>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_DEBUG << "Null value returned "
                                            "for protocol type";
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res.jsonValue["PortProtocol"] =
                        getPortProtocol(*value);
                }
                else if (propertyName == "LinkStatus")
                {
                    const std::string* value =
                        std::get_if<std::string>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_DEBUG << "Null value returned "
                                            "for link status";
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    if (*value ==
                            "xyz.openbmc_project.Inventory.Item.Port.LinkStatusType.LinkDown" ||
                        *value ==
                            "xyz.openbmc_project.Inventory.Item.Port.LinkStatusType.LinkUp")
                    {
                        asyncResp->res.jsonValue["Status"]["Health"] = "OK";
                    }
                    else if (
                        *value ==
                        "xyz.openbmc_project.Inventory.Item.Port.LinkStatusType.NoLink")
                    {
                        asyncResp->res.jsonValue["Status"]["Health"] =
                            "Critical";
                    }
                }
                else if (propertyName == "LinkState")
                {
                    const std::string* value =
                        std::get_if<std::string>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_DEBUG << "Null value returned "
                                            "for link state";
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    if (*value ==
                        "xyz.openbmc_project.Inventory.Item.Port.LinkStates.Enabled")
                    {
                        asyncResp->res.jsonValue["Status"]["State"] = "Enabled";
                    }
                    else if (
                        *value ==
                        "xyz.openbmc_project.Inventory.Item.Port.LinkStates.Disabled")
                    {
                        asyncResp->res.jsonValue["Status"]["State"] =
                            "Disabled";
                    }
                    else if (
                        *value ==
                        "xyz.openbmc_project.Inventory.Item.Port.LinkStates.Error")
                    {
                        asyncResp->res.jsonValue["Status"]["State"] =
                            "UnavailableOffline";
                    }
                    else
                    {
                        asyncResp->res.jsonValue["Status"]["State"] = "Absent";
                    }
                }
            }
        },
        service, objPath, "org.freedesktop.DBus.Properties", "GetAll",
        "xyz.openbmc_project.Inventory.Item.Port");
}

} // namespace port_utils
} // namespace redfish
