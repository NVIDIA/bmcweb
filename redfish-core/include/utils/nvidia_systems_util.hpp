#pragma once

namespace redfish
{
namespace nvidia_systems_utils
{
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
} // namespace nvidia_systems_utils
} // namespace redfish
