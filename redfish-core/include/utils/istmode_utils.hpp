#pragma once

#include <registries/privilege_registry.hpp>
#include <sdbusplus/asio/property.hpp>
#include <task.hpp>
#include <utils/dbus_utils.hpp>

#include <variant>

namespace redfish
{

static const std::string& istMgrServ = "com.Nvidia.IstModeManager";
static const std::string& istMgrIface = "com.Nvidia.IstModeManager.Server";
static const std::string& istMgrPath = "/xyz/openbmc_project/IstModeManager";

namespace ist_mode_utils
{

inline void getIstMode(const std::shared_ptr<bmcweb::AsyncResp>& aResp)
{
    std::string istIface = "xyz.openbmc_project.Control.Mode";
    // Async method call to get mode settings dbus object and service
    dbus::utility::getSubTree(
        "/xyz/openbmc_project/mode/", int32_t(0),
        std::array<std::string_view, 1>{"xyz.openbmc_project.Control.Mode"},
        [aResp,
         istIface](const boost::system::error_code& ec,
                   const dbus::utility::MapperGetSubTreeResponse& subtree) {
            if (ec)
            {
                // IST mode manager may be absent or the mapper transiently
                // unavailable under load; treated as non-fatal, so log at
                // debug level to avoid flooding.
                BMCWEB_LOG_DEBUG("D-Bus response error on GetSubTree {}", ec);
                // messages::internalError(aResp->res);
                return;
            }
            // Throw an error on empty subtree response
            // Assume only 1 system D-Bus object
            // Throw an error if there is more than 1
            if (subtree.empty() || (subtree.size() > 1))
            {
                BMCWEB_LOG_ERROR("Can't find system IST Mode D-Bus object!");
                messages::internalError(aResp->res);
                return;
            }

            const std::string& path = subtree[0].first;
            const std::string& service = subtree[0].second.begin()->first;

            if (service.empty())
            {
                BMCWEB_LOG_ERROR("ISTMode Settings service mapper error!");
                messages::internalError(aResp->res);
                return;
            }

            // Async method call to get Active ISTmode
            dbus::utility::getProperty<std::string>(
                service, path, istIface, "ISTMode",
                [aResp](const boost::system::error_code& ec1,
                        const std::string& istMode) {
                    if (ec1)
                    {
                        BMCWEB_LOG_ERROR("DBUS response error for "
                                         "Trying to get ISTMode");
                        messages::internalError(aResp->res);
                        return;
                    }
                    bool istModeEnabled = false;
                    nlohmann::json& json = aResp->res.jsonValue;
                    auto modeVal = dbus_utils::getRedfishIstMode(istMode);

                    if (modeVal == "Enabled")
                    {
                        istModeEnabled = true;
                    }

                    json["Oem"]["Nvidia"]["ISTModeEnabled"] = istModeEnabled;
                });
        });
}

inline void setIstMode(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                       const crow::Request& req, const bool& reqIstModeEnabled)
{
    std::string istIface = "xyz.openbmc_project.Control.Mode";
    auto reqPayload = std::make_shared<task::Payload>(req);

    // Async method call to get phosphor settings dbus object path and service
    dbus::utility::getSubTree(
        "/xyz/openbmc_project/mode/", int32_t(0),
        std::array<std::string_view, 1>{"xyz.openbmc_project.Control.Mode"},
        [aResp, reqIstModeEnabled, istIface,
         reqPayload](const boost::system::error_code& ec,
                     const dbus::utility::MapperGetSubTreeResponse& subtree) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("D-Bus response error on GetSubTree {}", ec);
                messages::internalError(aResp->res);
                return;
            }
            // Throw an error on empty subtree response
            // Assume only 1 system D-Bus object
            // Throw an error if there is more than 1
            if (subtree.empty() || (subtree.size() > 1))
            {
                BMCWEB_LOG_ERROR("Can't find system IST Mode D-Bus object!");
                messages::internalError(aResp->res);
                return;
            }

            const std::string& path = subtree[0].first;
            const std::string& service = subtree[0].second.begin()->first;

            if (service.empty())
            {
                BMCWEB_LOG_ERROR("ISTMode Settings service mapper error!");
                messages::internalError(aResp->res);
                return;
            }

            // Async method call to get Current ISTmode
            dbus::utility::getProperty<
                std::string>(service, path, istIface, "ISTMode",
                             [aResp, reqIstModeEnabled, istIface, path, service,
                              reqPayload](const boost::system::error_code& ec1,
                                          const std::string& istMode) {
                                 if (ec1)
                                 {
                                     BMCWEB_LOG_ERROR("DBUS response error for "
                                                      "Trying to get ISTMode");
                                     messages::internalError(aResp->res);
                                     return;
                                 }
                                 auto modeVal =
                                     dbus_utils::getRedfishIstMode(istMode);

                                 // validate request
                                 if ((modeVal == "Enabled") &&
                                     reqIstModeEnabled)
                                 {
                                     BMCWEB_LOG_ERROR(
                                         "ISTMode Already Enabled");
                                     aResp->res.result(boost::beast::http::
                                                           status::no_content);
                                     return;
                                 }
                                 // validate request
                                 if ((modeVal == "Disabled") &&
                                     !reqIstModeEnabled)
                                 {
                                     BMCWEB_LOG_ERROR(
                                         "ISTMode Already Disabled");
                                     aResp->res.result(boost::beast::http::
                                                           status::no_content);
                                     return;
                                 }

                                 // Async method call to get current Status
                                 dbus::utility::getProperty<std::string>(istMgrServ,
                                                                         istMgrPath,
                                                                         istMgrIface,
                                                                         "Status",
                                                                         [aResp,
                                                                          reqIstModeEnabled,
                                                                          reqPayload](const boost::
                                                                                          system::error_code&
                                                                                              ec2,
                                                                                      const std::
                                                                                          string&
                                                                                              istStatus) {
                                                                             if (ec2)
                                                                             {
                                                                                 BMCWEB_LOG_DEBUG(
                                                                                     "DBUS response error for "
                                                                                     "Trying to get ISTManager Status");
                                                                                 messages::internalError(
                                                                                     aResp
                                                                                         ->res);
                                                                                 return;
                                                                             }
                                                                             // If ISTMode Setting is already in progress,
                                                                             // return error
                                                                             auto status = dbus_utils::
                                                                                 toIstmgrStatus(
                                                                                     istStatus);

                                                                             if (status ==
                                                                                 "InProgress")
                                                                             {
                                                                                 BMCWEB_LOG_ERROR(
                                                                                     "ISTMode Settings In Progress");
                                                                                 std::string
                                                                                     resolution =
                                                                                         "ISTMode operation is in progress. Retry"
                                                                                         " the  operation once it is complete.";
                                                                                 redfish::messages::
                                                                                     updateInProgressMsg(
                                                                                         aResp
                                                                                             ->res,
                                                                                         resolution);
                                                                                 return;
                                                                             }

                                                                             std::string setParam =
                                                                                 dbus_utils::
                                                                                     getIstmgrParam(
                                                                                         reqIstModeEnabled);

                                                                             // Async method call setISTMode
                                                                             dbus::utility::async_method_call(
                                                                                 [aResp,
                                                                                  reqIstModeEnabled,
                                                                                  reqPayload](
                                                                                     boost::system::
                                                                                         error_code&
                                                                                             ec3) {
                                                                                     if (ec3)
                                                                                     {
                                                                                         BMCWEB_LOG_ERROR(
                                                                                             "setISTMode failed with error");
                                                                                         messages::internalError(
                                                                                             aResp
                                                                                                 ->res);
                                                                                         return;
                                                                                     }
                                                                                     std::string reqIstModVal =
                                                                                         dbus_utils::
                                                                                             getReqMode(
                                                                                                 reqIstModeEnabled);
                                                                                     // create task to monitor
                                                                                     // ISTMode status
                                                                                     std::shared_ptr<
                                                                                         task::
                                                                                             TaskData>
                                                                                         task = task::TaskData::createTask(
                                                                                             [reqIstModVal](
                                                                                                 boost::system::
                                                                                                     error_code
                                                                                                         ec4,
                                                                                                 sdbusplus::message::
                                                                                                     message&
                                                                                                         taskMsg,
                                                                                                 const std::shared_ptr<
                                                                                                     task::
                                                                                                         TaskData>&
                                                                                                     taskData) {
                                                                                                 if (ec4)
                                                                                                 {
                                                                                                     BMCWEB_LOG_ERROR(
                                                                                                         "task cancelled");
                                                                                                     taskData
                                                                                                         ->state =
                                                                                                         "Cancelled";
                                                                                                     taskData
                                                                                                         ->messages
                                                                                                         .emplace_back(messages::resourceErrorsDetectedFormatError(
                                                                                                             "SetIstMode task",
                                                                                                             ec4.message()));
                                                                                                     taskData
                                                                                                         ->finishTask();
                                                                                                     return task::
                                                                                                         completed;
                                                                                                 }

                                                                                                 std::string
                                                                                                     interface;
                                                                                                 std::map<
                                                                                                     std::
                                                                                                         string,
                                                                                                     dbus::utility::
                                                                                                         DbusVariantType>
                                                                                                     props;

                                                                                                 taskMsg
                                                                                                     .read(
                                                                                                         interface,
                                                                                                         props);
                                                                                                 auto it =
                                                                                                     props
                                                                                                         .find(
                                                                                                             "Status");
                                                                                                 if (it ==
                                                                                                     props
                                                                                                         .end())
                                                                                                 {
                                                                                                     BMCWEB_LOG_ERROR(
                                                                                                         "Did not receive an ISTMode Status value");
                                                                                                     return !task::
                                                                                                         completed;
                                                                                                 }
                                                                                                 auto* value = std::get_if<
                                                                                                     std::
                                                                                                         string>(
                                                                                                     &(it->second));
                                                                                                 if (value ==
                                                                                                     nullptr)
                                                                                                 {
                                                                                                     BMCWEB_LOG_ERROR(
                                                                                                         "Received ISTMode Status is not a string");
                                                                                                     return !task::
                                                                                                         completed;
                                                                                                 }
                                                                                                 auto mode = dbus_utils::
                                                                                                     toIstmgrStatus(
                                                                                                         *value);
                                                                                                 if (mode ==
                                                                                                     "InProgress")
                                                                                                 {
                                                                                                     // ignore inprogress change
                                                                                                     return !task::
                                                                                                         completed;
                                                                                                 }
                                                                                                 if (mode ==
                                                                                                     reqIstModVal)
                                                                                                 {
                                                                                                     // ist mode manager status
                                                                                                     // property changed to user
                                                                                                     // requested value
                                                                                                     taskData
                                                                                                         ->state =
                                                                                                         "Completed";
                                                                                                     taskData
                                                                                                         ->percentComplete =
                                                                                                         100;
                                                                                                     taskData
                                                                                                         ->messages
                                                                                                         .emplace_back(messages::taskCompletedOK(
                                                                                                             std::to_string(
                                                                                                                 taskData
                                                                                                                     ->index)));
                                                                                                     taskData
                                                                                                         ->finishTask();
                                                                                                     return task::
                                                                                                         completed;
                                                                                                 }

                                                                                                 // ist mode manager status
                                                                                                 // property changed to value
                                                                                                 // other than inprogress and
                                                                                                 // user requested value
                                                                                                 // throw error message in
                                                                                                 // task status and return
                                                                                                 taskData
                                                                                                     ->state =
                                                                                                     "Exception";
                                                                                                 taskData
                                                                                                     ->messages
                                                                                                     .emplace_back(
                                                                                                         messages::resourceErrorsDetectedFormatError(
                                                                                                             "NvidiaComputerSystem.ISTMode",
                                                                                                             reqIstModVal +
                                                                                                                 " Failed"));
                                                                                                 taskData
                                                                                                     ->finishTask();
                                                                                                 return task::
                                                                                                     completed;
                                                                                             },
                                                                                             "type='signal',interface='org.freedesktop.DBus.Properties',"
                                                                                             "member='PropertiesChanged',path='" +
                                                                                                 istMgrPath +
                                                                                                 "'");
                                                                                     task->startTimer(
                                                                                         std::chrono::
                                                                                             seconds(
                                                                                                 150));
                                                                                     task->populateResp(
                                                                                         aResp
                                                                                             ->res);
                                                                                     task->payload
                                                                                         .emplace(std::move(
                                                                                             *reqPayload));
                                                                                 },
                                                                                 istMgrServ,
                                                                                 istMgrPath,
                                                                                 istMgrIface,
                                                                                 "setISTMode",
                                                                                 setParam);
                                                                         });
                             });
        });
}

} // namespace ist_mode_utils
} // namespace redfish
