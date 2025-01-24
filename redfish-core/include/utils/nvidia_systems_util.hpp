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
                        asyncResp->res, "Service", std::string(protocolName));
                }
            });
        }
    }
}

} // namespace nvidia_systems_utils
} // namespace redfish
