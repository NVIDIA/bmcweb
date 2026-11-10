#include "dbus_utility.hpp"
#include "ethernet.hpp"
#include "http_response.hpp"

#include <nlohmann/json.hpp>
#include <sdbusplus/message/native_types.hpp>

#include <cstddef>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace redfish
{
namespace
{

using ::testing::ElementsAre;
using ::testing::IsEmpty;

constexpr const char* ipv6Protocol =
    "xyz.openbmc_project.Network.IP.Protocol.IPv6";

void addStaticGateway(dbus::utility::ManagedObjectType& objects,
                      const std::string& path, const std::string& gateway,
                      const std::string& protocol = ipv6Protocol)
{
    dbus::utility::DBusPropertiesMap props;
    props.emplace_back("Gateway", gateway);
    props.emplace_back("ProtocolType", protocol);
    dbus::utility::DBusInterfacesMap interfaces;
    interfaces.emplace_back("xyz.openbmc_project.Network.StaticGateway",
                            std::move(props));
    objects.emplace_back(sdbusplus::object_path(path), std::move(interfaces));
}

dbus::utility::ManagedObjectType parentAndVlanStaticGateways()
{
    dbus::utility::ManagedObjectType objects;
    addStaticGateway(objects, "/xyz/openbmc_project/network/eth0/parentgw",
                     "fe80::1");
    addStaticGateway(objects, "/xyz/openbmc_project/network/eth0_100/vlangw",
                     "fe80::100");
    return objects;
}

TEST(Ethernet, parseAddressesEmpty)
{
    std::vector<std::variant<nlohmann::json::object_t, std::nullptr_t>> addr;
    std::vector<IPv4AddressData> existingAddr;

    std::vector<AddressPatch> addrOut;
    std::string gatewayOut;

    crow::Response res;

    EXPECT_TRUE(parseAddresses(addr, existingAddr, res, addrOut, gatewayOut));
    EXPECT_THAT(addrOut, IsEmpty());
    EXPECT_THAT(gatewayOut, IsEmpty());
}

// Create full entry with all fields
TEST(Ethernet, parseAddressesCreateOne)
{
    std::vector<std::variant<nlohmann::json::object_t, std::nullptr_t>> addr;
    nlohmann::json::object_t eth;
    eth["Address"] = "1.1.1.2";
    eth["Gateway"] = "1.1.1.1";
    eth["SubnetMask"] = "255.255.255.0";
    addr.emplace_back(eth);
    std::vector<IPv4AddressData> existingAddr;

    std::vector<AddressPatch> addrOut;
    std::string gatewayOut;

    crow::Response res;

    EXPECT_TRUE(parseAddresses(addr, existingAddr, res, addrOut, gatewayOut));
    EXPECT_EQ(addrOut.size(), 1);
    EXPECT_EQ(addrOut[0].address, "1.1.1.2");
    EXPECT_EQ(addrOut[0].gateway, "1.1.1.1");
    EXPECT_EQ(addrOut[0].prefixLength, 24);
    EXPECT_EQ(addrOut[0].existingDbusId, "");
    EXPECT_EQ(addrOut[0].operation, AddrChange::Update);
    EXPECT_EQ(gatewayOut, "1.1.1.1");
}

// Missing gateway should default to no gateway
TEST(Ethernet, parseAddressesCreateOneNoGateway)
{
    std::vector<std::variant<nlohmann::json::object_t, std::nullptr_t>> addr;
    nlohmann::json::object_t eth;
    eth["Address"] = "1.1.1.2";
    eth["SubnetMask"] = "255.255.255.0";
    addr.emplace_back(eth);
    std::vector<IPv4AddressData> existingAddr;

    std::vector<AddressPatch> addrOut;
    std::string gatewayOut;

    crow::Response res;

    EXPECT_TRUE(parseAddresses(addr, existingAddr, res, addrOut, gatewayOut));
    EXPECT_EQ(addrOut.size(), 1);
    EXPECT_EQ(addrOut[0].address, "1.1.1.2");
    EXPECT_EQ(addrOut[0].gateway, "");
    EXPECT_EQ(addrOut[0].prefixLength, 24);
    EXPECT_EQ(addrOut[0].existingDbusId, "");
    EXPECT_EQ(addrOut[0].operation, AddrChange::Update);
    EXPECT_EQ(gatewayOut, "");
}

// Create two entries with conflicting gateways.
TEST(Ethernet, conflictingGatewaysNew)
{
    std::vector<std::variant<nlohmann::json::object_t, std::nullptr_t>> addr;
    nlohmann::json::object_t eth;
    eth["Address"] = "1.1.1.2";
    eth["Gateway"] = "1.1.1.1";
    eth["SubnetMask"] = "255.255.255.0";
    addr.emplace_back(eth);
    eth["Gateway"] = "1.1.1.5";
    addr.emplace_back(eth);
    std::vector<IPv4AddressData> existingAddr;

    std::vector<AddressPatch> addrOut;
    std::string gatewayOut;

    crow::Response res;

    EXPECT_FALSE(parseAddresses(addr, existingAddr, res, addrOut, gatewayOut));
}

// Create full entry with all fields
TEST(Ethernet, conflictingGatewaysExisting)
{
    std::vector<std::variant<nlohmann::json::object_t, std::nullptr_t>> addr;
    nlohmann::json::object_t eth;
    addr.emplace_back(eth);
    eth["Address"] = "1.1.1.2";
    eth["Gateway"] = "1.1.1.1";
    eth["SubnetMask"] = "255.255.255.0";
    addr.emplace_back(eth);
    std::vector<IPv4AddressData> existingAddr;
    IPv4AddressData& existing = existingAddr.emplace_back();
    existing.id = "my_ip_id";
    existing.origin = "Static";
    existing.gateway = "192.168.1.1";

    std::vector<AddressPatch> addrOut;
    std::string gatewayOut;

    crow::Response res;

    EXPECT_FALSE(parseAddresses(addr, existingAddr, res, addrOut, gatewayOut));
}

// Missing address should fail
TEST(Ethernet, parseMissingAddress)
{
    std::vector<std::variant<nlohmann::json::object_t, std::nullptr_t>> addr;
    nlohmann::json::object_t eth;
    eth["SubnetMask"] = "255.255.255.0";
    addr.emplace_back(eth);
    std::vector<IPv4AddressData> existingAddr;

    std::vector<AddressPatch> addrOut;
    std::string gatewayOut;

    crow::Response res;

    EXPECT_FALSE(parseAddresses(addr, existingAddr, res, addrOut, gatewayOut));
}

// Missing subnet should fail
TEST(Ethernet, parseAddressesMissingSubnet)
{
    std::vector<std::variant<nlohmann::json::object_t, std::nullptr_t>> addr;
    nlohmann::json::object_t eth;
    eth["Address"] = "1.1.1.2";
    addr.emplace_back(eth);
    std::vector<IPv4AddressData> existingAddr;

    std::vector<AddressPatch> addrOut;
    std::string gatewayOut;

    crow::Response res;

    EXPECT_FALSE(parseAddresses(addr, existingAddr, res, addrOut, gatewayOut));
}

// With one existing address, and a null, it should request deletion
// and clear gateway
TEST(Ethernet, parseAddressesDeleteExistingOnNull)
{
    std::vector<std::variant<nlohmann::json::object_t, std::nullptr_t>> addr;
    addr.emplace_back(nullptr);
    std::vector<IPv4AddressData> existingAddr;
    IPv4AddressData& existing = existingAddr.emplace_back();
    existing.id = "my_ip_id";
    existing.origin = "Static";

    std::vector<AddressPatch> addrOut;
    std::string gatewayOut;

    crow::Response res;

    EXPECT_TRUE(parseAddresses(addr, existingAddr, res, addrOut, gatewayOut));
    EXPECT_EQ(addrOut.size(), 1);
    EXPECT_EQ(addrOut[0].existingDbusId, "my_ip_id");
    EXPECT_EQ(addrOut[0].operation, AddrChange::Delete);
    EXPECT_EQ(gatewayOut, "");
}

TEST(Ethernet, parseAddressesDeleteExistingOnShortLength)
{
    // With one existing address, and an input of len(0) it should request
    // deletion and clear gateway
    std::vector<std::variant<nlohmann::json::object_t, std::nullptr_t>> addr;
    std::vector<IPv4AddressData> existingAddr;
    IPv4AddressData& existing = existingAddr.emplace_back();
    existing.id = "my_ip_id";
    existing.origin = "Static";

    std::vector<AddressPatch> addrOut;
    std::string gatewayOut;

    crow::Response res;

    EXPECT_TRUE(parseAddresses(addr, existingAddr, res, addrOut, gatewayOut));
    EXPECT_EQ(addrOut.size(), 1);
    EXPECT_EQ(addrOut[0].existingDbusId, "my_ip_id");
    EXPECT_EQ(addrOut[0].operation, AddrChange::Delete);
    EXPECT_EQ(gatewayOut, "");
}

TEST(Ethernet, extractIPv6DefaultGatewayDataSkipsVlanOnParentPrefix)
{
    const dbus::utility::ManagedObjectType objects =
        parentAndVlanStaticGateways();
    std::vector<StaticGatewayData> gateways;

    ASSERT_TRUE(extractIPv6DefaultGatewayData("eth0", objects, gateways));
    ASSERT_EQ(gateways.size(), 1);
    EXPECT_EQ(gateways[0].id, "parentgw");
    EXPECT_EQ(gateways[0].gateway, "fe80::1");
    EXPECT_EQ(gateways[0].protocol, ipv6Protocol);
}

TEST(Ethernet, extractIPv6DefaultGatewayDataSelectsVlanInterface)
{
    const dbus::utility::ManagedObjectType objects =
        parentAndVlanStaticGateways();
    std::vector<StaticGatewayData> gateways;

    ASSERT_TRUE(extractIPv6DefaultGatewayData("eth0_100", objects, gateways));
    ASSERT_EQ(gateways.size(), 1);
    EXPECT_EQ(gateways[0].id, "vlangw");
    EXPECT_EQ(gateways[0].gateway, "fe80::100");
    EXPECT_EQ(gateways[0].protocol, ipv6Protocol);
}

TEST(Ethernet, extractIPv6DefaultGatewayDataIgnoresNestedPaths)
{
    dbus::utility::ManagedObjectType objects = parentAndVlanStaticGateways();
    addStaticGateway(objects, "/xyz/openbmc_project/network/eth0/nested/gw",
                     "fe80::99");
    std::vector<StaticGatewayData> gateways;

    ASSERT_TRUE(extractIPv6DefaultGatewayData("eth0", objects, gateways));
    ASSERT_EQ(gateways.size(), 1);
    EXPECT_EQ(gateways[0].id, "parentgw");
}

TEST(Ethernet, leftoverStaticIPv6EntriesAfterEmptyPatch)
{
    std::vector<IPv6AddressData> ipv6Data;
    IPv6AddressData& linkLocal = ipv6Data.emplace_back();
    linkLocal.id = "ll";
    linkLocal.origin = "LinkLocal";
    IPv6AddressData& static1 = ipv6Data.emplace_back();
    static1.id = "static1";
    static1.origin = "Static";
    IPv6AddressData& static2 = ipv6Data.emplace_back();
    static2.id = "static2";
    static2.origin = "Static";

    EXPECT_THAT(leftoverStaticIPv6Ids(ipv6Data.cbegin(), ipv6Data.cend()),
                ElementsAre("static1", "static2"));
}

TEST(Ethernet, leftoverStaticIPv6IdsSkipsConsumedEntry)
{
    std::vector<IPv6AddressData> ipv6Data;
    IPv6AddressData& static1 = ipv6Data.emplace_back();
    static1.id = "static1";
    static1.origin = "Static";
    IPv6AddressData& static2 = ipv6Data.emplace_back();
    static2.id = "static2";
    static2.origin = "Static";

    auto afterFirst = getNextStaticIpEntry(ipv6Data.cbegin(), ipv6Data.cend());
    ASSERT_NE(afterFirst, ipv6Data.cend());
    ++afterFirst;

    EXPECT_THAT(leftoverStaticIPv6Ids(afterFirst, ipv6Data.cend()),
                ElementsAre("static2"));
}

TEST(Ethernet, leftoverIPv6GatewaysAfterEmptyPatch)
{
    dbus::utility::ManagedObjectType objects = parentAndVlanStaticGateways();
    addStaticGateway(objects, "/xyz/openbmc_project/network/eth0/v4gw",
                     "1.1.1.1", "xyz.openbmc_project.Network.IP.Protocol.IPv4");
    std::vector<StaticGatewayData> gateways;

    ASSERT_TRUE(extractIPv6DefaultGatewayData("eth0", objects, gateways));
    ASSERT_EQ(gateways.size(), 2);

    EXPECT_THAT(leftoverIPv6GatewayIds(gateways.cbegin(), gateways.cend()),
                ElementsAre("parentgw"));
}

} // namespace
} // namespace redfish
