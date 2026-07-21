// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#pragma once

#include "async_resp.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "generated/enums/nvidia_network_protocol.hpp"
#include "http_response.hpp"
#include "logging.hpp"
#include "utils/dbus_utils.hpp"

#include <boost/system/error_code.hpp>
#include <nlohmann/json.hpp>
#include <sdbusplus/message/native_types.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace redfish
{

using AuthMethod =
    std::variant<std::string, nlohmann::json::object_t, std::nullptr_t>;

constexpr const char* openocdPortForwardService =
    "com.nvidia.openocdportforward";
constexpr const char* openocdPortForwardPath = "/com/nvidia/openocdportforward";
constexpr const char* openocdPortForwardInterface =
    "xyz.openbmc_project.Object.Enable";
constexpr const char* openocdPortForwardProperty = "Enabled";

inline void afterGetOemNvidiaOpenOCDPortForwardEnabled(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec, const bool& enabled)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("Failed to read {}: {}", openocdPortForwardProperty,
                         ec.message());
        messages::internalError(asyncResp->res);
        return;
    }
    // Declare the parent NvidiaNetworkProtocol OEM type so its properties
    // resolve during Redfish schema validation.
    asyncResp->res.jsonValue["Oem"]["Nvidia"]["@odata.type"] =
        "#NvidiaNetworkProtocol.v1_2_0.NvidiaNetworkProtocol";
    asyncResp->res.jsonValue["Oem"]["Nvidia"]["OpenOCDPortForward"]["Enable"] =
        enabled;
}

inline void getOemNvidiaOpenOCDPortForward(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    constexpr std::array<std::string_view, 1> interfaces = {
        openocdPortForwardInterface};

    dbus::utility::getDbusObject(
        openocdPortForwardPath, interfaces,
        [asyncResp](const boost::system::error_code& ec,
                    const dbus::utility::MapperGetObject& mapperResponse) {
            if (ec || mapperResponse.empty())
            {
                BMCWEB_LOG_DEBUG(
                    "OpenOCDPortForward backend not registered: {}",
                    ec.message());
                return;
            }
            const std::string& service = mapperResponse[0].first;
            dbus::utility::getProperty<bool>(
                service, openocdPortForwardPath, openocdPortForwardInterface,
                openocdPortForwardProperty,
                [asyncResp](const boost::system::error_code& ec2,
                            const bool& enabled) {
                    afterGetOemNvidiaOpenOCDPortForwardEnabled(asyncResp, ec2,
                                                               enabled);
                });
        });
}

inline void setOemNvidiaOpenOCDPortForward(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp, const bool value)
{
    setDbusProperty(asyncResp, "Oem/Nvidia/OpenOCDPortForward/Enable",
                    openocdPortForwardService,
                    sdbusplus::message::object_path(openocdPortForwardPath),
                    openocdPortForwardInterface, openocdPortForwardProperty,
                    value);
}

static constexpr std::string_view sshAuthPolicyIface =
    "com.nvidia.User.AccountPolicy";
static constexpr std::string_view sshAuthPolicyPath =
    "/xyz/openbmc_project/user";
static constexpr std::string_view sshAuthPolicyProp =
    "SSHPreferredAuthentication";
static constexpr std::string_view sshAuthMethodPrefix =
    "com.nvidia.User.AccountPolicy.AuthenticationMethod.";

/**
 * @brief Bidirectional mapping between a full D-Bus enum string and the
 *        corresponding SSHPreferredAuthentication enum value.
 *
 * Used in two directions:
 *   - Forward (GET):   match @p dbusName  to obtain @p authEnum.
 *   - Reverse (PATCH): match @p authEnum  to obtain @p dbusName.
 */
struct SSHAuthEntry
{
    /** Full D-Bus authentication-method value, e.g.
     *  "com.nvidia.User.AccountPolicy.AuthenticationMethod.Password". */
    std::string_view dbusName;
    nvidia_network_protocol::SSHPreferredAuthentication authEnum;
};

static constexpr std::array<SSHAuthEntry, 5> sshAuthMethodMap = {{
    {"com.nvidia.User.AccountPolicy.AuthenticationMethod.Password",
     nvidia_network_protocol::SSHPreferredAuthentication::Password},
    {"com.nvidia.User.AccountPolicy.AuthenticationMethod.PublicKey",
     nvidia_network_protocol::SSHPreferredAuthentication::PublicKey},
    {"com.nvidia.User.AccountPolicy.AuthenticationMethod.KeyboardInteractive",
     nvidia_network_protocol::SSHPreferredAuthentication::KeyboardInteractive},
    {"com.nvidia.User.AccountPolicy.AuthenticationMethod.GSSAPIWithMIC",
     nvidia_network_protocol::SSHPreferredAuthentication::GSSAPIWithMIC},
    {"com.nvidia.User.AccountPolicy.AuthenticationMethod.HostBased",
     nvidia_network_protocol::SSHPreferredAuthentication::HostBased},
}};

/**
 * @brief Populates Oem/Nvidia/SSH/PreferredAuthentications in a GET response.
 *
 * Queries the D-Bus Object Mapper to discover which service owns
 * @p sshAuthPolicyPath and exposes @p sshAuthPolicyIface, then reads the
 * @p sshAuthPolicyProp property from that service and maps each D-Bus string
 * value to its SSHPreferredAuthentication enum counterpart via
 * @p sshAuthMethodMap.
 *
 * If no service is found, or if the property cannot be read, the OEM field is
 * silently omitted — the property is treated as unavailable on this platform.
 *
 * @param asyncResp Shared async response object for the in-flight GET request.
 */
inline void populateSSHPreferredAuthentications(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    constexpr std::array<std::string_view, 1> interfaces = {sshAuthPolicyIface};

    dbus::utility::getDbusObject(
        std::string(sshAuthPolicyPath), interfaces,
        [asyncResp](const boost::system::error_code& ec,
                    const dbus::utility::MapperGetObject& mapperResponse) {
            if (ec || mapperResponse.empty())
            {
                BMCWEB_LOG_DEBUG("No service exposes {} on {}: {}",
                                 sshAuthPolicyIface, sshAuthPolicyPath,
                                 ec.message());
                return;
            }

            const std::string& service = mapperResponse[0].first;

            dbus::utility::getProperty<std::vector<std::string>>(
                service, std::string(sshAuthPolicyPath),
                std::string(sshAuthPolicyIface), std::string(sshAuthPolicyProp),
                [asyncResp](const boost::system::error_code& ec2,
                            const std::vector<std::string>& authMethods) {
                    if (ec2)
                    {
                        BMCWEB_LOG_ERROR("Failed to get {}: {}",
                                         sshAuthPolicyProp, ec2.message());
                        return;
                    }
                    nlohmann::json::array_t sshPreferredAuths;
                    for (const auto& method : authMethods)
                    {
                        // Forward lookup: D-Bus string → enum
                        const auto* it = std::ranges::find(
                            sshAuthMethodMap, std::string_view(method),
                            &SSHAuthEntry::dbusName);
                        if (it == sshAuthMethodMap.end())
                        {
                            BMCWEB_LOG_WARNING("Unknown {} value: {}",
                                               sshAuthPolicyProp, method);
                            continue;
                        }
                        sshPreferredAuths.emplace_back(it->authEnum);
                    }
                    asyncResp->res
                        .jsonValue["Oem"]["Nvidia"]["SSH"]["@odata.type"] =
                        "#NvidiaNetworkProtocol.v1_2_0.SSHProtocol";
                    asyncResp->res.jsonValue["Oem"]["Nvidia"]["SSH"]
                                            ["PreferredAuthentications"] =
                        std::move(sshPreferredAuths);
                });
        });
}

/**
 * @brief Applies DMTF DSP0266 variable-length array PATCH semantics to
 *        Oem/Nvidia/SSH/PreferredAuthentications.
 *
 * Each element of @p patchInput is interpreted as follows:
 *   - @c string  — update the element at the corresponding position, or append
 *                  if the index is past the current end of the array.
 *   - @c null    — delete the element at the corresponding position.
 *   - @c {}      — leave the element at the corresponding position unchanged.
 *
 * Elements beyond the length of @p patchInput are removed from
 * @p currentAuthMethods (variable-length array semantics).
 *
 * This function is pure — it performs no D-Bus calls. On success,
 * @p currentAuthMethods holds the merged result ready to be written back.
 *
 * @param res                 HTTP response used to write Redfish error
 * messages.
 * @param patchInput          Caller-supplied PATCH array from the request body.
 * @param currentAuthMethods  Current D-Bus-prefixed values on entry; merged
 *                            result on successful return.
 * @return @c true  if the merge succeeded.
 * @return @c false if a validation error was detected and written to @p res.
 */
inline bool patchSSHPreferredAuths(crow::Response& res,
                                   const std::vector<AuthMethod>& patchInput,
                                   std::vector<std::string>& currentAuthMethods)
{
    std::vector<std::string>::iterator currentMethod =
        currentAuthMethods.begin();

    for (size_t index = 0; index < patchInput.size(); index++)
    {
        const AuthMethod& entry = patchInput[index];
        const std::string propertyPath =
            "Oem/Nvidia/SSH/PreferredAuthentications/" + std::to_string(index);

        if (std::holds_alternative<std::nullptr_t>(entry))
        {
            // null: delete element at this position
            if (currentMethod == currentAuthMethods.end())
            {
                messages::propertyValueNotInList(res, "null", propertyPath);
                return false;
            }
            currentMethod = currentAuthMethods.erase(currentMethod);
            continue;
        }

        const nlohmann::json::object_t* obj =
            std::get_if<nlohmann::json::object_t>(&entry);
        if (obj != nullptr)
        {
            if (!obj->empty())
            {
                messages::propertyValueNotInList(res, *obj, propertyPath);
                return false;
            }
            // empty object: leave element unchanged
            if (currentMethod == currentAuthMethods.end())
            {
                messages::propertyValueOutOfRange(res, *obj, propertyPath);
                return false;
            }
            currentMethod++;
            continue;
        }

        const std::string* strVal = std::get_if<std::string>(&entry);
        if (strVal == nullptr)
        {
            messages::internalError(res);
            return false;
        }

        // Validate the Redfish string and resolve to the full D-Bus value in
        // one step: construct the candidate D-Bus name and look it up in
        // sshAuthMethodMap.  No exception handling is needed.
        std::string candidateDbusName =
            std::string(sshAuthMethodPrefix) + *strVal;
        const auto* it = std::ranges::find(sshAuthMethodMap,
                                           std::string_view(candidateDbusName),
                                           &SSHAuthEntry::dbusName);
        if (it == sshAuthMethodMap.end())
        {
            messages::propertyValueFormatError(res, *strVal, propertyPath);
            return false;
        }
        std::string dbusValue = std::string(it->dbusName);

        if (currentMethod == currentAuthMethods.end())
        {
            // Past the end: append new element
            currentAuthMethods.push_back(std::move(dbusValue));
            currentMethod = currentAuthMethods.end();
        }
        else
        {
            *currentMethod = std::move(dbusValue);
            currentMethod++;
        }
    }

    // Variable-length style: remove any remaining elements beyond the PATCH
    // array length
    currentAuthMethods.erase(currentMethod, currentAuthMethods.end());
    return true;
}

/**
 * @brief Writes the merged authentication-method list back to D-Bus.
 *
 * Calls @p patchSSHPreferredAuths to apply the PATCH semantics against
 * @p currentAuthMethods, then — if the merge succeeds — writes the result to
 * the @p sshAuthPolicyProp property on the already-discovered @p service and
 * @p objectPath.
 *
 * @param asyncResp           Shared async response object.
 * @param patchInput          Caller-supplied PATCH array from the request body.
 * @param currentAuthMethods  Current D-Bus-prefixed values fetched from D-Bus.
 * @param service             D-Bus service name that owns the object.
 * @param objectPath          D-Bus object path of the user-manager object.
 */
inline void handleSSHPreferredAuthsPatch(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::vector<AuthMethod>& patchInput,
    std::vector<std::string> currentAuthMethods, const std::string& service,
    const std::string& objectPath)
{
    if (!patchSSHPreferredAuths(asyncResp->res, patchInput, currentAuthMethods))
    {
        return;
    }
    setDbusProperty(asyncResp, "Oem/Nvidia/SSH/PreferredAuthentications",
                    service, sdbusplus::message::object_path(objectPath),
                    std::string(sshAuthPolicyIface),
                    std::string(sshAuthPolicyProp), currentAuthMethods);
}

/**
 * @brief Entry point for PATCH of Oem/Nvidia/SSH/PreferredAuthentications.
 *
 * Uses the D-Bus Object Mapper to discover which service exposes
 * @p sshAuthPolicyIface on @p sshAuthPolicyPath, reads the current value of
 * @p sshAuthPolicyProp from that service, delegates the merge to
 * @p handleSSHPreferredAuthsPatch, then writes the result back to D-Bus.
 *
 * Responds with @c internalError if no service is found or if the current
 * property value cannot be read.
 *
 * @param asyncResp  Shared async response object for the in-flight PATCH
 *                   request.
 * @param patchInput Caller-supplied PATCH array from the request body.
 */
inline void applySSHPreferredAuthsPatch(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::vector<AuthMethod>& patchInput)
{
    constexpr std::array<std::string_view, 1> interfaces = {sshAuthPolicyIface};

    dbus::utility::getDbusObject(
        std::string(sshAuthPolicyPath), interfaces,
        [asyncResp,
         patchInput](const boost::system::error_code& ec,
                     const dbus::utility::MapperGetObject& mapperResponse) {
            if (ec || mapperResponse.empty())
            {
                BMCWEB_LOG_WARNING("No service exposes {} on {}: {}",
                                   sshAuthPolicyIface, sshAuthPolicyPath,
                                   ec.message());
                messages::propertyNotWritable(
                    asyncResp->res, "Oem/Nvidia/SSH/PreferredAuthentications");
                return;
            }

            const std::string& service = mapperResponse[0].first;

            dbus::utility::getProperty<std::vector<std::string>>(
                service, std::string(sshAuthPolicyPath),
                std::string(sshAuthPolicyIface), std::string(sshAuthPolicyProp),
                [asyncResp, patchInput,
                 service](const boost::system::error_code& ec2,
                          const std::vector<std::string>& currentMethods) {
                    if (ec2)
                    {
                        BMCWEB_LOG_WARNING("Failed to get {}: {}",
                                           sshAuthPolicyProp, ec2.message());
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    handleSSHPreferredAuthsPatch(
                        asyncResp, patchInput, currentMethods, service,
                        std::string(sshAuthPolicyPath));
                });
        });
}

} // namespace redfish
