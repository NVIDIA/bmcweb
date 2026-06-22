#pragma once

#include "dbus_utility.hpp"

#include <sdbusplus/message.hpp>

#include <string>
#include <string_view>
#include <vector>
namespace dbus
{

namespace utility
{
using GetSubTreeType = std::vector<
    std::pair<std::string,
              std::vector<std::pair<std::string, std::vector<std::string>>>>>;

void getAllNameSpaceObjects(
    const std::string& service, const sdbusplus::object_path& path,
    const std::string& interfaces, const std::string& namespaceName,
    const std::string& filter,
    std::function<void(const boost::system::error_code&,
                       const ManagedObjectType&)>&& callback);

template <typename Callback>
inline void findAssociations(const std::string& path, Callback&& callbackIn)
{
    dbus::utility::getProperty<std::vector<std::string>>(
        "xyz.openbmc_project.ObjectMapper", path,
        "xyz.openbmc_project.Association", "endpoints",
        [callback{std::forward<Callback>(callbackIn)}](
            const boost::system::error_code ec,
            const std::vector<std::string>& resp) { callback(ec, resp); });
}

void systemdReload();
void systemdRestartUnit(std::string_view unit, const char* mode);
} // namespace utility
} // namespace dbus
