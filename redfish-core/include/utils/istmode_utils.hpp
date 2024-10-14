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
    crow::connections::systemBus->async_method_call(
        [aResp,
         istIface](const boost::system::error_code ec,
                   const dbus::utility::MapperGetSubTreeResponse& subtree) {
        if (ec)
        {
            BMCWEB_LOG_ERROR("D-Bus response error on GetSubTree {}", ec);
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
        crow::connections::systemBus->async_method_call(
            [aResp](const boost::system::error_code ec1,
                    const std::variant<std::string>& istMode) {
            if (ec1)
            {
                BMCWEB_LOG_ERROR("DBUS response error for "
                                 "Trying to get ISTMode");
                messages::internalError(aResp->res);
                return;
            }
            bool istModeEnabled = false;
            nlohmann::json& json = aResp->res.jsonValue;
            auto mode = dbus_utils::getRedfishIstMode(
                *std::get_if<std::string>(&istMode));

            if (mode == "Enabled")
            {
                istModeEnabled = true;
            }

            json["Oem"]["Nvidia"]["ISTModeEnabled"] = istModeEnabled;
        },
            service, path, "org.freedesktop.DBus.Properties", "Get", istIface,
            "ISTMode");
    },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/xyz/openbmc_project/mode/", int32_t(0),
        std::array<const char*, 1>{"xyz.openbmc_project.Control.Mode"});
}

inline void setIstMode(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                       const crow::Request& req, const bool& reqIstModeEnabled)
{
    std::string istIface = "xyz.openbmc_project.Control.Mode";

    // Async method call to get phosphor settings dbus object path and service
    crow::connections::systemBus->async_method_call(
        [aResp, req, reqIstModeEnabled,
         istIface](const boost::system::error_code ec,
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
        crow::connections::systemBus->async_method_call(
            [aResp, req, reqIstModeEnabled, istIface, path,
             service](const boost::system::error_code ec1,
                      const std::variant<std::string>& istMode) {
            if (ec1)
            {
                BMCWEB_LOG_ERROR("DBUS response error for "
                                 "Trying to get ISTMode");
                messages::internalError(aResp->res);
                return;
            }
            auto modePtr = std::get_if<std::string>(&istMode);
            if (modePtr == nullptr)
            {
                BMCWEB_LOG_ERROR("ISTMode not received");
                messages::internalError(aResp->res);
                return;
            }
            auto mode = dbus_utils::getRedfishIstMode(*modePtr);

            // validate request
            if ((mode == "Enabled") && reqIstModeEnabled)
            {
                BMCWEB_LOG_ERROR("ISTMode Already Enabled");
                aResp->res.result(boost::beast::http::status::no_content);
                return;
            }
            // validate request
            if ((mode == "Disabled") && !reqIstModeEnabled)
            {
                BMCWEB_LOG_ERROR("ISTMode Already Disabled");
                aResp->res.result(boost::beast::http::status::no_content);
                return;
            }

            // Async method call to get current Status
            crow::connections::systemBus->async_method_call(
                [aResp, reqIstModeEnabled,
                 req](const boost::system::error_code ec1,
                      const std::variant<std::string>& istStatus) {
                if (ec1)
                {
                    BMCWEB_LOG_DEBUG("DBUS response error for "
                                     "Trying to get ISTManager Status");
                    messages::internalError(aResp->res);
                    return;
                }
                // If ISTMode Setting is already in progress,
                // return error
                auto statusPtr = std::get_if<std::string>(&istStatus);
                if (statusPtr == nullptr)
                {
                    BMCWEB_LOG_ERROR("ISTMode Settings Status not found");
                    messages::internalError(aResp->res);
                    return;
                }
                auto status = dbus_utils::toIstmgrStatus(*statusPtr);

                if (status == "InProgress")
                {
                    BMCWEB_LOG_ERROR("ISTMode Settings In Progress");
                    std::string resolution =
                        "ISTMode operation is in progress. Retry"
                        " the  operation once it is complete.";
                    redfish::messages::updateInProgressMsg(aResp->res,
                                                           resolution);
                    return;
                }

                std::string setParam =
                    dbus_utils::getIstmgrParam(reqIstModeEnabled);

                // Async method call setISTMode
                crow::connections::systemBus->async_method_call(
                    [aResp, req,
                     reqIstModeEnabled](boost::system::error_code ec) {
                    if (ec)
                    {
                        BMCWEB_LOG_ERROR("setISTMode failed with error");
                        messages::internalError(aResp->res);
                        return;
                    }
                    std::string reqIstModVal =
                        dbus_utils::getReqMode(reqIstModeEnabled);
                    // create task to monitor
                    // ISTMode status
                    std::shared_ptr<task::TaskData> task =
                        task::TaskData::createTask(
                            [reqIstModVal](
                                boost::system::error_code err,
                                sdbusplus::message::message& taskMsg,
                                const std::shared_ptr<task::TaskData>&
                                    taskData) {
                        if (err)
                        {
                            BMCWEB_LOG_ERROR("task cancelled");
                            taskData->state = "Cancelled";
                            taskData->messages.emplace_back(
                                messages::resourceErrorsDetectedFormatError(
                                    "SetIstMode task", err.message()));
                            taskData->finishTask();
                            return task::completed;
                        }

                        std::string interface;
                        std::map<std::string, dbus::utility::DbusVariantType>
                            props;

                        taskMsg.read(interface, props);
                        auto it = props.find("Status");
                        if (it == props.end())
                        {
                            BMCWEB_LOG_ERROR(
                                "Did not receive an ISTMode Status value");
                            return !task::completed;
                        }
                        auto value = std::get_if<std::string>(&(it->second));
                        if (!value)
                        {
                            BMCWEB_LOG_ERROR(
                                "Received ISTMode Status is not a string");
                            return !task::completed;
                        }
                        std::string propName = "ISTMode";
                        auto mode = dbus_utils::toIstmgrStatus(*value);
                        if (mode == "InProgress")
                        {
                            // ignore inprogress change
                            return !task::completed;
                        }
                        if (mode == reqIstModVal)
                        {
                            // ist mode manager status
                            // property changed to user
                            // requested value
                            taskData->state = "Completed";
                            taskData->percentComplete = 100;
                            taskData->messages.emplace_back(
                                messages::taskCompletedOK(
                                    std::to_string(taskData->index)));
                            taskData->finishTask();
                            return task::completed;
                        }

                        // ist mode manager status
                        // property changed to value
                        // other than inprogress and
                        // user requested value
                        // throw error message in
                        // task status and return
                        taskData->state = "Exception";
                        taskData->messages.emplace_back(
                            messages::resourceErrorsDetectedFormatError(
                                "NvidiaComputerSystem.ISTMode",
                                reqIstModVal + " Failed"));
                        taskData->finishTask();
                        return task::completed;
                    },
                            "type='signal',interface='org.freedesktop.DBus.Properties',"
                            "member='PropertiesChanged',path='" +
                                istMgrPath + "'");
                    task->startTimer(std::chrono::seconds(150));
                    task->populateResp(aResp->res);
                    task->payload.emplace(req);
                },
                    istMgrServ, istMgrPath, istMgrIface, "setISTMode",
                    setParam);
            },
                istMgrServ, istMgrPath, "org.freedesktop.DBus.Properties",
                "Get", istMgrIface, "Status");
        },
            service, path, "org.freedesktop.DBus.Properties", "Get", istIface,
            "ISTMode");
    },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/xyz/openbmc_project/mode/", int32_t(0),
        std::array<const char*, 1>{"xyz.openbmc_project.Control.Mode"});
}

} // namespace ist_mode_utils
} // namespace redfish
