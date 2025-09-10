#include "nvidia_dbus_utility.hpp"

namespace dbus
{

namespace utility
{
void getAllNameSpaceObjects(
    const std::string& service, const sdbusplus::message::object_path& path,
    const std::string& interfaces, const std::string& namespaceName,
    const std::string& filter,
    std::function<void(const boost::system::error_code&,
                       const ManagedObjectType&)>&& callback)
{
    crow::connections::systemBus->async_method_call(
        [callback{std::move(callback)}](const boost::system::error_code& ec,
                                        const ManagedObjectType& objects) {
            callback(ec, objects);
        },
        service, path, interfaces, "GetAll", namespaceName, filter);
}

void systemdReload()
{
    auto method = crow::connections::systemBus->new_method_call(
        "org.freedesktop.systemd1", "/org/freedesktop/systemd1",
        "org.freedesktop.systemd1.Manager", "Reload");

    crow::connections::systemBus->call_noreply(method);
}

void systemdRestartUnit(std::string_view unit, const char* mode)
{
    std::string path("/org/freedesktop/systemd1/unit/");
    path.append(unit);
    auto method = crow::connections::systemBus->new_method_call(
        "org.freedesktop.systemd1", path.c_str(),
        "org.freedesktop.systemd1.Unit", "Restart");

    method.append(mode);

    crow::connections::systemBus->call_noreply(method);
}

} // namespace utility
} // namespace dbus