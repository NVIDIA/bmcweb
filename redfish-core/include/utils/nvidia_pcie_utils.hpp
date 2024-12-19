#pragma once

namespace redfish
{

static constexpr const char* pcieService = "xyz.openbmc_project.PCIe";
static constexpr const char* pciePath = "/xyz/openbmc_project/PCIe";
static constexpr const char* assetInterface =
    "xyz.openbmc_project.Inventory.Decorator.Asset";
static constexpr const char* uuidInterface = "xyz.openbmc_project.Common.UUID";
static constexpr const char* stateInterface =
    "xyz.openbmc_project.State.Chassis";
static constexpr const char* pcieClockReferenceIntf =
    "xyz.openbmc_project.Inventory.Decorator.PCIeRefClock";
static constexpr const char* nvlinkClockReferenceIntf =
    "com.nvidia.NVLink.NVLinkRefClock";
static constexpr const char* pcieLtssmIntf =
    "xyz.openbmc_project.PCIe.LTSSMState";

static constexpr const char* pcieAerErrorStatusIntf =
    "com.nvidia.PCIe.AERErrorStatus";
static constexpr const char* pcieDeviceInterfaceNV =
    "xyz.openbmc_project.PCIe.Device";

static inline std::string getPCIeType(const std::string& pcieType)
{
    if (pcieType ==
        "xyz.openbmc_project.Inventory.Item.PCIeDevice.PCIeTypes.Gen1")
    {
        return "Gen1";
    }
    if (pcieType ==
        "xyz.openbmc_project.Inventory.Item.PCIeDevice.PCIeTypes.Gen2")
    {
        return "Gen2";
    }
    if (pcieType ==
        "xyz.openbmc_project.Inventory.Item.PCIeDevice.PCIeTypes.Gen3")
    {
        return "Gen3";
    }
    if (pcieType ==
        "xyz.openbmc_project.Inventory.Item.PCIeDevice.PCIeTypes.Gen4")
    {
        return "Gen4";
    }
    if (pcieType ==
        "xyz.openbmc_project.Inventory.Item.PCIeDevice.PCIeTypes.Gen5")
    {
        return "Gen5";
    }
    if (pcieType ==
        "xyz.openbmc_project.Inventory.Item.PCIeDevice.PCIeTypes.Gen6")
    {
        return "Gen6";
    }
    // Unknown or others
    return "Unknown";
}

//  PCIeDevice asset properties
static inline void
    getPCIeDeviceAssetData(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                           const std::string& device, const std::string& path,
                           const std::string& service)
{
    auto getPCIeDeviceAssetCallback =
        [asyncResp{asyncResp}](
            const boost::system::error_code ec,
            const std::vector<std::pair<
                std::string, std::variant<std::string>>>& propertiesList) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error");
                messages::internalError(asyncResp->res);
                return;
            }
            for (const std::pair<std::string, std::variant<std::string>>&
                     property : propertiesList)
            {
                const std::string& propertyName = property.first;
                if ((propertyName == "PartNumber") ||
                    (propertyName == "SerialNumber") ||
                    (propertyName == "Manufacturer") ||
                    (propertyName == "Model"))
                {
                    const std::string* value =
                        std::get_if<std::string>(&property.second);
                    if (value != nullptr)
                    {
                        asyncResp->res.jsonValue[propertyName] = *value;
                    }
                }
            }
        };
    std::string escapedPath = std::string(path) + "/" + device;
    dbus::utility::escapePathForDbus(escapedPath);
    crow::connections::systemBus->async_method_call(
        std::move(getPCIeDeviceAssetCallback), service, escapedPath,
        "org.freedesktop.DBus.Properties", "GetAll", assetInterface);
}


//  PCIeDevice UUID
static inline void
    getPCIeDeviceUUID(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                      const std::string& device, const std::string& path,
                      const std::string& service)
{
    auto getPCIeDeviceUUIDCallback =
        [asyncResp{asyncResp}](const boost::system::error_code ec,
                               const std::variant<std::string>& uuid) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error");
                messages::internalError(asyncResp->res);
                return;
            }
            const std::string* s = std::get_if<std::string>(&uuid);
            if (s != nullptr)
            {
                asyncResp->res.jsonValue["UUID"] = *s;
            }
        };
    std::string escapedPath = std::string(path) + "/" + device;
    dbus::utility::escapePathForDbus(escapedPath);
    crow::connections::systemBus->async_method_call(
        std::move(getPCIeDeviceUUIDCallback), service, escapedPath,
        "org.freedesktop.DBus.Properties", "Get", uuidInterface, "UUID");
}

//  PCIeDevice getPCIeDeviceClkRefOem
static inline void
    getPCIeDeviceClkRefOem(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                           const std::string& device, const std::string& path,
                           const std::string& service)
{
    auto getPCIeDeviceOemCallback = [asyncResp{asyncResp}](
                                        const boost::system::error_code ec,
                                        const std::vector<std::pair<
                                            std::string, std::variant<bool>>>&
                                            propertiesList) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG(
                "DBUS response error on getting PCIeDeviceclock reference OEM properties");
            messages::internalError(asyncResp->res);
            return;
        }

        for (const std::pair<std::string, std::variant<bool>>& property :
             propertiesList)
        {
            const std::string& propertyName = property.first;
            if (propertyName == "PCIeReferenceClockEnabled")
            {
                const bool* value = std::get_if<bool>(&property.second);
                if (value != nullptr)
                {
                    asyncResp->res.jsonValue["Oem"]["Nvidia"][propertyName] =
                        *value;
                }
            }
        }
    };
    std::string escapedPath = std::string(path) + "/" + device;
    dbus::utility::escapePathForDbus(escapedPath);
    crow::connections::systemBus->async_method_call(
        std::move(getPCIeDeviceOemCallback), service, escapedPath,
        "org.freedesktop.DBus.Properties", "GetAll", pcieClockReferenceIntf);
}

//  PCIeDevice nvlink clock reference OEM
static inline void getPCIeDeviceNvLinkClkRefOem(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& device, const std::string& path,
    const std::string& service)
{
    auto getPCIeDeviceOemCallback = [asyncResp{asyncResp}](
                                        const boost::system::error_code ec,
                                        const std::vector<std::pair<
                                            std::string, std::variant<bool>>>&
                                            propertiesList) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG(
                "DBUS response error on getting PCIeDeviceNVLink Clock Reference OEM properties");
            messages::internalError(asyncResp->res);
            return;
        }

        for (const std::pair<std::string, std::variant<bool>>& property :
             propertiesList)
        {
            const std::string& propertyName = property.first;
            if (propertyName == "NVLinkReferenceClockEnabled")
            {
                const bool* value = std::get_if<bool>(&property.second);
                if (value != nullptr)
                {
                    asyncResp->res.jsonValue["Oem"]["Nvidia"][propertyName] =
                        *value;
                }
            }
        }
    };
    std::string escapedPath = std::string(path) + "/" + device;
    dbus::utility::escapePathForDbus(escapedPath);
    crow::connections::systemBus->async_method_call(
        std::move(getPCIeDeviceOemCallback), service, escapedPath,
        "org.freedesktop.DBus.Properties", "GetAll", nvlinkClockReferenceIntf);
}

//  PCIeDevice LTSSM State
static inline void
    getPCIeLTssmState(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                      const std::string& device, const std::string& path,
                      const std::string& service)
{
    BMCWEB_LOG_DEBUG("FROM getPCIeLTssmState");

    std::string escapedPath = std::string(path) + "/" + device;
    dbus::utility::escapePathForDbus(escapedPath);

    crow::connections::systemBus->async_method_call(
        [asyncResp, service](const boost::system::error_code ec,
                             std::variant<std::vector<std::string>>& resp) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("Failed to get connected_port");
                return; // no ports = no failures
            }
            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&resp);
            if (data == nullptr)
            {
                return;
            }

            for (const std::string& portPath : *data)
            {
                auto getPCIeLtssmCallback = [asyncResp{asyncResp}](
                                                const boost::system::error_code
                                                    ec,
                                                const std::vector<std::pair<
                                                    std::string,
                                                    std::variant<std::string>>>&
                                                    propertiesList) {
                    if (ec)
                    {
                        BMCWEB_LOG_ERROR(
                            "DBUS response error on getting PCIeDevice LTSSM State");
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    for (const std::pair<std::string,
                                         std::variant<std::string>>& property :
                         propertiesList)
                    {
                        const std::string& propertyName = property.first;
                        if (propertyName == "LTSSMState")
                        {
                            const std::string* value =
                                std::get_if<std::string>(&property.second);
                            if (value != nullptr)
                            {
                                std::string val =
                                    redfish::dbus_utils::getRedfishLtssmState(
                                        *value);
                                if (val.empty())
                                    asyncResp->res.jsonValue["Oem"]["Nvidia"]
                                                            [propertyName] =
                                        nlohmann::json::value_t::null;

                                else
                                {
                                    asyncResp->res.jsonValue["Oem"]["Nvidia"]
                                                            [propertyName] =
                                        val;
                                }
                            }
                        }
                    }
                };
                crow::connections::systemBus->async_method_call(
                    std::move(getPCIeLtssmCallback), service, portPath,
                    "org.freedesktop.DBus.Properties", "GetAll", pcieLtssmIntf);
            }
        },
        "xyz.openbmc_project.ObjectMapper", escapedPath + "/connected_port",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}


static inline void
    getPCIeDevice(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                  const std::string& device, const std::string& path = pciePath,
                  const std::string& service = pcieService,
                  const std::string& deviceIntf = pcieDeviceInterfaceNV)
{
    auto getPCIeDeviceCallback =
        [asyncResp,
         device](const boost::system::error_code ec,
                 const std::vector<
                     std::pair<std::string, std::variant<std::string, size_t>>>&
                     propertiesList) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG(
                    "failed to get PCIe Device properties ec: {}: {}",
                    ec.value(), ec.message());
                if (ec.value() ==
                    boost::system::linux_error::bad_request_descriptor)
                {
                    messages::resourceNotFound(asyncResp->res, "PCIeDevice",
                                               device);
                }
                else
                {
                    messages::internalError(asyncResp->res);
                }
                return;
            }

            for (const std::pair<std::string,
                                 std::variant<std::string, size_t>>& property :
                 propertiesList)
            {
                const std::string& propertyName = property.first;
                if ((propertyName == "Manufacturer") ||
                    (propertyName == "DeviceType"))
                {
                    const std::string* value =
                        std::get_if<std::string>(&property.second);
                    if (value != nullptr)
                    {
                        asyncResp->res.jsonValue[propertyName] = *value;
                    }
                }
                else if (propertyName == "MaxLanes")
                {
                    const size_t* value = std::get_if<size_t>(&property.second);
                    if (value != nullptr)
                    {
                        asyncResp->res
                            .jsonValue["PCIeInterface"][propertyName] = *value;
                    }
                }
                else if (propertyName == "LanesInUse")
                {
                    const size_t* value = std::get_if<size_t>(&property.second);
                    if (value != nullptr)
                    {
                        if (*value == INT_MAX)
                        {
                            asyncResp->res
                                .jsonValue["PCIeInterface"][propertyName] = 0;
                        }
                        else
                        {
                            asyncResp->res
                                .jsonValue["PCIeInterface"][propertyName] =
                                *value;
                        }
                    }
                }
                else if ((propertyName == "PCIeType") ||
                         (propertyName == "MaxPCIeType"))
                {
                    const std::string* value =
                        std::get_if<std::string>(&property.second);
                    if (value != nullptr)
                    {
                        std::optional<std::string> propValue =
                            pcie_util::redfishPcieTypeStringFromDbus(*value);
                        if (!propValue)
                        {
                            asyncResp->res
                                .jsonValue["PCIeInterface"][propertyName] =
                                nullptr;
                        }
                        else
                        {
                            asyncResp->res
                                .jsonValue["PCIeInterface"][propertyName] =
                                *propValue;
                        }
                    }
                }
                else if (propertyName == "GenerationInUse")
                {
                    const std::string* value =
                        std::get_if<std::string>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("property {} value not found.",
                                         propertyName);
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    std::optional<std::string> generationInUse =
                        pcie_util::redfishPcieGenerationStringFromDbus(*value);
                    if (!generationInUse)
                    {
                        asyncResp->res.jsonValue["PCIeInterface"]["PCIeType"] =
                            nullptr;
                    }
                    else
                    {
                        asyncResp->res.jsonValue["PCIeInterface"]["PCIeType"] =
                            *generationInUse;
                    }
                }
            }
        };
    std::string escapedPath = std::string(path) + "/" + device;
    dbus::utility::escapePathForDbus(escapedPath);
    crow::connections::systemBus->async_method_call(
        std::move(getPCIeDeviceCallback), service, escapedPath,
        "org.freedesktop.DBus.Properties", "GetAll", deviceIntf);
}


static inline void getPCIeDeviceFunctionsList(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& device, const std::string& path = pciePath,
    const std::string& service = pcieService,
    const std::string& deviceIntf = pcieDeviceInterfaceNV,
    const std::string& chassisId = std::string())
{
    auto getPCIeDeviceCallback =
        [asyncResp{asyncResp}, device, chassisId](
            const boost::system::error_code ec,
            boost::container::flat_map<std::string, std::variant<std::string>>&
                pcieDevProperties) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG(
                    "failed to get PCIe Device properties ec: {}: {} ",
                    ec.value(), ec.message());

                if (ec.value() ==
                    boost::system::linux_error::bad_request_descriptor)
                {
                    messages::resourceNotFound(asyncResp->res, "PCIeDevice",
                                               device);
                }
                else
                {
                    messages::internalError(asyncResp->res);
                }
                return;
            }

            nlohmann::json& pcieFunctionList =
                asyncResp->res.jsonValue["Members"];
            pcieFunctionList = nlohmann::json::array();
            static constexpr const int maxPciFunctionNum = 8;
            for (int functionNum = 0; functionNum < maxPciFunctionNum;
                 functionNum++)
            {
                // Check if this function exists by looking for a device
                // ID
                std::string devIDProperty =
                    "Function" + std::to_string(functionNum) + "DeviceId";
                std::string* property =
                    std::get_if<std::string>(&pcieDevProperties[devIDProperty]);
                if (property && !property->empty())
                {
                    if (!chassisId.empty())
                    {
                        pcieFunctionList.push_back(
                            {{"@odata.id",
                              std::string("/redfish/v1/Chassis/")
                                  .append(chassisId)
                                  .append("/PCIeDevices/")
                                  .append(device)
                                  .append("/PCIeFunctions/")
                                  .append(std::to_string(functionNum))}});
                    }
                    else
                    {
                        pcieFunctionList.push_back(
                            {{"@odata.id",
                              "/redfish/v1/Systems/" +
                                  std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                                  "/PCIeDevices/" + device + "/PCIeFunctions/" +
                                  std::to_string(functionNum)}});
                    }
                }
            }
            asyncResp->res.jsonValue["Members@odata.count"] =
                pcieFunctionList.size();
        };
    std::string escapedPath = std::string(path) + "/" + device;
    dbus::utility::escapePathForDbus(escapedPath);
    crow::connections::systemBus->async_method_call(
        std::move(getPCIeDeviceCallback), service, escapedPath,
        "org.freedesktop.DBus.Properties", "GetAll", deviceIntf);
}

static inline void getPCIeDeviceFunction(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& device, const std::string& function,
    const std::string& path = pciePath,
    const std::string& service = pcieService,
    const std::string& chassisId = std::string(),
    const std::string& deviceIntf = pcieDeviceInterfaceNV)
{
    auto getPCIeDeviceCallback = [asyncResp{asyncResp}, device, function,
                                  chassisId](const boost::system::error_code ec,
                                             boost::container::flat_map<
                                                 std::string,
                                                 std::variant<std::string>>&
                                                 pcieDevProperties) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG("failed to get PCIe Device properties ec: {} : {}",
                             ec.value(), ec.message());
            if (ec.value() ==
                boost::system::linux_error::bad_request_descriptor)
            {
                messages::resourceNotFound(asyncResp->res, "PCIeDevice",
                                           device);
            }
            else
            {
                messages::internalError(asyncResp->res);
            }
            return;
        }

        // Check if this function exists by looking for a device ID
        std::string devIDProperty = "Function" + function + "DeviceId";
        if (std::string* property =
                std::get_if<std::string>(&pcieDevProperties[devIDProperty]);
            property && property->empty())
        {
            messages::resourceNotFound(
                asyncResp->res, "#PCIeFunction.v1_2_0.PCIeFunction", function);
            return;
        }

        // Update pcieDeviceURI based on system or chassis path
        std::string pcieDeviceURI;
        std::string pcieFunctionURI;
        if (chassisId.empty())
        {
            pcieDeviceURI =
                boost::urls::format("/redfish/v1/Systems/{}/PCIeDevices/{}",
                                    BMCWEB_REDFISH_SYSTEM_URI_NAME, device)
                    .buffer();
            pcieFunctionURI =
                boost::urls::format(
                    "/redfish/v1/Systems/{}/PCIeDevices/{}/PCIeFunctions/{}",
                    BMCWEB_REDFISH_SYSTEM_URI_NAME, device, function)
                    .buffer();
        }
        else
        {
            pcieDeviceURI =
                boost::urls::format("/redfish/v1/Chassis/{}/PCIeDevices/{}",
                                    chassisId, device)
                    .buffer();
            pcieFunctionURI =
                boost::urls::format(
                    "/redfish/v1/Chassis/{}/PCIeDevices/{}/PCIeFunctions/{}",
                    chassisId, device, function)
                    .buffer();
        }

        asyncResp->res.jsonValue = {
            {"@odata.type", "#PCIeFunction.v1_2_0.PCIeFunction"},
            {"@odata.id", pcieFunctionURI},
            {"Name", "PCIe Function"},
            {"Id", function},
            {"FunctionId", std::stoi(function)},
            {"Links", {{"PCIeDevice", {{"@odata.id", pcieDeviceURI}}}}}};

        for (const auto& property : pcieDevProperties)
        {
            const std::string& propertyName = property.first;
            if (propertyName == "Function" + function + "DeviceId")
            {
                const std::string* value =
                    std::get_if<std::string>(&property.second);
                if (value != nullptr)
                {
                    asyncResp->res.jsonValue["DeviceId"] = *value;
                }
            }
            else if (propertyName == "Function" + function + "VendorId")
            {
                const std::string* value =
                    std::get_if<std::string>(&property.second);
                if (value != nullptr)
                {
                    asyncResp->res.jsonValue["VendorId"] = *value;
                }
            }
            else if (propertyName == "Function" + function + "FunctionType")
            {
                const std::string* value =
                    std::get_if<std::string>(&property.second);
                if (value != nullptr)
                {
                    asyncResp->res.jsonValue["FunctionType"] = *value;
                }
            }
            else if (propertyName == "Function" + function + "DeviceClass")
            {
                const std::string* value =
                    std::get_if<std::string>(&property.second);
                if (value != nullptr)
                {
                    asyncResp->res.jsonValue["DeviceClass"] = *value;
                }
            }
            else if (propertyName == "Function" + function + "ClassCode")
            {
                const std::string* value =
                    std::get_if<std::string>(&property.second);
                if (value != nullptr)
                {
                    asyncResp->res.jsonValue["ClassCode"] = *value;
                }
            }
            else if (propertyName == "Function" + function + "RevisionId")
            {
                const std::string* value =
                    std::get_if<std::string>(&property.second);
                if (value != nullptr)
                {
                    asyncResp->res.jsonValue["RevisionId"] = *value;
                }
            }
            else if (propertyName == "Function" + function + "SubsystemId")
            {
                const std::string* value =
                    std::get_if<std::string>(&property.second);
                if (value != nullptr)
                {
                    asyncResp->res.jsonValue["SubsystemId"] = *value;
                }
            }
            else if (propertyName ==
                     "Function" + function + "SubsystemVendorId")
            {
                const std::string* value =
                    std::get_if<std::string>(&property.second);
                if (value != nullptr)
                {
                    asyncResp->res.jsonValue["SubsystemVendorId"] = *value;
                }
            }
        }
    };
    std::string escapedPath = std::string(path) + "/" + device;
    dbus::utility::escapePathForDbus(escapedPath);
    crow::connections::systemBus->async_method_call(
        std::move(getPCIeDeviceCallback), service, escapedPath,
        "org.freedesktop.DBus.Properties", "GetAll", deviceIntf);
}

namespace nvidia_pcie_utils
{

static constexpr const char* pciePath = "/xyz/openbmc_project/PCIe";
static constexpr const char* pcieAerErrorStatusIntf =
    "com.nvidia.PCIe.AERErrorStatus";

inline void getPCIeDeviceList(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& name, const std::string& path = pciePath,
    const std::string& chassisId = std::string())
{
    auto getPCIeMapCallback = [asyncResp{asyncResp}, name, chassisId](
                                  const boost::system::error_code ec,
                                  std::vector<std::string>& pcieDevicePaths) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG("no PCIe device paths found ec: ", ec.message());
            // Not an error, system just doesn't have PCIe info
            return;
        }
        nlohmann::json& pcieDeviceList = asyncResp->res.jsonValue[name];
        pcieDeviceList = nlohmann::json::array();
        for (const std::string& pcieDevicePath : pcieDevicePaths)
        {
            size_t devStart = pcieDevicePath.rfind('/');
            if (devStart == std::string::npos)
            {
                continue;
            }

            std::string devName = pcieDevicePath.substr(devStart + 1);
            if (devName.empty())
            {
                continue;
            }
            if (!chassisId.empty())
            {
                pcieDeviceList.push_back(
                    {{"@odata.id", std::string("/redfish/v1/Chassis/")
                                       .append(chassisId)
                                       .append("/PCIeDevices/")
                                       .append(devName)}});
            }
            else
            {
                pcieDeviceList.push_back(
                    {{"@odata.id",
                      "/redfish/v1/Systems/" +
                          std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                          "/PCIeDevices/" + devName}});
            }
        }
        asyncResp->res.jsonValue[name + "@odata.count"] = pcieDeviceList.size();
    };
    crow::connections::systemBus->async_method_call(
        std::move(getPCIeMapCallback), "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTreePaths",
        std::string(path) + "/", 1, std::array<std::string, 0>());
}

inline void
    getAerErrorStatusOem(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                         const std::string& device, const std::string& path,
                         const std::string& service)
{
    auto getAerErrorStatusOemCallback = [asyncResp{asyncResp}](
                                            const boost::system::error_code ec,
                                            const std::vector<std::pair<
                                                std::string,
                                                std::variant<
                                                    std::string,
                                                    std::vector<uint32_t>>>>&
                                                propertiesList) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG(
                "DBUS response error on getting AER Error Status reference OEM properties");
            messages::internalError(asyncResp->res);
            return;
        }

        for (const std::pair<std::string,
                             std::variant<std::string, std::vector<uint32_t>>>&
                 property : propertiesList)
        {
            const std::string& propertyName = property.first;
            if (propertyName == "AERUncorrectableErrorStatus")
            {
                const std::string* value =
                    std::get_if<std::string>(&property.second);
                if (value != nullptr)
                {
                    asyncResp->res.jsonValue["Oem"]["Nvidia"][propertyName] =
                        *value;
                }
            }
            else if (propertyName == "AERCorrectableErrorStatus")
            {
                const std::string* value =
                    std::get_if<std::string>(&property.second);
                if (value != nullptr)
                {
                    asyncResp->res.jsonValue["Oem"]["Nvidia"][propertyName] =
                        *value;
                }
            }
        }
    };
    std::string escapedPath = std::string(path) + "/" + device;
    dbus::utility::escapePathForDbus(escapedPath);
    crow::connections::systemBus->async_method_call(
        std::move(getAerErrorStatusOemCallback), service, escapedPath,
        "org.freedesktop.DBus.Properties", "GetAll", pcieAerErrorStatusIntf);
}

inline void
    clearAerErrorStatus(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                        const std::string& connection, const std::string& path)
{
    dbus::utility::getDbusObject(
        path, std::array<std::string_view, 1>{"com.nvidia.PCIe.AERErrorStatus"},
        [asyncResp, path,
         connection](const boost::system::error_code& ec,
                     const dbus::utility::MapperGetObject& object) {
            if (!ec)
            {
                for (const auto& [serv, _] : object)
                {
                    if (serv != connection)
                    {
                        continue;
                    }

                    BMCWEB_LOG_DEBUG("Performing Post using Async Method Call");

                    nvidia_async_operation_utils::
                        doGenericCallAsyncAndGatherResult<int>(
                            asyncResp, std::chrono::seconds(60), connection,
                            path, "com.nvidia.PCIe.AERErrorStatus",
                            "ClearAERStatus",
                            [asyncResp](const std::string& status,
                                        [[maybe_unused]] const int* retValue) {
                                if (status == nvidia_async_operation_utils::
                                                  asyncStatusValueSuccess)
                                {
                                    BMCWEB_LOG_DEBUG(
                                        "Clear AER Error Status Succeeded");
                                    messages::success(asyncResp->res);
                                    return;
                                }
                                BMCWEB_LOG_ERROR(
                                    "Clear AER Error Status Throws error {}",
                                    status);
                                messages::internalError(asyncResp->res);
                            });

                    return;
                }
            }
        });
};

inline void postClearAerErrorStatus(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& device)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp, chassisId,
         device](const boost::system::error_code ec,
                 const std::vector<std::string>& chassisPaths) {
            if (ec)
            {
                messages::internalError(asyncResp->res);
                return;
            }
            for (const std::string& chassisPath : chassisPaths)
            {
                // Get the chassisId object
                sdbusplus::message::object_path objPath(chassisPath);
                if (objPath.filename() != chassisId)
                {
                    continue;
                }
                const std::string chassisPCIePath =
                    std::string(
                        "/xyz/openbmc_project/inventory/system/chassis/")
                        .append(chassisId)
                        .append("/PCIeDevices");
                const std::string chassisPCIeDevicePath =
                    std::string(chassisPCIePath).append("/").append(device);
                const std::array<const char*, 1> interface = {
                    "xyz.openbmc_project.Inventory.Item.PCIeDevice"};
                // Get Inventory Service
                crow::connections::systemBus->async_method_call(
                    [asyncResp, device, chassisPCIePath, chassisId,
                     chassisPCIeDevicePath,
                     chassisPath](const boost::system::error_code ec,
                                  const GetSubTreeType& subtree) {
                        if (ec)
                        {
                            BMCWEB_LOG_DEBUG("DBUS response error");
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        // Iterate over all retrieved ObjectPaths.
                        for (const std::pair<
                                 std::string,
                                 std::vector<std::pair<
                                     std::string, std::vector<std::string>>>>&
                                 object : subtree)
                        {
                            if (object.first != chassisPCIeDevicePath)
                            {
                                continue;
                            }
                            const std::vector<std::pair<
                                std::string, std::vector<std::string>>>&
                                connectionNames = object.second;

                            for (auto [connection, interfaces] :
                                 connectionNames)
                            {
                                if (std::find(
                                        interfaces.begin(), interfaces.end(),
                                        "com.nvidia.PCIe.AERErrorStatus") !=
                                    interfaces.end())
                                {
                                    clearAerErrorStatus(asyncResp, connection,
                                                        object.first);
                                    return;
                                }
                            }
                        }
                        messages::resourceNotFound(
                            asyncResp->res, "#PCIeDevice.v1_14_0.PCIeDevice",
                            device);
                    },
                    "xyz.openbmc_project.ObjectMapper",
                    "/xyz/openbmc_project/object_mapper",
                    "xyz.openbmc_project.ObjectMapper", "GetSubTree",
                    "/xyz/openbmc_project/inventory", 0, interface);
                return;
            }
            messages::resourceNotFound(asyncResp->res,
                                       "#Chassis.v1_15_0.Chassis", chassisId);
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTreePaths",
        "/xyz/openbmc_project/inventory", 0,
        std::array<const char*, 1>{
            "xyz.openbmc_project.Inventory.Item.Chassis"});
};

inline void getFabricSwitchLink(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                                const std::string& objPath)
{
    crow::connections::systemBus->async_method_call(
        [aResp, objPath](const boost::system::error_code ec,
                         std::variant<std::vector<std::string>>& resp) {
            if (ec)
            {
                return; // no fabric = no failures
            }
            std::vector<std::string>* dataFabric =
                std::get_if<std::vector<std::string>>(&resp);
            if (dataFabric == nullptr || dataFabric->size() > 1)
            {
                // There must be single fabric
                return;
            }
            const std::string& fabricPath = dataFabric->front();
            sdbusplus::message::object_path objectPath(fabricPath);
            std::string fabricId = objectPath.filename();
            if (fabricId.empty())
            {
                BMCWEB_LOG_ERROR("Fabric name empty");
                messages::internalError(aResp->res);
                return;
            }

            crow::connections::systemBus->async_method_call(
                [aResp,
                 fabricId](const boost::system::error_code ec,
                           std::variant<std::vector<std::string>>& resp1) {
                    if (ec)
                    {
                        return; // no switches = no failures
                    }
                    std::vector<std::string>* dataSwitch =
                        std::get_if<std::vector<std::string>>(&resp1);
                    if (dataSwitch == nullptr || dataSwitch->size() > 1)
                    {
                        // There must be single fabric
                        return;
                    }

                    const std::string& switchPath = dataSwitch->front();
                    sdbusplus::message::object_path objectPath1(switchPath);
                    std::string switchId = objectPath1.filename();
                    if (switchId.empty())
                    {
                        BMCWEB_LOG_ERROR("Switch name empty");
                        messages::internalError(aResp->res);
                        return;
                    }
                    std::string switchLink =
                        boost::urls::format(
                            "/redfish/v1/Fabrics/{}/Switches/{}", fabricId,
                            switchId)
                            .buffer();
                    aResp->res.jsonValue["Links"]["Switch"]["@odata.id"] =
                        switchLink;
                    return;
                },
                "xyz.openbmc_project.ObjectMapper", objPath + "/all_switches",
                "org.freedesktop.DBus.Properties", "Get",
                "xyz.openbmc_project.Association", "endpoints");
        },
        "xyz.openbmc_project.ObjectMapper", objPath + "/fabrics",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

} // namespace nvidia_pcie_utils

inline void requestRoutesChassisPCIeFunctionCollection(App& app)
{
    BMCWEB_ROUTE(app,
                 "/redfish/v1/Chassis/<str>/PCIeDevices/<str>/PCIeFunctions/")
        .privileges({{"Login"}})
        .methods(
            boost::beast::http::verb::
                get)([&app](const crow::Request& req,
                            const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const std::string& chassisId,
                            const std::string& device) {
            if (!redfish::setUpRedfishRoute(app, req, asyncResp))
            {
                return;
            }
            crow::connections::systemBus->async_method_call(
                [asyncResp, chassisId,
                 device](const boost::system::error_code ec,
                         const std::vector<std::string>& chassisPaths) {
                    if (ec)
                    {
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    for (const std::string& chassisPath : chassisPaths)
                    {
                        // Get the chassisId object
                        sdbusplus::message::object_path objPath(chassisPath);
                        if (objPath.filename() != chassisId)
                        {
                            continue;
                        }
                        const std::string chassisPCIePath =
                            std::string(
                                "/xyz/openbmc_project/inventory/system/chassis/")
                                .append(chassisId)
                                .append("/PCIeDevices");
                        const std::string chassisPCIeDevicePath =
                            std::string(chassisPCIePath)
                                .append("/")
                                .append(device);
                        const std::array<const char*, 1> interface = {
                            "xyz.openbmc_project.Inventory.Item.PCIeDevice"};
                        // Response
                        std::string pcieFunctionURI = "/redfish/v1/Chassis/";
                        pcieFunctionURI += chassisId;
                        pcieFunctionURI += "/PCIeDevices/";
                        pcieFunctionURI += device;
                        pcieFunctionURI += "/PCIeFunctions";
                        asyncResp->res.jsonValue = {
                            {"@odata.type",
                             "#PCIeFunctionCollection.PCIeFunctionCollection"},
                            {"@odata.id", pcieFunctionURI},
                            {"Name", "PCIe Function Collection"},
                            {"Description",
                             "Collection of PCIe Functions for PCIe Device " +
                                 device}};
                        // Get Inventory Service
                        crow::connections::systemBus->async_method_call(
                            [asyncResp, device, chassisPCIePath, interface,
                             chassisId, chassisPCIeDevicePath](
                                const boost::system::error_code ec,
                                const GetSubTreeType& subtree) {
                                if (ec)
                                {
                                    BMCWEB_LOG_DEBUG("DBUS response error");
                                    messages::internalError(asyncResp->res);
                                    return;
                                }
                                // Iterate over all retrieved ObjectPaths.
                                for (const std::pair<
                                         std::string,
                                         std::vector<std::pair<
                                             std::string,
                                             std::vector<std::string>>>>&
                                         object : subtree)
                                {
                                    if (object.first != chassisPCIeDevicePath)
                                    {
                                        continue;
                                    }
                                    const std::vector<std::pair<
                                        std::string, std::vector<std::string>>>&
                                        connectionNames = object.second;
                                    if (connectionNames.size() < 1)
                                    {
                                        BMCWEB_LOG_ERROR(
                                            "Got 0 Connection names");
                                        continue;
                                    }
                                    const std::string& connectionName =
                                        connectionNames[0].first;
                                    getPCIeDeviceFunctionsList(
                                        asyncResp, device, chassisPCIePath,
                                        connectionName, interface[0],
                                        chassisId);
                                    return;
                                }
                                messages::resourceNotFound(
                                    asyncResp->res,
                                    "#PCIeDevice.v1_14_0.PCIeDevice", device);
                            },
                            "xyz.openbmc_project.ObjectMapper",
                            "/xyz/openbmc_project/object_mapper",
                            "xyz.openbmc_project.ObjectMapper", "GetSubTree",
                            "/xyz/openbmc_project/inventory", 0, interface);
                        return;
                    }
                    messages::resourceNotFound(
                        asyncResp->res, "#Chassis.v1_15_0.Chassis", chassisId);
                },
                "xyz.openbmc_project.ObjectMapper",
                "/xyz/openbmc_project/object_mapper",
                "xyz.openbmc_project.ObjectMapper", "GetSubTreePaths",
                "/xyz/openbmc_project/inventory", 0,
                std::array<const char*, 1>{
                    "xyz.openbmc_project.Inventory.Item.Chassis"});
        });
}

inline void requestRoutesChassisPCIeFunction(App& app)
{
    BMCWEB_ROUTE(
        app, "/redfish/v1/Chassis/<str>/PCIeDevices/<str>/PCIeFunctions/<str>/")
        .privileges({{"Login"}})
        .methods(
            boost::beast::http::verb::
                get)([&app](const crow::Request& req,
                            const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const std::string& chassisId,
                            const std::string& device,
                            const std::string& function) {
            if (!redfish::setUpRedfishRoute(app, req, asyncResp))
            {
                return;
            }
            crow::connections::systemBus->async_method_call(
                [asyncResp, chassisId, device,
                 function](const boost::system::error_code ec,
                           const std::vector<std::string>& chassisPaths) {
                    if (ec)
                    {
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    for (const std::string& chassisPath : chassisPaths)
                    {
                        // Get the chassisId object
                        sdbusplus::message::object_path objPath(chassisPath);
                        if (objPath.filename() != chassisId)
                        {
                            continue;
                        }
                        const std::string chassisPCIePath =
                            std::string(
                                "/xyz/openbmc_project/inventory/system/chassis/")
                                .append(chassisId)
                                .append("/PCIeDevices");
                        const std::string chassisPCIeDevicePath =
                            std::string(chassisPCIePath)
                                .append("/")
                                .append(device);
                        const std::array<const char*, 1> interface = {
                            "xyz.openbmc_project.Inventory.Item.PCIeDevice"};
                        // Get Inventory Service
                        crow::connections::systemBus->async_method_call(
                            [asyncResp, device, function, chassisPCIePath,
                             interface, chassisId, chassisPCIeDevicePath](
                                const boost::system::error_code ec,
                                const GetSubTreeType& subtree) {
                                if (ec)
                                {
                                    BMCWEB_LOG_DEBUG("DBUS response error");
                                    messages::internalError(asyncResp->res);
                                    return;
                                }
                                // Iterate over all retrieved ObjectPaths.
                                for (const std::pair<
                                         std::string,
                                         std::vector<std::pair<
                                             std::string,
                                             std::vector<std::string>>>>&
                                         object : subtree)
                                {
                                    if (object.first != chassisPCIeDevicePath)
                                    {
                                        continue;
                                    }
                                    const std::vector<std::pair<
                                        std::string, std::vector<std::string>>>&
                                        connectionNames = object.second;
                                    if (connectionNames.size() < 1)
                                    {
                                        BMCWEB_LOG_ERROR(
                                            "Got 0 Connection names");
                                        continue;
                                    }
                                    const std::string& connectionName =
                                        connectionNames[0].first;
                                    getPCIeDeviceFunction(
                                        asyncResp, device, function,
                                        chassisPCIePath, connectionName,
                                        chassisId, interface[0]);
                                    return;
                                }
                                messages::resourceNotFound(
                                    asyncResp->res,
                                    "#PCIeDevice.v1_14_0.PCIeDevice", device);
                            },
                            "xyz.openbmc_project.ObjectMapper",
                            "/xyz/openbmc_project/object_mapper",
                            "xyz.openbmc_project.ObjectMapper", "GetSubTree",
                            "/xyz/openbmc_project/inventory", 0, interface);
                        return;
                    }
                    messages::resourceNotFound(
                        asyncResp->res, "#Chassis.v1_15_0.Chassis", chassisId);
                },
                "xyz.openbmc_project.ObjectMapper",
                "/xyz/openbmc_project/object_mapper",
                "xyz.openbmc_project.ObjectMapper", "GetSubTreePaths",
                "/xyz/openbmc_project/inventory", 0,
                std::array<const char*, 1>{
                    "xyz.openbmc_project.Inventory.Item.Chassis"});
        });
    // Todo Telemetry team - Need to migrate downstream code to this function
    // BMCWEB_ROUTE(
    //     app,
    //     "/redfish/v1/Systems/<str>/PCIeDevices/<str>/PCIeFunctions/<str>/")
    //     .privileges(redfish::privileges::getPCIeFunction)
    //     .methods(boost::beast::http::verb::get)(
    //         std::bind_front(handlePCIeFunctionGet, std::ref(app)));
}

inline void requestRoutesClearPCIeAerErrorStatus(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Chassis/<str>/PCIeDevices/<str>/Actions/Oem/NvidiaPCIeDevice.ClearAERErrorStatus/")
        .privileges({{"Login"}})
        .methods(boost::beast::http::verb::post)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& chassisId, const std::string& device) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }

                redfish::nvidia_pcie_utils::postClearAerErrorStatus(
                    asyncResp, chassisId, device);
            });
}

} // namespace redfish
