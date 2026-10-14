#include "async_resp.hpp"
#include "utils/nvidia_platform_power_cycle_utils.hpp"

#include <boost/beast/http/status.hpp>
#include <boost/system/errc.hpp>
#include <boost/system/error_code.hpp>

#include <cerrno>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace redfish::nvidia_platform_power_cycle
{
namespace
{
TEST(PlatformPowerCycleUtils, BuildsPerSystemObjectPath)
{
    EXPECT_EQ(getObjectPath(0).str,
              "/xyz/openbmc_project/control/power_cycle/host0");
    EXPECT_EQ(getObjectPath(7).str,
              "/xyz/openbmc_project/control/power_cycle/host7");
}

TEST(PlatformPowerCycleUtils, RequiresAnExactAdvertisedType)
{
    const Capabilities capabilities{
        "com.nvidia.Control.Platform.PowerCycle",
        {std::string(auxPowerCycle), std::string(fullPowerCycle)}};

    EXPECT_TRUE(supports(capabilities, auxPowerCycle));
    EXPECT_TRUE(supports(capabilities, fullPowerCycle));
    EXPECT_FALSE(supports(capabilities, auxPowerCycleForce));
    EXPECT_FALSE(supports(capabilities, "FullPowerCycle"));
}

TEST(PlatformPowerCycleUtils, ConvertsCapabilitiesToFullPowerCycleSupport)
{
    const Capabilities capabilities{"com.nvidia.Control.Platform.PowerCycle",
                                    {std::string(fullPowerCycle)}};

    bool callbackCalled = false;
    afterGetPowerCycleSupport(
        [&callbackCalled](const boost::system::error_code& ec, bool supported) {
            EXPECT_FALSE(ec);
            EXPECT_TRUE(supported);
            callbackCalled = true;
        },
        std::string(fullPowerCycle), {}, capabilities);

    EXPECT_TRUE(callbackCalled);
}

TEST(PlatformPowerCycleUtils, MissingCapabilitiesReportUnsupported)
{
    bool callbackCalled = false;
    afterGetPowerCycleSupport(
        [&callbackCalled](const boost::system::error_code& ec, bool supported) {
            EXPECT_FALSE(ec);
            EXPECT_FALSE(supported);
            callbackCalled = true;
        },
        std::string(fullPowerCycle), {}, std::nullopt);

    EXPECT_TRUE(callbackCalled);
}

TEST(PlatformPowerCycleUtils, EmptyMapperSubTreeReportsProviderAbsent)
{
    bool callbackCalled = false;
    afterGetPowerCycleSubTree(
        getObjectPath(0),
        [&callbackCalled](const boost::system::error_code& ec,
                          const std::optional<Capabilities>& capabilities) {
            EXPECT_FALSE(ec);
            EXPECT_FALSE(capabilities);
            callbackCalled = true;
        },
        {}, {});

    EXPECT_TRUE(callbackCalled);
}

TEST(PlatformPowerCycleUtils, MapperSubTreeRequiresExactHostPath)
{
    const dbus::utility::MapperGetSubTreeResponse subtree = {
        {getObjectPath(1).str,
         {{"com.nvidia.Control.Platform.PowerCycle",
           {std::string(interface)}}}}};

    bool callbackCalled = false;
    afterGetPowerCycleSubTree(
        getObjectPath(0),
        [&callbackCalled](const boost::system::error_code& ec,
                          const std::optional<Capabilities>& capabilities) {
            EXPECT_FALSE(ec);
            EXPECT_FALSE(capabilities);
            callbackCalled = true;
        },
        {}, subtree);

    EXPECT_TRUE(callbackCalled);
}

TEST(PlatformPowerCycleUtils, MapperFailureDoesNotReportProviderAbsent)
{
    const boost::system::error_code mapperError =
        boost::system::errc::make_error_code(boost::system::errc::io_error);

    bool callbackCalled = false;
    afterGetPowerCycleSubTree(
        getObjectPath(0),
        [&callbackCalled,
         &mapperError](const boost::system::error_code& ec,
                       const std::optional<Capabilities>& capabilities) {
            EXPECT_EQ(ec, mapperError);
            EXPECT_FALSE(capabilities);
            callbackCalled = true;
        },
        mapperError, {});

    EXPECT_TRUE(callbackCalled);
}

TEST(PlatformPowerCycleUtils, MultipleProvidersReportAnError)
{
    const dbus::utility::MapperGetSubTreeResponse subtree = {
        {getObjectPath(0).str,
         {{"com.nvidia.Control.Platform.PowerCycle.0",
           {std::string(interface)}},
          {"com.nvidia.Control.Platform.PowerCycle.1",
           {std::string(interface)}}}}};

    bool callbackCalled = false;
    afterGetPowerCycleSubTree(
        getObjectPath(0),
        [&callbackCalled](const boost::system::error_code& ec,
                          const std::optional<Capabilities>& capabilities) {
            EXPECT_EQ(ec, boost::system::errc::state_not_recoverable);
            EXPECT_FALSE(capabilities);
            callbackCalled = true;
        },
        {}, subtree);

    EXPECT_TRUE(callbackCalled);
}

TEST(PlatformPowerCycleUtils, MissingProviderRejectsFullPowerCycleAsUnsupported)
{
    auto response = std::make_shared<bmcweb::AsyncResp>();

    afterGetCapabilitiesForRequest(
        response, getObjectPath(0), std::string(fullPowerCycle),
        "FullPowerCycle", "ComputerSystem.Reset", {}, std::nullopt);

    EXPECT_EQ(response->res.result(), boost::beast::http::status::bad_request);
    EXPECT_EQ(response->res.jsonValue["error"]["code"],
              "Base.1.19.ActionParameterValueNotInList");
}

constexpr const char* cpuObject =
    "/xyz/openbmc_project/control/power_cycle/cpu0";

TEST(ComponentPowerCycle, SingleAssociatedObjectIsSelected)
{
    const dbus::utility::MapperGetSubTreeResponse subtree = {
        {cpuObject,
         {{"com.nvidia.Control.Platform.PowerCycle",
           {std::string(interface)}}}}};

    std::optional<ComponentPowerCycle> component =
        findComponentPowerCycle(subtree);

    ASSERT_TRUE(component);
    if (!component)
    {
        return;
    }
    EXPECT_EQ(component->path, cpuObject);
    EXPECT_EQ(component->capabilities.service,
              "com.nvidia.Control.Platform.PowerCycle");
    EXPECT_TRUE(component->capabilities.types.empty());
}

TEST(ComponentPowerCycle, NoOrSeveralObjectsSelectNothing)
{
    EXPECT_FALSE(findComponentPowerCycle({}));

    const dbus::utility::MapperGetSubTreeResponse twoObjects = {
        {cpuObject,
         {{"com.nvidia.Control.Platform.PowerCycle",
           {std::string(interface)}}}},
        {"/xyz/openbmc_project/control/power_cycle/mcu0",
         {{"com.nvidia.Control.Platform.PowerCycle",
           {std::string(interface)}}}}};
    EXPECT_FALSE(findComponentPowerCycle(twoObjects));

    const dbus::utility::MapperGetSubTreeResponse twoServices = {
        {cpuObject,
         {{"com.nvidia.Control.Platform.PowerCycle.0",
           {std::string(interface)}},
          {"com.nvidia.Control.Platform.PowerCycle.1",
           {std::string(interface)}}}}};
    EXPECT_FALSE(findComponentPowerCycle(twoServices));
}

TEST(ComponentPowerCycle, MissingAssociationReportsNoObject)
{
    bool callbackCalled = false;
    afterGetComponentPowerCycleSubTree(
        [&callbackCalled](const boost::system::error_code& ec,
                          const std::optional<ComponentPowerCycle>& component) {
            EXPECT_FALSE(ec);
            EXPECT_FALSE(component);
            callbackCalled = true;
        },
        boost::system::error_code(EBADR, boost::system::system_category()), {});

    EXPECT_TRUE(callbackCalled);
}

TEST(ComponentPowerCycle, MapperFailureIsReported)
{
    const boost::system::error_code mapperError =
        boost::system::errc::make_error_code(boost::system::errc::io_error);

    bool callbackCalled = false;
    afterGetComponentPowerCycleSubTree(
        [&callbackCalled,
         &mapperError](const boost::system::error_code& ec,
                       const std::optional<ComponentPowerCycle>& component) {
            EXPECT_EQ(ec, mapperError);
            EXPECT_FALSE(component);
            callbackCalled = true;
        },
        mapperError, {});

    EXPECT_TRUE(callbackCalled);
}

TEST(ComponentPowerCycle, EmptySubTreeReportsNoObject)
{
    bool callbackCalled = false;
    afterGetComponentPowerCycleSubTree(
        [&callbackCalled](const boost::system::error_code& ec,
                          const std::optional<ComponentPowerCycle>& component) {
            EXPECT_FALSE(ec);
            EXPECT_FALSE(component);
            callbackCalled = true;
        },
        {}, {});

    EXPECT_TRUE(callbackCalled);
}

const ComponentPowerCycle cpuObjectWithoutProperties{
    cpuObject, {"com.nvidia.Control.Platform.PowerCycle", {}}, {}};

TEST(ComponentPowerCycle, ReadPropertiesAreReported)
{
    const dbus::utility::DBusPropertiesMap properties = {
        {"SupportedPowerCycleTypes",
         std::vector<std::string>{std::string(componentPowerCycle)}},
        {"Description", std::string("Power cycle the CPU and SSD.")}};

    bool callbackCalled = false;
    afterGetComponentPowerCycleProperties(
        [&callbackCalled](const boost::system::error_code& ec,
                          const std::optional<ComponentPowerCycle>& component) {
            EXPECT_FALSE(ec);
            ASSERT_TRUE(component);
            if (!component)
            {
                return;
            }
            EXPECT_EQ(component->path, cpuObject);
            EXPECT_TRUE(supports(component->capabilities, componentPowerCycle));
            EXPECT_EQ(component->description, "Power cycle the CPU and SSD.");
            callbackCalled = true;
        },
        cpuObjectWithoutProperties, {}, properties);

    EXPECT_TRUE(callbackCalled);
}

TEST(ComponentPowerCycle, MissingDescriptionIsEmpty)
{
    const dbus::utility::DBusPropertiesMap properties = {
        {"SupportedPowerCycleTypes",
         std::vector<std::string>{std::string(componentPowerCycle)}}};

    bool callbackCalled = false;
    afterGetComponentPowerCycleProperties(
        [&callbackCalled](const boost::system::error_code& ec,
                          const std::optional<ComponentPowerCycle>& component) {
            EXPECT_FALSE(ec);
            ASSERT_TRUE(component);
            if (!component)
            {
                return;
            }
            EXPECT_TRUE(supports(component->capabilities, componentPowerCycle));
            EXPECT_TRUE(component->description.empty());
            callbackCalled = true;
        },
        cpuObjectWithoutProperties, {}, properties);

    EXPECT_TRUE(callbackCalled);
}

TEST(ComponentPowerCycle, WrongPropertyTypeIsReported)
{
    const dbus::utility::DBusPropertiesMap properties = {
        {"Description", std::vector<std::string>{"not a string"}}};

    bool callbackCalled = false;
    afterGetComponentPowerCycleProperties(
        [&callbackCalled](const boost::system::error_code& ec,
                          const std::optional<ComponentPowerCycle>& component) {
            EXPECT_TRUE(ec);
            EXPECT_FALSE(component);
            callbackCalled = true;
        },
        cpuObjectWithoutProperties, {}, properties);

    EXPECT_TRUE(callbackCalled);
}

TEST(ComponentPowerCycle, PropertyReadFailureIsReported)
{
    const boost::system::error_code readError =
        boost::system::errc::make_error_code(boost::system::errc::io_error);

    bool callbackCalled = false;
    afterGetComponentPowerCycleProperties(
        [&callbackCalled,
         &readError](const boost::system::error_code& ec,
                     const std::optional<ComponentPowerCycle>& component) {
            EXPECT_EQ(ec, readError);
            EXPECT_FALSE(component);
            callbackCalled = true;
        },
        cpuObjectWithoutProperties, readError, {});

    EXPECT_TRUE(callbackCalled);
}
} // namespace
} // namespace redfish::nvidia_platform_power_cycle
