#pragma once

#include "bmcweb_config.h"

#include "async_resp.hpp"
#include "dbus_utility.hpp"

#include <boost/system/error_code.hpp>

#include <memory>
#include <string>

inline void afterSystemSpiInterfacesFound(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreePathsResponse& /*paths*/)
{
    if (ec)
    {
        // NO spi interfaces found.  This is fine.
        return;
    }
    nlohmann::json& oemActions = asyncResp->res.jsonValue["Actions"]["Oem"];

    // AuxPowerReset
    oemActions["#NvidiaSystem.VariableSpiErase"]["target"] = boost::urls::format(
        "/redfish/v1/Systems/{}/Actions/Oem/NvidiaProcessor.VariableSpiErase",
        chassisId);
}

inline void getSystemsOemNvidiaProperties(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& systemId)
{
    if constexpr (!BMCWEB_NVIDIA_OEM_PROPERTIES)
    {
        // Nothing to do if the option isn't enabled
        return;
    }

    std::array<std::string_view, 1> interfaces{"com.nvidia.SPI.SPI"};
    dbus::utility::getSubTreePaths(
        "/xyz/openbmc_project/inventory", 0, interfaces,
        std::bind_front(&afterSystemSpiInterfacesFound, asyncResp, systemId));
}
