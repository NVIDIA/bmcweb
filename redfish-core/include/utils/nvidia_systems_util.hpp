#pragma once

namespace redfish
{
namespace nvidia_systems_utils
{
template <typename CallbackFunc>
inline void getChassisNMIStatus(CallbackFunc&& callback)
{
    sdbusplus::asio::getProperty<bool>(
        *crow::connections::systemBus, "xyz.openbmc_project.Settings",
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
} // namespace nvidia_systems_utils
} // namespace redfish
