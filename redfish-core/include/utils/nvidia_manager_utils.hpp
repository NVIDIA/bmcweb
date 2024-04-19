
#include <async_resp.hpp>
#include <sdbusplus/asio/connection.hpp>
namespace redfish
{

namespace nvidia_manager_util
{
/**
 * @brief Retrieves telemetry ready state data over DBus
 *
 * @param[in] aResp Shared pointer for completing asynchronous calls
 * @param[in] connectionName - service name
 * @param[in] path - object path
 * @return none
 */
inline void getOemManagerState(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                               const std::string& connectionName,
                               const std::string& path)
{
    BMCWEB_LOG_DEBUG("Get manager service Telemetry state.");
    crow::connections::systemBus->async_method_call(
        [aResp](const boost::system::error_code ec,
                const std::vector<std::pair<
                    std::string, std::variant<std::string>>>& propertiesList) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG("Error in getting manager service state");
            messages::internalError(aResp->res);
            return;
        }
        for (const std::pair<std::string, std::variant<std::string>>& property :
             propertiesList)
        {
            if (property.first == "FeatureType")
            {
                const std::string* value =
                    std::get_if<std::string>(&property.second);
                if (*value ==
                    "xyz.openbmc_project.State.FeatureReady.FeatureTypes.Manager")
                {
                    for (const std::pair<std::string,
                                         std::variant<std::string>>&
                             propertyItr : propertiesList)
                    {
                        if (propertyItr.first == "State")
                        {
                            const std::string* stateValue =
                                std::get_if<std::string>(&propertyItr.second);
                            if (stateValue == nullptr)
                            {
                                BMCWEB_LOG_DEBUG(
                                    "Null value returned for manager service state");
                                messages::internalError(aResp->res);
                                return;
                            }
                            std::string state = redfish::chassis_utils::
                                getFeatureReadyStateType(*stateValue);
                            aResp->res.jsonValue["Status"]["State"] = state;
                            if (state == "Enabled")
                            {
                                aResp->res.jsonValue["Status"]["Health"] = "OK";
                            }
                            else
                            {
                                aResp->res.jsonValue["Status"]["Health"] =
                                    "Critical";
                            }
                        }
                    }
                }
            }
        }
    },
        connectionName, path, "org.freedesktop.DBus.Properties", "GetAll",
        "xyz.openbmc_project.State.FeatureReady");
}

inline void
    getOemReadyState(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                     const std::string& bmcId)
{
    // call to get telemtery Ready status
    crow::connections::systemBus->async_method_call(
        [asyncResp, bmcId](
            const boost::system::error_code ec,
            const std::vector<std::pair<
                std::string,
                std::vector<std::pair<std::string, std::vector<std::string>>>>>&
                subtree) {
        if (ec)
        {
            // if platform doesn't support FeatureReady iface then report state
            // based on upstream only no failure reported
            return;
        }
        if (!subtree.empty())
        {
            // Iterate over all retrieved ObjectPaths.
            for (const std::pair<std::string,
                                 std::vector<std::pair<
                                     std::string, std::vector<std::string>>>>&
                     object : subtree)
            {
                const std::string& path = object.first;
                const std::vector<
                    std::pair<std::string, std::vector<std::string>>>&
                    connectionNames = object.second;

                const std::string& connectionName = connectionNames[0].first;
                const std::vector<std::string>& interfaces =
                    connectionNames[0].second;
                for (const auto& interfaceName : interfaces)
                {
                    if (interfaceName == "xyz.openbmc_project.State."
                                         "FeatureReady")
                    {
                        getOemManagerState(asyncResp, connectionName, path);
                    }
                }
            }
            return;
        }
        BMCWEB_LOG_ERROR(
            "Could not find interface xyz.openbmc_project.State.FeatureReady");
    },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree", "/", int32_t(0),
        std::array<const char*, 1>{"xyz.openbmc_project.State."
                                   "FeatureReady"});
}

} // namespace nvidia_manager_util
} // namespace redfish
