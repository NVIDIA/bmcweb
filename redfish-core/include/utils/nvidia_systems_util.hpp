#pragma once
#include "dbus_utility.hpp"
#include "utils/dbus_utils.hpp"
#include "utils/json_utils.hpp"
#include "utils/pcie_util.hpp"
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
inline void
    getChassisNMIStatus(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp](const boost::system::error_code& ec,
                    const std::variant<bool>& resp) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error, {}", ec);
                return;
            }

            bool enabledNmi = std::get<bool>(resp);
            if (enabledNmi == true)
            {
                asyncResp->res.jsonValue["Parameters"][0]["AllowableValues"]
                    .emplace_back("Nmi");
            }
        },
        "xyz.openbmc_project.Settings",
        "/xyz/openbmc_project/Control/ChassisCapabilities",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Control.ChassisCapabilities", "ChassisNMIEnabled");
}

/**
 * @brief Populate objects from D-Bus object of entity-manager
 *
 * @param[in] aResp  - Shared pointer for completing asynchronous calls.
 *
 * @return None.
 */
inline void
    populateFromEntityManger(const std::shared_ptr<bmcweb::AsyncResp>& aResp)
{
    crow::connections::systemBus->async_method_call(
        [aResp](const boost::system::error_code ec,
                const std::vector<std::pair<
                    std::string, std::variant<std::string>>>& propertiesList) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error for "
                                 "Populate from entity manager ");
                return;
            }
            for (auto& property : propertiesList)
            {
                const std::string& propertyName = property.first;
                if (propertyName == "SKU")
                {
                    const std::string* sku =
                        std::get_if<std::string>(&property.second);
                    if (sku != nullptr)
                    {
                        aResp->res.jsonValue["SKU"] = *sku;
                    }
                }
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
        },
        entityMangerService, card1Path, "org.freedesktop.DBus.Properties",
        "GetAll", "xyz.openbmc_project.Inventory.Decorator.Asset");
    crow::connections::systemBus->async_method_call(
        [aResp](const boost::system::error_code ec,
                const std::variant<std::string>& uuid) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error for "
                                 "Trying to get UUID");
                return;
            }
            aResp->res.jsonValue["UUID"] = *std::get_if<std::string>(&uuid);
        },
        entityMangerService, card1Path, "org.freedesktop.DBus.Properties",
        "Get", "xyz.openbmc_project.Common.UUID", "UUID");
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
        if (isSettingsResource == false)
        {
            sdbusplus::asio::setProperty(
                *crow::connections::systemBus,
                "xyz.openbmc_project.BIOSConfigManager",
                "/xyz/openbmc_project/bios_config/manager",
                "xyz.openbmc_project.BIOSConfig.BootOrder", "BootOrder",
                bootOrder, [aResp](const boost::system::error_code ec) {
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
            sdbusplus::asio::getProperty<std::vector<std::string>>(
                *crow::connections::systemBus,
                "xyz.openbmc_project.BIOSConfigManager",
                "/xyz/openbmc_project/bios_config/manager",
                "xyz.openbmc_project.BIOSConfig.BootOrder", "BootOrder",
                [aResp,
                 bootOrder](const boost::system::error_code ec,
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

                    sdbusplus::asio::setProperty(
                        *crow::connections::systemBus,
                        "xyz.openbmc_project.BIOSConfigManager",
                        "/xyz/openbmc_project/bios_config/manager",
                        "xyz.openbmc_project.BIOSConfig.BootOrder",
                        "PendingBootOrder", bootOrder,
                        [aResp](const boost::system::error_code ec2) {
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

    if (isSettingsResource == false)
    {
        // Only BIOS is allowed to patch active BootOrder
        privilege_utils::isBiosPrivilege(
            req, [aResp, setBootOrderFunc](const boost::system::error_code ec,
                                           const bool isBios) {
                if (ec || isBios == false)
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

    sdbusplus::asio::getAllProperties(
        *crow::connections::systemBus, "xyz.openbmc_project.BIOSConfigManager",
        "/xyz/openbmc_project/bios_config/manager",
        "xyz.openbmc_project.BIOSConfig.BootOrder",
        [aResp, isSettingsResource](
            const boost::system::error_code ec,
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
            for (auto& [propertyName, propertyVariant] : properties)
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
            if (isSettingsResource == false)
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

// TODO: Move to different file
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

    crow::connections::systemBus->async_method_call(
        [aResp](const boost::system::error_code ec,
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
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/xyz/openbmc_project/bios_config", int32_t(0),
        std::array<const char*, 1>{
            "xyz.openbmc_project.BIOSConfig.SecureBoot"});

    BMCWEB_LOG_DEBUG("EXIT: Get SecureBoot parameters");
}

} // namespace nvidia_systems_utils
} // namespace redfish
