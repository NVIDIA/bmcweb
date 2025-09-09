/*
 * SPDX-FileCopyrightText: Copyright (c) 2021-2024 NVIDIA CORPORATION &
 * AFFILIATES. All rights reserved. SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include "debug_token/tlv/tlv.h"
#include "debug_token/tlv/types.h"

#include "component_integrity.hpp"
#include "dbus_singleton.hpp"
#include "dbus_utility.hpp"
#include "debug_token/request_utils.hpp"
#include "logging.hpp"
#include "utils/dbus_utils.hpp"

#include <boost/asio/post.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/system/error_code.hpp>
#include <sdbusplus/asio/property.hpp>
#include <sdbusplus/bus/match.hpp>
#include <sdbusplus/unpack_properties.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace redfish::debug_token::unified::spdm_request
{

/**
 * @brief Timeout duration in seconds for SPDM token requests
 *
 * This constant defines the maximum time to wait for a SPDM token request
 * operation to complete before timing out. The timeout is used to prevent
 * indefinite waiting for SPDM operations that may fail or hang.
 */
constexpr const int spdmTokenRequestTimeout = 5;

using Result = std::variant<std::string, std::vector<uint8_t>>;
using ResultCallback = std::function<void(const Result&)>;

/**
 * @brief Handler class for managing SPDM token requests
 *
 * This class handles asynchronous SPDM token requests for debug token
 * operations. It manages the complete lifecycle of SPDM operations including
 * DBus communication, timeout handling, error processing, and result delivery.
 *
 * The class uses std::enable_shared_from_this to ensure proper lifetime
 * management during asynchronous operations. It provides two operation
 * initiation methods:
 * - Chassis-based discovery: Automatically finds the appropriate DBus service
 *   and object path based on chassis ID
 * - Direct service/object path: Uses provided service and object path directly
 *
 * @note This class is non-copyable and non-movable due to shared state
 * management
 * @note All operations are asynchronous and use callbacks for completion
 * notification
 */
class Handler : public std::enable_shared_from_this<Handler>
{
  public:
    // Delete copy constructor and assignment operator since this class manages
    // shared state and should not be copied
    Handler(const Handler&) = delete;
    Handler& operator=(const Handler&) = delete;

    // Delete move constructor and assignment operator since this class manages
    // shared state and should not be moved
    Handler(Handler&&) = delete;
    Handler& operator=(Handler&&) = delete;
    /**
     * @brief Start a SPDM operation for a specific chassis
     *
     * This function initiates a SPDM token request operation for a specific
     * chassis. It creates a shared pointer to a new Handler object and
     * initializes it with the provided token type and callback function.
     * The handler automatically discovers the appropriate DBus service and
     * object path associated with the chassis.
     *
     * @param chassisId The chassis ID for which the operation is to be
     *                  performed
     * @param type The token type for the SPDM operation
     * @param callback The callback function to be called when the operation
     *                 completes (successfully or with error)
     *
     * @note The operation is asynchronous and the callback will be invoked
     *       when the operation completes or times out
     * @note If chassis discovery fails, the callback will be invoked with
     *       an error message
     */
    static void startOperation(const std::string& chassisId, TokenType type,
                               ResultCallback callback)
    {
        struct MakeSharedHelper : public Handler
        {
            MakeSharedHelper(TokenType t, ResultCallback cb) :
                Handler(t, std::move(cb))
            {}
        };
        std::shared_ptr<Handler> handler =
            std::make_shared<MakeSharedHelper>(type, std::move(callback));
        handler->run(chassisId);
    }

    /**
     * @brief Start a SPDM token request for a specific DBus object and service
     *
     * This function initiates a SPDM token request operation using a specific
     * DBus service and object path. It creates a shared pointer to a new
     * Handler object and initializes it with the provided token type and
     * callback function. This method bypasses chassis discovery and uses
     * the provided service and object path directly.
     *
     * @param service The DBus service name for the SPDM operation
     * @param objectPath The DBus object path for the SPDM responder
     * @param type The token type for the SPDM operation
     * @param callback The callback function to be called when the operation
     *                 completes (successfully or with error)
     *
     * @note The operation is asynchronous and the callback will be invoked
     *       when the operation completes or times out
     * @note This method is typically used when the service and object path
     *       are already known, avoiding the overhead of chassis discovery
     */
    static void startOperation(const std::string& service,
                               const std::string& objectPath, TokenType type,
                               ResultCallback callback)
    {
        struct MakeSharedHelper : public Handler
        {
            MakeSharedHelper(TokenType t, ResultCallback cb) :
                Handler(t, std::move(cb))
            {}
        };
        std::shared_ptr<Handler> handler =
            std::make_shared<MakeSharedHelper>(type, std::move(callback));
        handler->run(service, objectPath);
    }

    /**
     * @brief Destructor for the SPDM Handler
     *
     * Ensures that any pending callback is invoked with the current result
     * before the handler is destroyed. This guarantees that callers are
     * always notified of the operation outcome, even if the handler is
     * destroyed before explicit completion.
     */
    ~Handler()
    {
        if (callback)
        {
            callback(result);
        }
    }

  private:
    /**
     * @brief Private constructor for the SPDM Handler
     *
     * Constructs a new Handler instance with the specified token type
     * and callback function. This constructor is private to enforce the
     * use of the static startOperation methods for creating handler instances.
     *
     * @param t The token type for the SPDM operation
     * @param cb The callback function to be called when the operation
     *           completes (successfully or with error)
     */
    Handler(TokenType t, ResultCallback cb) : type(t), callback(std::move(cb))
    {}

    TokenType type;
    Result result;
    ResultCallback callback;

    std::string spdmObjectPath;
    std::unique_ptr<sdbusplus::bus::match_t> match;
    std::unique_ptr<boost::asio::steady_timer> timer;

    /**
     * @brief Run the SPDM operation for a specific chassis
     *
     * This function initiates the SPDM operation by discovering the appropriate
     * DBus service and object path associated with the specified chassis.
     * It performs a subtree query to find SPDM responder interfaces and
     * matches them to the chassis ID.
     *
     * @param chassisId The chassis ID for which the operation is to be
     *                  performed
     *
     * @note This is an internal method called by the public startOperation
     *       methods
     * @note If no matching chassis is found, the operation will be terminated
     *       with an error
     */
    void run(const std::string& chassisId)
    {
        constexpr std::array<std::string_view, 1> interfaces = {
            spdmResponderIntf};
        dbus::utility::getSubTree(
            std::string(rootSPDMDbusPath), 0, interfaces,
            std::bind_front(&Handler::subTreeHandler, this, shared_from_this(),
                            chassisId));
    }

    /**
     * @brief Handle the sub-tree response for chassis discovery
     *
     * This function processes the response from the DBus subtree query used
     * to discover SPDM responder interfaces. It searches through the response
     * to find the service and object path that matches the specified chassis
     * ID.
     *
     * @param self The shared pointer to this handler instance
     * @param chassisId The chassis ID for which the operation is to be
     *                  performed
     * @param ec Error code from the DBus subtree query
     * @param resp The subtree response containing available services and
     *             object paths
     *
     * @note If no matching chassis is found or an error occurs, the operation
     *       is terminated with an appropriate error message
     * @note The chassis ID matching is performed by comparing the filename
     *       of the object path with the provided chassis ID
     */
    void subTreeHandler(const std::shared_ptr<Handler>& self,
                        const std::string& chassisId,
                        const boost::system::error_code& ec,
                        const dbus::utility::MapperGetSubTreeResponse& resp)
    {
        if (ec)
        {
            errorHandler(self, "GetSubTreePaths error: " + ec.message());
            return;
        }
        if (resp.empty())
        {
            errorHandler(self,
                         "No objects with SPDM responder interface found");
            return;
        }
        std::string service;
        std::string objectPath;
        for (const auto& [path, serviceMap] : resp)
        {
            auto pathChassisId =
                std::filesystem::path(path).filename().string();
            if (chassisId == pathChassisId)
            {
                service = serviceMap[0].first;
                objectPath = path;
                break;
            }
        }
        if (objectPath.empty())
        {
            errorHandler(self, "SPDM responder interface not implemented for " +
                                   chassisId);
            return;
        }
        run(service, objectPath);
    }

    /**
     * @brief Run the SPDM operation for a specific DBus object and service
     *
     * This function initiates the actual SPDM token request by calling the
     * Refresh method on the specified SPDM responder service. It sets up
     * monitoring for the operation status and creates a timeout timer.
     *
     * @param service The DBus service name for the SPDM responder
     * @param objectPath The DBus object path for the SPDM responder
     *
     * @note This method sets up asynchronous monitoring and timeout handling
     *       for the SPDM operation
     * @note The operation uses the token type to determine the measurement
     *       index for the SPDM request
     */
    void run(const std::string& service, const std::string& objectPath)
    {
        spdmObjectPath = objectPath;

        createMatch();
        createTimer(spdmTokenRequestTimeout);

        std::vector<uint8_t> indices{
            spdmTokenTypeToMeasurementIndex(this->type)};
        std::function<void(const boost::system::error_code&)> handler =
            std::bind_front(&Handler::methodHandler, this, shared_from_this());
        crow::connections::systemBus->async_method_call(
            handler, service, objectPath, spdmResponderIntf, "Refresh",
            static_cast<uint8_t>(0), std::vector<uint8_t>(), indices,
            static_cast<uint32_t>(0));
    }

    /**
     * @brief Create a DBus match for monitoring SPDM operation status
     *
     * This function creates a DBus match rule to monitor property changes
     * on the SPDM responder interface. The match will trigger callbacks
     * when the SPDM operation status changes.
     *
     * @note The match is automatically cleaned up when the operation
     *       completes or times out
     * @note The match rule monitors property changes on the SPDM responder
     *       interface for the specific object path
     */
    void createMatch()
    {
        std::string matchRule = sdbusplus::bus::match::rules::propertiesChanged(
            spdmObjectPath, spdmResponderIntf);
        match = std::make_unique<sdbusplus::bus::match_t>(
            *crow::connections::systemBus, matchRule.c_str(),
            std::bind_front(&Handler::matchHandler, this, shared_from_this()));
    }

    /**
     * @brief Destroy the DBus match for SPDM operation monitoring
     *
     * This function cleans up the DBus match that was created to monitor
     * the SPDM operation status. It should be called when the operation
     * completes or times out.
     *
     * @param self The shared self pointer to the parent object (unused)
     * @note This method is thread-safe and can be called from any thread
     */
    void destroyMatch(const std::shared_ptr<Handler>& /*unused*/)
    {
        match.reset(nullptr);
    }

    /**
     * @brief Handle DBus property change notifications for SPDM status
     *
     * This function is called when the SPDM operation status changes.
     * It extracts the status from the DBus message and processes it
     * accordingly.
     *
     * @param self The shared self pointer to the parent object
     * @param msg DBus message containing the property change notification
     * @note This method handles the unpacking of the Status property from
     *       the DBus message and forwards it to the status handler
     */
    void matchHandler(const std::shared_ptr<Handler>& self,
                      sdbusplus::message::message& msg)
    {
        std::string interface;
        dbus::utility::DBusPropertiesMap propertiesMap;
        msg.read(interface, propertiesMap);
        std::string spdmStatus;
        const bool success = sdbusplus::unpackPropertiesNoThrow(
            dbus_utils::UnpackErrorPrinter(), propertiesMap, "Status",
            spdmStatus);
        if (!success)
        {
            BMCWEB_LOG_ERROR("DebugToken SPDM error: "
                             "failed to unpack Status");
            return;
        }
        spdmStatusHandler(self, spdmStatus);
    }

    /**
     * @brief Create a timer to monitor for SPDM request timeout
     *
     * This function creates an asynchronous timer that will trigger if the
     * SPDM operation takes longer than the specified timeout period.
     *
     * @param timeout Timeout duration in seconds
     *
     * @note The timer is automatically cancelled if the operation completes
     *       before the timeout
     * @note The timer uses the system bus IO context for execution
     */
    void createTimer(int timeout)
    {
        timer = std::make_unique<boost::asio::steady_timer>(
            crow::connections::systemBus->get_io_context());
        timer->expires_after(std::chrono::seconds(timeout));
        timer->async_wait(
            std::bind_front(&Handler::timerHandler, this, shared_from_this()));
    }

    /**
     * @brief Destroy the SPDM operation timeout timer
     *
     * This function cleans up the timeout timer that was created to monitor
     * the SPDM operation. It should be called when the operation completes
     * or times out.
     *
     * @param self The shared self pointer to the parent object (unused)
     * @note This method is thread-safe and can be called from any thread
     */
    void destroyTimer(const std::shared_ptr<Handler>& /*unused*/)
    {
        timer.reset(nullptr);
    }

    /**
     * @brief Handle SPDM operation timeout
     *
     * This function is called when the SPDM operation timeout timer expires
     * or when a timer error occurs. It handles the timeout by setting an
     * appropriate error message and cleaning up resources.
     *
     * @param self The shared self pointer to the parent object
     * @param ec The error code from the timer operation
     *
     * @note If ec is empty, it indicates a timeout. If ec is operation_aborted,
     *       the timer was cancelled normally
     * @note This method performs cleanup of both the match and timer resources
     */
    void timerHandler(const std::shared_ptr<Handler>& self,
                      const boost::system::error_code& ec)
    {
        if (!ec)
        {
            errorHandler(self, "SPDM operation timeout");
            return;
        }
        if (ec != boost::asio::error::operation_aborted)
        {
            BMCWEB_LOG_ERROR("async_wait error: {}", ec.message());
        }
        destroyMatch(self);
        boost::asio::post(crow::connections::systemBus->get_io_context(),
                          std::bind_front(&Handler::destroyTimer, this, self));
    }
    /**
     * @brief Handle the SPDM Refresh method call response
     *
     * This function is called when the SPDM Refresh method call completes.
     * It checks for errors and then retrieves the current status of the
     * SPDM operation.
     *
     * @param self The shared self pointer to the parent object
     * @param ec The error code from the DBus method call
     *
     * @note This is the callback for the async_method_call to the SPDM Refresh
     * method
     * @note If the method call succeeds, it retrieves the current status
     *       from the SPDM responder service
     */
    void methodHandler(const std::shared_ptr<Handler>& self,
                       const boost::system::error_code& ec)
    {
        if (ec)
        {
            errorHandler(self, "DBus error: " + ec.message());
            return;
        }
        sdbusplus::asio::getProperty<std::string>(
            *crow::connections::systemBus, spdmBusName, spdmObjectPath,
            std::string(spdmResponderIntf), "Status",
            std::bind_front(&Handler::getStatusHandler, this, self));
    }

    /**
     * @brief Handle the SPDM status property retrieval response
     *
     * This function is called when the SPDM status property is successfully
     * retrieved from the DBus service. It processes the status and determines
     * the next action based on the status value.
     *
     * @param self The shared self pointer to the parent object
     * @param ec The error code from the DBus property get operation
     * @param spdmStatus The current SPDM operation status string
     * @note This method forwards the status to the status handler for
     *       further processing
     */
    void getStatusHandler(const std::shared_ptr<Handler>& self,
                          const boost::system::error_code& ec,
                          const std::string& spdmStatus)
    {
        if (ec)
        {
            errorHandler(self, "DBus Get error: " + ec.message());
            return;
        }
        spdmStatusHandler(self, spdmStatus);
    }

    /**
     * @brief Process SPDM operation status and determine next action
     *
     * This function parses the SPDM status value and determines the appropriate
     * next action. If the status indicates success, it retrieves the
     * measurement data. If the status indicates an error, it handles the error
     * condition.
     *
     * @param self The shared self pointer to the parent object
     * @param spdmStatus The SPDM status value to process
     *
     * @note Status values starting with "Error_" indicate failure conditions,
     *       while "Success" indicates successful completion
     * @note The status is parsed by extracting the substring after the last
     *       dot character
     */
    void spdmStatusHandler(const std::shared_ptr<Handler>& self,
                           const std::string& spdmStatus)
    {
        std::string status =
            spdmStatus.substr(spdmStatus.find_last_of('.') + 1);
        if (status.starts_with("Error_"))
        {
            errorHandler(self, "SPDM error: " + status);
        }
        else if (status == "Success")
        {
            dbus::utility::getAllProperties(
                spdmBusName, spdmObjectPath, spdmResponderIntf,
                std::bind_front(&Handler::requestHandler, this, self));
        }
    }

    /**
     * @brief Handle the SPDM measurement data retrieval response
     *
     * This function processes the SPDM measurement data retrieved from the
     * DBus service. It extracts the signed measurements, capabilities, and
     * certificate data, then encodes them into a TLV structure.
     *
     * @param self The shared self pointer to the parent object (unused)
     * @param ec The error code from the DBus properties retrieval call
     * @param propertiesMap The map of properties retrieved from the SPDM
     * responder service
     *
     * @note This function handles the final data processing and result
     *       construction for the SPDM operation
     * @note The result is constructed using TLV encoding with:
     *       - Type 0x0014 (MeasurementTranscript) for signed measurements
     *       - Type 0x0013 (CertificateChain) for certificate data
     */
    void requestHandler(const std::shared_ptr<Handler>& self,
                        const boost::system::error_code& ec,
                        const dbus::utility::DBusPropertiesMap& propertiesMap)
    {
        if (ec)
        {
            errorHandler(self, "DBus Get error: " + ec.message());
            return;
        }
        std::vector<uint8_t> signedMeasurements;
        uint32_t capabilities = 0;
        std::vector<std::tuple<uint8_t, std::string>> certificates;
        const bool success = sdbusplus::unpackPropertiesNoThrow(
            dbus_utils::UnpackErrorPrinter(), propertiesMap,
            "SignedMeasurements", signedMeasurements, "Capabilities",
            capabilities, "Certificate", certificates);
        if (!success)
        {
            errorHandler(self, "cannot unpack properties");
            return;
        }

        ::debug_token::tlv_encoder::Structure tlvStructure;
        tlvStructure.setVersion(1, 0);
        tlvStructure.add(::debug_token::types::Common::MeasurementTranscript,
                         signedMeasurements);
        if ((capabilities & spdmCertCapability) != 0U)
        {
            auto certificateSlot =
                std::find_if(certificates.begin(), certificates.end(),
                             [](const auto& e) { return std::get<0>(e) == 0; });
            if (certificateSlot == certificates.end())
            {
                errorHandler(self, "cannot find certificate for slot 0");
                return;
            }
            std::string pem = std::get<1>(*certificateSlot);
            std::vector<uint8_t> pemBytes(pem.begin(), pem.end());
            tlvStructure.add(::debug_token::types::Common::CertificateChain,
                             pemBytes);
        }
        result = tlvStructure.encode();
        cleanup(self);
    }

    /**
     * @brief Handle error conditions and set error result
     *
     * This function processes error conditions that occur during the SPDM
     * operation. It logs the error message and sets the result to contain the
     * error information before cleaning up resources.
     *
     * @param self The shared self pointer to the parent object
     * @param error The error message describing what went wrong
     * @note The error message is prefixed with the SPDM object path for
     *       better debugging context
     */
    void errorHandler(const std::shared_ptr<Handler>& self,
                      const std::string& error)
    {
        BMCWEB_LOG_ERROR("DebugToken SPDM error: {}", error);
        result = error;

        cleanup(self);
    }

    /**
     * @brief Clean up resources after operation completion
     *
     * This function performs cleanup operations after the SPDM operation
     * completes (either successfully or with an error). It destroys the
     * DBus match and timer resources asynchronously.
     *
     * @param self The shared self pointer to the parent object
     * @note This method posts cleanup tasks to the IO context to ensure
     *       thread-safe resource cleanup
     */
    void cleanup(const std::shared_ptr<Handler>& self)
    {
        boost::asio::post(crow::connections::systemBus->get_io_context(),
                          std::bind_front(&Handler::destroyMatch, this, self));
        boost::asio::post(crow::connections::systemBus->get_io_context(),
                          std::bind_front(&Handler::destroyTimer, this, self));
    }
};

} // namespace redfish::debug_token::unified::spdm_request
