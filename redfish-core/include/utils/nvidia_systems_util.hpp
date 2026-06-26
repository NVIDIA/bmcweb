#pragma once
#include "dbus_utility.hpp"
#include "utils/dbus_utils.hpp"
#include "utils/json_utils.hpp"
#include "utils/nvidia_time_utils.hpp"
#include "utils/nvidia_utils.hpp"
#include "utils/pcie_util.hpp"
#include "utils/privilege_utils.hpp"
#include "utils/sw_utils.hpp"
#include "utils/time_utils.hpp"

namespace redfish
{
namespace nvidia_systems_utils
{
static const std::string& entityMangerService =
    "xyz.openbmc_project.EntityManager";
static const std::string& card1Path =
    "/xyz/openbmc_project/inventory/system/board/Card1";

/**
 * @brief Populate objects from D-Bus object of entity-manager
 *
 * @param[in] aResp  - Shared pointer for completing asynchronous calls.
 *
 * @return None.
 */
inline void populateFromEntityManger(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp)
{
    dbus::utility::getAllProperties(
        entityMangerService, card1Path,
        "xyz.openbmc_project.Inventory.Decorator.Asset",
        [aResp](const boost::system::error_code& ec,
                const dbus::utility::DBusPropertiesMap& propertiesList) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error for "
                                 "Populate from entity manager ");
                return;
            }
            for (const auto& property : propertiesList)
            {
                const std::string& propertyName = property.first;
                if (propertyName == "SerialNumber")
                {
                    const std::string* serialNumber =
                        std::get_if<std::string>(&property.second);
                    if (serialNumber != nullptr)
                    {
                        aResp->res.jsonValue["SerialNumber"] = *serialNumber;
                    }
                }
                if (propertyName == "Manufacturer")
                {
                    const std::string* manufacturer =
                        std::get_if<std::string>(&property.second);
                    if (manufacturer != nullptr)
                    {
                        aResp->res.jsonValue["Manufacturer"] = *manufacturer;
                    }
                }
                if (propertyName == "Model")
                {
                    const std::string* model =
                        std::get_if<std::string>(&property.second);
                    if (model != nullptr)
                    {
                        aResp->res.jsonValue["Model"] = *model;
                    }
                }
            }
        });
    dbus::utility::getProperty<std::string>(
        entityMangerService, card1Path,
        "xyz.openbmc_project.Inventory.Decorator.SKU", "SKU",
        [aResp](const boost::system::error_code& ec, const std::string& sku) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error for "
                                 "Trying to get SKU");
                return;
            }
            aResp->res.jsonValue["SKU"] = sku;
        }

    );
    dbus::utility::getProperty<std::string>(
        entityMangerService, card1Path, "xyz.openbmc_project.Common.UUID",
        "UUID",
        [aResp](const boost::system::error_code& ec, const std::string& uuid) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error for "
                                 "Trying to get UUID");
                return;
            }
            aResp->res.jsonValue["UUID"] = uuid;
        });
}

/**
 * @brief Set Boot Order properties.
 *
 * @param[in] aResp  Shared pointer for generating response message.
 * @param[in] username  Username from request.
 * @param[in] bootOrder  Boot order properties from request.
 * @param[in] isSettingsResource  false to set active BootOrder, true to set
 * pending BootOrder in Settings URI
 *
 * @return None.
 */
inline void setBootOrder(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                         const crow::Request& req,
                         const std::vector<std::string>& bootOrder,
                         const bool isSettingsResource = false)
{
    BMCWEB_LOG_DEBUG("Set boot order.");

    auto setBootOrderFunc = [aResp, bootOrder, isSettingsResource]() {
        if (!isSettingsResource)
        {
            dbus::utility::setProperty(
                "xyz.openbmc_project.BIOSConfigManager",
                "/xyz/openbmc_project/bios_config/manager",
                "xyz.openbmc_project.BIOSConfig.BootOrder", "BootOrder",
                bootOrder, [aResp](const boost::system::error_code& ec) {
                    if (ec)
                    {
                        BMCWEB_LOG_ERROR(
                            "DBUS response error on BootOrder setProperty: {}",
                            ec);
                        messages::internalError(aResp->res);
                        return;
                    }
                });
        }
        else
        {
            dbus::utility::getProperty<std::vector<std::string>>(
                "xyz.openbmc_project.BIOSConfigManager",
                "/xyz/openbmc_project/bios_config/manager",
                "xyz.openbmc_project.BIOSConfig.BootOrder", "BootOrder",
                [aResp,
                 bootOrder](const boost::system::error_code& ec,
                            const std::vector<std::string>& activeBootOrder) {
                    if (ec)
                    {
                        BMCWEB_LOG_DEBUG(
                            "DBUS response error on BootOrder getProperty: {}",
                            ec);
                        messages::internalError(aResp->res);
                        return;
                    }
                    if (bootOrder.size() != activeBootOrder.size())
                    {
                        BMCWEB_LOG_DEBUG("New BootOrder length is incorrect");
                        messages::propertyValueIncorrect(
                            aResp->res, "Boot/BootOrder",
                            nlohmann::json(bootOrder).dump());
                        return;
                    }
                    // Check every bootReference of acitve BootOrder
                    // existing in new BootOrder.
                    for (const auto& bootReference : activeBootOrder)
                    {
                        auto result = std::find(bootOrder.begin(),
                                                bootOrder.end(), bootReference);
                        if (result == bootOrder.end())
                        {
                            BMCWEB_LOG_DEBUG("{} missing in new BootOrder",
                                             bootReference);
                            messages::propertyValueIncorrect(
                                aResp->res, "Boot/BootOrder",
                                nlohmann::json(bootOrder).dump());
                            return;
                        }
                    }

                    dbus::utility::setProperty(
                        "xyz.openbmc_project.BIOSConfigManager",
                        "/xyz/openbmc_project/bios_config/manager",
                        "xyz.openbmc_project.BIOSConfig.BootOrder",
                        "PendingBootOrder", bootOrder,
                        [aResp](const boost::system::error_code& ec2) {
                            if (ec2)
                            {
                                BMCWEB_LOG_ERROR(
                                    "DBUS response error on BootOrder setProperty: {}",
                                    ec2);
                                messages::internalError(aResp->res);
                                return;
                            }
                        });
                });
        }
    };

    if (!isSettingsResource)
    {
        // Only BIOS is allowed to patch active BootOrder
        privilege_utils::isBiosPrivilege(
            req.session->username,
            [aResp, setBootOrderFunc](const boost::system::error_code& ec,
                                      const bool isBios) {
                if (ec || !isBios)
                {
                    messages::propertyNotWritable(aResp->res, "BootOrder");
                    return;
                }
                setBootOrderFunc();
            });
    }
    else
    {
        setBootOrderFunc();
    }
}

/**
 * @brief Retrieves host boot order properties over DBUS
 *
 * @param[in] aResp     Shared pointer for completing asynchronous calls.
 *
 * @return None.
 */
inline void getBootOrder(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                         const bool isSettingsResource = false)
{
    BMCWEB_LOG_DEBUG("Get boot order parameters");

    dbus::utility::getAllProperties(
        "xyz.openbmc_project.BIOSConfigManager",
        "/xyz/openbmc_project/bios_config/manager",
        "xyz.openbmc_project.BIOSConfig.BootOrder",
        [aResp, isSettingsResource](
            const boost::system::error_code& ec,
            const dbus::utility::DBusPropertiesMap& properties) {
            if (ec)
            {
                // This is an optional interface so just return
                // if failed to get all properties
                BMCWEB_LOG_DEBUG("No BootOrder found");
                return;
            }

            std::vector<std::string> bootOrder;
            std::vector<std::string> pendingBootOrder;
            for (const auto& [propertyName, propertyVariant] : properties)
            {
                if (propertyName == "BootOrder" &&
                    std::holds_alternative<std::vector<std::string>>(
                        propertyVariant))
                {
                    bootOrder =
                        std::get<std::vector<std::string>>(propertyVariant);
                }
                else if (propertyName == "PendingBootOrder" &&
                         std::holds_alternative<std::vector<std::string>>(
                             propertyVariant))
                {
                    pendingBootOrder =
                        std::get<std::vector<std::string>>(propertyVariant);
                }
            }
            if (!isSettingsResource)
            {
                aResp->res.jsonValue["@Redfish.Settings"]["@odata.type"] =
                    "#Settings.v1_3_5.Settings";
                aResp->res.jsonValue["@Redfish.Settings"]["SettingsObject"] = {
                    {"@odata.id",
                     "/redfish/v1/Systems/" +
                         std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                         "/Settings"}};
                aResp->res.jsonValue["Boot"]["BootOptions"]["@odata.id"] =
                    "/redfish/v1/Systems/" +
                    std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                    "/BootOptions";
                aResp->res.jsonValue["Boot"]["BootOrder"] = bootOrder;
            }
            else
            {
                aResp->res.jsonValue["Boot"]["BootOrder"] = pendingBootOrder;
            }
        });

    BMCWEB_LOG_DEBUG("EXIT: Get boot order parameters");
}

/**
 * @brief Retrieves host secure boot properties over DBUS
 *
 * @param[in] aResp     Shared pointer for completing asynchronous calls.
 *
 * @return None.
 */
inline void getSecureBoot(const std::shared_ptr<bmcweb::AsyncResp>& aResp)
{
    BMCWEB_LOG_DEBUG("Get SecureBoot parameters");

    dbus::utility::getSubTree(
        "/xyz/openbmc_project/bios_config", int32_t(0),
        std::array<std::string_view, 1>{
            "xyz.openbmc_project.BIOSConfig.SecureBoot"},
        [aResp](const boost::system::error_code& ec,
                const dbus::utility::MapperGetSubTreeResponse& subtree) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG(
                    "DBUS response error on SecureBoot GetSubTree {}", ec);
                messages::internalError(aResp->res);
                return;
            }
            if (subtree.empty())
            {
                // This is an optional interface so just return
                // if there is no instance found
                BMCWEB_LOG_DEBUG("No instances found");
                return;
            }
            // SecureBoot object found
            aResp->res.jsonValue["SecureBoot"]["@odata.id"] =
                "/redfish/v1/Systems/" +
                std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) + "/SecureBoot";
        });

    BMCWEB_LOG_DEBUG("EXIT: Get SecureBoot parameters");
}

template <typename CallbackFunc>
inline void getChassisNMIStatus(CallbackFunc&& callback)
{
    dbus::utility::getProperty<bool>(
        "xyz.openbmc_project.Settings",
        "/xyz/openbmc_project/Control/ChassisCapabilities",
        "xyz.openbmc_project.Control.ChassisCapabilities", "ChassisNMIEnabled",
        [callback](const boost::system::error_code& ec, const bool enabledNmi) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error, {}", ec);
                callback(false);
                return;
            }

            callback(enabledNmi);
            return;
        });
}

inline std::string decodeSystemdName(const std::string& input)
{
    std::string decoded = input;

    // Convert "_6f" to "o"
    decoded = std::regex_replace(decoded, std::regex("_6f"), "o");
    // Convert "_2d" to "-"
    decoded = std::regex_replace(decoded, std::regex("_2d"), "-");

    return decoded;
}

// Helper function to handle individual service state changes
// Example: Backwards compatibility wrapper for SSH serial console
// setProtocolServiceEnabled(asyncResp, std::span{protocolToDBusForSystems},
// "SSH", true);
inline void setProtocolServiceEnabled(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    std::span<const std::pair<std::string_view, std::string_view>>
        protocolToDBus,
    const std::string_view& targetProtocol, const bool& serviceEnabled)
{
    BMCWEB_LOG_DEBUG("Set {} service state to: {}", targetProtocol,
                     serviceEnabled ? "enabled" : "disabled");

    for (const auto& [protocolName, dbusName] : protocolToDBus)
    {
        if (protocolName == targetProtocol)
        {
            constexpr std::array<std::string_view, 1> interfaces = {
                "xyz.openbmc_project.Control.Service.SocketAttributes"};

            dbus::utility::getSubTree(
                "/xyz/openbmc_project/control/service", 0, interfaces,
                [asyncResp, protocolName, dbusName, serviceEnabled](
                    const boost::system::error_code& ec,
                    const dbus::utility::MapperGetSubTreeResponse& subtree) {
                    if (ec || subtree.empty())
                    {
                        BMCWEB_LOG_ERROR(
                            "Error while getting manager service state");
                        redfish::messages::internalError(asyncResp->res);
                        return;
                    }

                    bool serviceFound = false;
                    // Iterate over all retrieved ObjectPaths.
                    for (const auto& [path, serviceMap] : subtree)
                    {
                        const std::string& service = serviceMap.begin()->first;
                        std::string serviceName =
                            std::filesystem::path(path).filename().string();
                        serviceName = decodeSystemdName(serviceName);

                        if (serviceName.find(dbusName) != std::string::npos)
                        {
                            serviceFound = true;
                            redfish::setDbusProperty(
                                asyncResp, "Running", service,
                                sdbusplus::message::object_path(path),
                                "xyz.openbmc_project.Control.Service.Attributes",
                                "Running", serviceEnabled);
                            break;
                        }
                    }

                    if (!serviceFound)
                    {
                        BMCWEB_LOG_ERROR("No matching service found for {}",
                                         protocolName);
                        redfish::messages::resourceNotFound(
                            asyncResp->res, "Service",
                            std::string(protocolName));
                    }
                });
        }
    }
}

/**
 * @brief Converts a D-Bus IPMI restriction mode to the IPMIHostInterface
 *        ServiceEnabled state.
 *
 * None, Allowlist and ProvisionedHostAllowlist map to enabled;
 * ProvisionedHostDisabled maps to disabled.
 *
 * @param[in] mode  The D-Bus restriction mode string.
 *
 * @return The ServiceEnabled state, or std::nullopt if the mode is unknown.
 */
inline std::optional<bool> ipmiHostInterfaceEnabledFromDbus(
    const std::string& mode)
{
    if (mode ==
        "xyz.openbmc_project.Control.Security.RestrictionMode.Modes.None")
    {
        return true;
    }
    if (mode ==
        "xyz.openbmc_project.Control.Security.RestrictionMode.Modes.Allowlist")
    {
        return true;
    }
    if (mode ==
        "xyz.openbmc_project.Control.Security.RestrictionMode.Modes.ProvisionedHostAllowlist")
    {
        return true;
    }
    if (mode ==
        "xyz.openbmc_project.Control.Security.RestrictionMode.Modes.ProvisionedHostDisabled")
    {
        return false;
    }
    return std::nullopt;
}

/**
 * @brief Converts a Redfish interface enabled state to the IPMI restriction
 * mode.
 *
 * @param[in] enabled The Redfish interface enabled state.
 *
 * @return The IPMI restriction mode.
 */
inline std::string ipmiHostInterfaceEnabledFromRedfish(const bool enabled)
{
    if (enabled)
    {
        return "xyz.openbmc_project.Control.Security.RestrictionMode.Modes.None";
    }
    return "xyz.openbmc_project.Control.Security.RestrictionMode.Modes.ProvisionedHostDisabled";
}

/**
 * @brief Populates ComputerSystem.IPMIHostInterface.ServiceEnabled from
 *        the IPMI restriction mode.
 *
 * @param[in] asyncResp   Shared pointer for generating response message.
 * @param[in] ec          The error code.
 * @param[in] modeStr     The D-Bus restriction mode string.
 */
inline void populateIPMIHostInterfaceData(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec, const std::string& modeStr)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("DBUS getProperty error: {}", ec);
        return;
    }
    std::optional<bool> serviceEnabled =
        ipmiHostInterfaceEnabledFromDbus(modeStr);
    if (!serviceEnabled)
    {
        BMCWEB_LOG_ERROR("Invalid restriction mode: {}", modeStr);
        return;
    }
    asyncResp->res.jsonValue["IPMIHostInterface"]["ServiceEnabled"] =
        *serviceEnabled;
}

/**
 * @brief Handles getting the restriction mode property from D-Bus.
 *
 * @param[in] asyncResp   Shared pointer for generating response message.
 * @param[in] path        The path of the restriction mode.
 * @param[in] interface   The interface of the restriction mode.
 * @param[in] property    The property of the restriction mode.
 * @param[in] ec          The error code from D-Bus call.
 * @param[in] object      The MapperGetObject result from D-Bus.
 */
inline void getIPMIHostInterfaceHandler(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& path, const std::string& interface,
    const std::string& property, const boost::system::error_code& ec,
    const dbus::utility::MapperGetObject& object)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("getDbusObject failed for {}: {}", path, ec);
        return;
    }

    if (object.empty())
    {
        // No restriction mode object (e.g. HMC): the in-band IPMI host
        // interface is simply not present, so skip without an error.
        BMCWEB_LOG_DEBUG("No restriction mode service for {}", path);
        return;
    }

    const std::string& service = object.begin()->first;
    dbus::utility::getProperty<std::string>(
        service, path, interface, property,
        std::bind_front(populateIPMIHostInterfaceData, asyncResp));
}

/**
 * @brief Gets the in-band IPMI host interface state if SSIF is present.
 *
 * @param[in] asyncResp   Shared pointer for generating response message.
 * @param[in] ec          The error code from D-Bus call.
 * @param[in] subTreePaths The MapperGetSubTreePaths result from D-Bus.
 */
inline void getIPMIHostInterfaceIfSsifPresent(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreePathsResponse& subTreePaths)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR(
            "DBUS getSubTreePaths error getting SSIF interface: {}", ec);
        return;
    }

    if (subTreePaths.empty())
    {
        BMCWEB_LOG_DEBUG("SSIF interface not found");
        return;
    }

    dbus::utility::getDbusObject(
        nvidia_ipmi::restrictionModePath,
        nvidia_ipmi::restrictionModeInterfaceArray,
        std::bind_front(getIPMIHostInterfaceHandler, asyncResp,
                        nvidia_ipmi::restrictionModePath,
                        nvidia_ipmi::restrictionModeInterface,
                        nvidia_ipmi::restrictionModeProperty));
}

/**
 * @brief Populates the in-band IPMI (SSIF) host interface state.
 *
 * @param[in] asyncResp   Shared pointer for generating response message.
 */
inline void getIPMIHostInterface(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    dbus::utility::getSubTreePaths(
        "/xyz/openbmc_project/Ipmi", 0, nvidia_ipmi::ssifInterfaceArray,
        std::bind_front(getIPMIHostInterfaceIfSsifPresent, asyncResp));
}

/**
 * @brief Handles setting the restriction mode property on D-Bus.
 *
 * @param[in] asyncResp   Shared pointer for generating response message.
 * @param[in] path        The path of the restriction mode.
 * @param[in] interface   The interface of the restriction mode.
 * @param[in] property    The property of the restriction mode.
 * @param[in] dbusMode    The D-Bus restriction mode to set.
 * @param[in] ec          The error code from D-Bus call.
 * @param[in] object      The MapperGetObject result from D-Bus.
 */
inline void setIPMIHostInterfaceHandler(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& path, const std::string& interface,
    const std::string& property, const std::string& dbusMode,
    const boost::system::error_code& ec,
    const dbus::utility::MapperGetObject& object)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("getDbusObject failed for {}: {}", path, ec);
        messages::internalError(asyncResp->res);
        return;
    }

    if (object.empty())
    {
        // No restriction mode object (e.g. HMC): the in-band IPMI host
        // interface is not present, so report it as a missing resource.
        BMCWEB_LOG_DEBUG("No restriction mode service for {}", path);
        messages::resourceNotFound(asyncResp->res, "IPMIHostInterface", path);
        return;
    }

    const std::string& service = object.begin()->first;
    redfish::setDbusProperty(asyncResp, "IPMIHostInterface/ServiceEnabled",
                             service, sdbusplus::message::object_path(path),
                             interface, property, dbusMode);
}

/**
 * @brief Sets the in-band IPMI host interface state if SSIF is present.
 *
 * BEHAVIOR 2: when the in-band IPMI host interface is set to enabled the IPMI
 * restriction mode is set to None; when it is set to disabled the restriction
 * mode is set to ProvisionedHostDisabled.
 *
 * @param[in] asyncResp       Shared pointer for generating response message.
 * @param[in] serviceEnabled  Desired state of the in-band IPMI host interface.
 * @param[in] ec              The error code from D-Bus call.
 * @param[in] subTreePaths    The MapperGetSubTreePaths result from D-Bus.
 */
inline void setIPMIHostInterfaceIfSsifPresent(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const bool serviceEnabled, const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreePathsResponse& subTreePaths)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR(
            "DBUS getSubTreePaths error getting SSIF interface: {}", ec);
        messages::internalError(asyncResp->res);
        return;
    }

    if (subTreePaths.empty())
    {
        BMCWEB_LOG_ERROR("SSIF interface not found");
        messages::resourceNotFound(asyncResp->res, "IPMIHostInterface", "SSIF");
        return;
    }

    std::string dbusMode = ipmiHostInterfaceEnabledFromRedfish(serviceEnabled);
    dbus::utility::getDbusObject(
        nvidia_ipmi::restrictionModePath,
        nvidia_ipmi::restrictionModeInterfaceArray,
        std::bind_front(setIPMIHostInterfaceHandler, asyncResp,
                        nvidia_ipmi::restrictionModePath,
                        nvidia_ipmi::restrictionModeInterface,
                        nvidia_ipmi::restrictionModeProperty, dbusMode));
}

/**
 * @brief Sets the in-band IPMI (SSIF) host interface state.
 *
 * @param[in] asyncResp       Shared pointer for generating response message.
 * @param[in] serviceEnabled  Desired state of the in-band IPMI host interface.
 */
inline void setIPMIHostInterface(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const bool serviceEnabled)
{
    dbus::utility::getSubTreePaths(
        "/xyz/openbmc_project/Ipmi", 0, nvidia_ipmi::ssifInterfaceArray,
        std::bind_front(setIPMIHostInterfaceIfSsifPresent, asyncResp,
                        serviceEnabled));
}

/* @brief Chassis inventory subtree searched for the BIOS object */
constexpr const char* biosInventorySearchPath =
    "/xyz/openbmc_project/inventory";
/* @brief Revision interface containing the SMBIOS Type 0 BIOS version */
constexpr const char* biosRevisionInterface =
    "xyz.openbmc_project.Inventory.Decorator.Revision";

using BiosVersionCallback =
    std::function<void(const boost::system::error_code&, const std::string&)>;

/**
 * @brief Handles the BIOS version subtree from D-Bus.
 *
 * @param[in] callback  Callback to process the BIOS version result.
 * @param[in] ec        The error code from D-Bus call.
 * @param[in] subtree   The MapperGetSubTree result from D-Bus.
 */
inline void biosVersionSubTreeHandler(
    BiosVersionCallback&& callback, const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreeResponse& subtree)
{
    if (!callback)
    {
        return;
    }

    if (ec)
    {
        callback(ec, {});
        return;
    }

    auto biosPathIt = std::ranges::find_if(subtree, [](const auto& object) {
        return sdbusplus::message::object_path(object.first).filename() ==
               "bios";
    });
    if (biosPathIt == subtree.end() || biosPathIt->second.empty())
    {
        callback(boost::system::errc::make_error_code(
                     boost::system::errc::no_such_file_or_directory),
                 {});
        return;
    }

    const std::string& biosPath = biosPathIt->first;
    const std::string& service = biosPathIt->second.front().first;
    dbus::utility::getProperty<std::string>(
        service, biosPath, biosRevisionInterface, "Version",
        std::move(callback));
}

/**
 * @brief Populates the BIOS version from the SMBIOS inventory.
 *
 * @param[in] callback  Callback to process the SMBIOS BIOS lookup result.
 */
inline void populateBiosVersion(BiosVersionCallback&& callback)
{
    constexpr std::array<std::string_view, 1> interfaces = {
        biosRevisionInterface};
    dbus::utility::getSubTree(
        biosInventorySearchPath, 0, interfaces,
        [callback = std::move(callback)](
            const boost::system::error_code& ec,
            const dbus::utility::MapperGetSubTreeResponse& subtree) mutable {
            biosVersionSubTreeHandler(std::move(callback), ec, subtree);
        });
}

/**
 * @brief Retrieves the BIOS version from the SMBIOS inventory.
 *
 * @param[in] asyncResp  Shared pointer for generating response message.
 */
inline void getBiosVersion(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    populateBiosVersion([asyncResp](const boost::system::error_code& ec,
                                    const std::string& biosVersion) {
        if (ec)
        {
            BMCWEB_LOG_ERROR(
                "Failed to retrieve BIOS version from SMBIOS inventory: {}",
                ec);
            return;
        }
        asyncResp->res.jsonValue["BiosVersion"] = biosVersion;
    });
}

} // namespace nvidia_systems_utils
} // namespace redfish
