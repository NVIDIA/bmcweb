#pragma once

#include <app.hpp>
#include <dbus_utility.hpp>
#include <query.hpp>
#include <registries/privilege_registry.hpp>
#include <utils/sw_utils.hpp>

namespace redfish
{
namespace bios
{
/**
 * BiosConfig Manager Dbus info
 */
constexpr char const* biosConfigObj =
    "/xyz/openbmc_project/bios_config/manager";
constexpr char const* biosConfigIface =
    "xyz.openbmc_project.BIOSConfig.Manager";

/**
 * BiosService DBus types
 */
using BaseBIOSTable = boost::container::flat_map<
    std::string,
    std::tuple<std::string, bool, std::string, std::string, std::string,
               std::variant<int64_t, std::string, bool>,
               std::variant<int64_t, std::string, bool>,
               std::vector<std::tuple<std::string,
                                      std::variant<int64_t, std::string>>>>>;

using BaseBIOSTableItem = std::pair<
    std::string,
    std::tuple<std::string, bool, std::string, std::string, std::string,
               std::variant<int64_t, std::string, bool>,
               std::variant<int64_t, std::string, bool>,
               std::vector<std::tuple<std::string,
                                      std::variant<int64_t, std::string>>>>>;

using PendingAttrType = boost::container::flat_map<
    std::string,
    std::tuple<std::string, std::variant<int64_t, std::string, bool>>>;

using PendingAttrItemType = std::pair<
    std::string,
    std::tuple<std::string, std::variant<int64_t, std::string, bool>>>;

using AttrBoundType =
    std::tuple<std::string, std::variant<int64_t, std::string>>;

enum BaseBiosTableIndex
{
    baseBiosAttrType = 0,
    baseBiosReadonlyStatus,
    baseBiosDisplayName,
    baseBiosDescription,
    baseBiosMenuPath,
    baseBiosCurrValue,
    baseBiosDefaultValue,
    baseBiosBoundValues
};

enum BaseBiosBoundIndex
{
    baseBiosBoundType = 0,
    baseBiosBoundValue
};

enum BiosPendingAttributesIndex
{
    biosPendingAttrType = 0,
    biosPendingAttrValue
};

/**
 *@brief Translates Base BIOS Table attribute type from DBUS property value to
 *Redfish string type.
 *
 *@param[in] attrType The DBUS BIOS attribute type value
 *
 *@return Returns as a string, the attribute type required for Redfish.
 *If attribute type didn't match, then returns 'UNKNOWN' string.
 */
static std::string getBiosAttrType(const std::string& attrType)
{
    std::string type;
    if (attrType ==
        "xyz.openbmc_project.BIOSConfig.Manager.AttributeType.Enumeration")
    {
        type = "Enumeration";
    }
    else if (attrType ==
             "xyz.openbmc_project.BIOSConfig.Manager.AttributeType.String")
    {
        type = "String";
    }
    else if (attrType ==
             "xyz.openbmc_project.BIOSConfig.Manager.AttributeType.Password")
    {
        type = "Password";
    }
    else if (attrType ==
             "xyz.openbmc_project.BIOSConfig.Manager.AttributeType.Integer")
    {
        type = "Integer";
    }
    else if (attrType ==
             "xyz.openbmc_project.BIOSConfig.Manager.AttributeType.Boolean")
    {
        type = "Boolean";
    }
    else
    {
        type = "UNKNOWN";
    }
    return type;
}

/**
 *@brief Translates Base BIOS Table attribute type from Redfish string type to
 *DBUS property value.
 *
 *@param[in] attrType The Redfish BIOS attribute string type value
 *
 *@return Returns as a string, the attribute type required for DBUS.
 *If attribute type didn't match, then returns 'UNKNOWN' string.
 */
static std::string getDbusBiosAttrType(const std::string& attrType)
{
    std::string type;
    if (attrType == "Enumeration")
    {
        type =
            "xyz.openbmc_project.BIOSConfig.Manager.AttributeType.Enumeration";
    }
    else if (attrType == "String")
    {
        type = "xyz.openbmc_project.BIOSConfig.Manager.AttributeType.String";
    }
    else if (attrType == "Password")
    {
        type = "xyz.openbmc_project.BIOSConfig.Manager.AttributeType.Password";
    }
    else if (attrType == "Integer")
    {
        type = "xyz.openbmc_project.BIOSConfig.Manager.AttributeType.Integer";
    }
    else if (attrType == "Boolean")
    {
        type = "xyz.openbmc_project.BIOSConfig.Manager.AttributeType.Boolean";
    }
    else
    {
        type = "UNKNOWN";
    }
    return type;
}

/**
 *@brief Translates Base BIOS Table attribute bound value type from DBUS
 *property value to Redfish string type.
 *
 *@param[in] attrType The DBUS BIOS Bound value attribute type value
 *
 *@return Returns as a string, the attribute bound value type required for
 *Redfish. If attribute type didn't match, then returns 'UNKNOWN' string.
 */
static std::string getBiosBoundValType(const std::string& boundValType)
{
    std::string type;
    if (boundValType ==
        "xyz.openbmc_project.BIOSConfig.Manager.BoundType.ScalarIncrement")
    {
        type = "ScalarIncrement";
    }
    else if (boundValType ==
             "xyz.openbmc_project.BIOSConfig.Manager.BoundType.LowerBound")
    {
        type = "LowerBound";
    }
    else if (boundValType ==
             "xyz.openbmc_project.BIOSConfig.Manager.BoundType.UpperBound")
    {
        type = "UpperBound";
    }
    else if (boundValType ==
             "xyz.openbmc_project.BIOSConfig.Manager.BoundType.OneOf")
    {
        type = "OneOf";
    }
    else if (boundValType ==
             "xyz.openbmc_project.BIOSConfig.Manager.BoundType.MinStringLength")
    {
        type = "MinStringLength";
    }
    else if (boundValType ==
             "xyz.openbmc_project.BIOSConfig.Manager.BoundType.MaxStringLength")
    {
        type = "MaxStringLength";
    }
    else
    {
        type = "UNKNOWN";
    }
    return type;
}

/**
 *@brief Translates Reset BIOS to Default Settings status type from DBUS
 *property value to Redfish string type.
 *
 *@param[in] biosMode The DBUS BIOS Reset BIOS to Default Setting status value
 *
 *@return Returns as a string, the Reset BIOS Settings to default type required
 *for Redfish. If attribute type didn't match, then returns 'UNKNOWN' string.
 */
static std::string getBiosDefaultSettingsMode(const std::string& biosMode)
{
    std::string mode;
    if (biosMode == "xyz.openbmc_project.BIOSConfig.Manager.ResetFlag.NoAction")
    {
        mode = "NoAction";
    }
    else if (biosMode ==
             "xyz.openbmc_project.BIOSConfig.Manager.ResetFlag.FactoryDefaults")
    {
        mode = "FactoryDefaults";
    }
    else if (
        biosMode ==
        "xyz.openbmc_project.BIOSConfig.Manager.ResetFlag.FailSafeDefaults")
    {
        mode = "FailSafeDefaults";
    }
    else
    {
        mode = "UNKNOWN";
    }
    return mode;
}

/**
 *@brief sets the Reset BIOS Settings to default property.
 *
 * @param[in]       ResetBiosToDefaultsPending    Reset BIOS Settings to Default
 *status
 * @param[in,out]   asyncResp   Async HTTP response.
 *
 * @return None.
 */
[[maybe_unused]] static void
    setResetBiosSettings(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                         const bool& resetBiosToDefaultsPending)
{
    BMCWEB_LOG_DEBUG << "Set Reset Bios Settings to Defaults Pending Status";
    crow::connections::systemBus->async_method_call(
        [asyncResp, resetBiosToDefaultsPending](
            const boost::system::error_code ec,
            const dbus::utility::MapperGetObject& objType) {
            if (ec || objType.empty())
            {
                BMCWEB_LOG_DEBUG << "GetObject for path " << biosConfigObj;
                return;
            }

            const std::string& biosService = objType.begin()->first;

            std::string biosMode;
            if (resetBiosToDefaultsPending)
            {
                biosMode =
                    "xyz.openbmc_project.BIOSConfig.Manager.ResetFlag.FactoryDefaults";
            }
            else
            {
                biosMode =
                    "xyz.openbmc_project.BIOSConfig.Manager.ResetFlag.NoAction";
            }

            crow::connections::systemBus->async_method_call(
                [asyncResp](const boost::system::error_code ec2) {
                    if (ec2)
                    {
                        BMCWEB_LOG_DEBUG
                            << "DBUS response error for "
                               "Set Reset BIOS setting to default status.";
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    messages::success(asyncResp->res);
                },
                biosService, biosConfigObj, "org.freedesktop.DBus.Properties",
                "Set", biosConfigIface, "ResetBIOSSettings",
                std::variant<std::string>(biosMode));
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetObject", biosConfigObj,
        std::array<const char*, 1>{biosConfigIface});
}

/**
 *@brief Reads the Reset BIOS Settings to default property.
 *
 * @param[in,out]   asyncResp   Async HTTP response.
 *
 * @return None.
 */
static void
    getResetBiosSettings(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    BMCWEB_LOG_DEBUG << "Get Reset Bios Settings to Defaults Pending Status";
    crow::connections::systemBus->async_method_call(
        [asyncResp](const boost::system::error_code ec,
                    const dbus::utility::MapperGetObject& objType) {
            if (ec || objType.empty())
            {
                BMCWEB_LOG_DEBUG << "GetObject for path " << biosConfigObj;
                return;
            }

            const std::string& biosService = objType.begin()->first;

            crow::connections::systemBus->async_method_call(
                [asyncResp](
                    const boost::system::error_code ec2,
                    const std::variant<std::string>& resetBiosSettingsMode) {
                    if (ec2)
                    {
                        BMCWEB_LOG_DEBUG
                            << "DBUS response error for "
                               "Get Reset BIOS setting to default status.";
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    const std::string* value =
                        std::get_if<std::string>(&resetBiosSettingsMode);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_DEBUG
                            << "Null value returned for Reset BIOS Settings status";
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    std::string biosMode = getBiosDefaultSettingsMode(*value);

                    if (biosMode == "NoAction")
                    {
                        asyncResp->res.jsonValue["ResetBiosToDefaultsPending"] =
                            false;
                    }
                    else if ((biosMode == "FactoryDefaults") ||
                             (biosMode == "FailSafeDefaults"))
                    {
                        asyncResp->res.jsonValue["ResetBiosToDefaultsPending"] =
                            true;
                    }
                    else
                    {
                        BMCWEB_LOG_DEBUG
                            << "Invalid Reset BIOS Settings Status";
                        messages::internalError(asyncResp->res);
                        return;
                    }
                },
                biosService, biosConfigObj, "org.freedesktop.DBus.Properties",
                "Get", biosConfigIface, "ResetBIOSSettings");
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetObject", biosConfigObj,
        std::array<const char*, 1>{biosConfigIface});
}

/**
 *@brief Reads the BIOS Base Table DBUS property and update the Bios Attributes
 *response.
 *
 * @param[in,out]   asyncResp   Async HTTP response.
 *
 * @return None.
 */
static void
    getBiosAttributes(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp](const boost::system::error_code ec,
                    const dbus::utility::MapperGetObject& objType) {
            if (ec || objType.empty())
            {
                BMCWEB_LOG_DEBUG << "GetObject for path " << biosConfigObj;
                return;
            }

            const std::string& biosService = objType.begin()->first;
            crow::connections::systemBus->async_method_call(
                [asyncResp](
                    const boost::system::error_code ec2,
                    const std::variant<BaseBIOSTable>& baseBiosTableResp) {
                    if (ec2)
                    {
                        BMCWEB_LOG_ERROR
                            << "Get BaseBIOSTable DBus response error" << ec2;
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    const BaseBIOSTable* baseBiosTable =
                        std::get_if<BaseBIOSTable>(&baseBiosTableResp);

                    nlohmann::json& attributesJson =
                        asyncResp->res.jsonValue["Attributes"];
                    if (baseBiosTable == nullptr)
                    {
                        BMCWEB_LOG_ERROR << "Empty BaseBIOSTable";
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    for (const BaseBIOSTableItem& attrIt : *baseBiosTable)
                    {
                        const std::string& attr = attrIt.first;

                        // read the attribute type at 0th field and convert from
                        // dbus to string format
                        std::string attrType = getBiosAttrType(std::string(
                            std::get<BaseBiosTableIndex::baseBiosAttrType>(
                                attrIt.second)));
                        if ((attrType == "String") ||
                            (attrType == "Enumeration"))
                        {
                            // read the current value of attribute at 5th field
                            const std::string* attrCurrValue =
                                std::get_if<std::string>(
                                    &std::get<
                                        BaseBiosTableIndex::baseBiosCurrValue>(
                                        attrIt.second));
                            if (attrCurrValue != nullptr)
                            {
                                attributesJson.emplace(attr, *attrCurrValue);
                            }
                            else
                            {
                                attributesJson.emplace(attr, std::string(""));
                            }
                        }
                        else if ((attrType == "Integer") ||
                                 (attrType == "Boolean"))
                        {
                            // read the current value of attribute at 5th field
                            const int64_t* attrCurrValue = std::get_if<int64_t>(
                                &std::get<
                                    BaseBiosTableIndex::baseBiosCurrValue>(
                                    attrIt.second));
                            if (attrCurrValue != nullptr)
                            {
                                if (attrType == "Boolean")
                                {
                                    if (*attrCurrValue)
                                    {
                                        attributesJson.emplace(attr, true);
                                    }
                                    else
                                    {
                                        attributesJson.emplace(attr, false);
                                    }
                                }
                                else
                                {
                                    attributesJson.emplace(attr,
                                                           *attrCurrValue);
                                }
                            }
                            else
                            {
                                if (attrType == "Boolean")
                                {
                                    attributesJson.emplace(attr, false);
                                }
                                else
                                {
                                    attributesJson.emplace(attr, 0);
                                }
                            }
                        }
                        else
                        {
                            BMCWEB_LOG_ERROR << "Attribute type not supported";
                        }
                    }
                },
                biosService, biosConfigObj, "org.freedesktop.DBus.Properties",
                "Get", biosConfigIface, "BaseBIOSTable");
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetObject", biosConfigObj,
        std::array<const char*, 1>{biosConfigIface});
}

/**
 *@brief Validates the requested BIOS Base Table JSON with the required
 *attribute format.
 *
 * @param[in]	    attrJson	BIOS Attribute JSON
 * @param[in,out]   asyncResp   Async HTTP response.
 *
 * @return Returns as a bool flag, true if attribute json is in valid format,
 * or else returns false.
 */
static bool isValidAttrJson(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const nlohmann::json& attrJson)
{
    const std::vector<std::string> stringRequired{
        "AttributeName", "DisplayName", "Description", "MenuPath", "Type"};
    const std::vector<std::string> booleanRequired{"ReadOnly"};
    const std::vector<std::string> valueTypeRequired{"CurrentValue",
                                                     "DefaultValue"};
    const std::vector<std::string> integerAddition{"LowerBound", "UpperBound",
                                                   "ScalarIncrement"};
    const std::vector<std::string> stringAddition{"MinLength", "MaxLength"};
    const std::string enumerationAddition{"Values"};

    // checking existence of required keys
    for (const auto& key : stringRequired)
    {
        if (!attrJson.contains(key))
        {
            messages::propertyMissing(asyncResp->res, key);
            BMCWEB_LOG_ERROR << "Required propery missing in req!";
            return false;
        }
        if (!attrJson[key].is_string())
        {
            messages::propertyValueTypeError(asyncResp->res,
                                             attrJson[key].dump(), key);
            BMCWEB_LOG_ERROR << "Attribute type is not valid in req!";
            return false;
        }
    }

    for (const auto& key : booleanRequired)
    {
        if (!attrJson.contains(key))
        {
            messages::propertyMissing(asyncResp->res, key);
            BMCWEB_LOG_ERROR << "Required propery missing in req!";
            return false;
        }
        if (!attrJson[key].is_boolean())
        {
            messages::propertyValueTypeError(asyncResp->res,
                                             attrJson[key].dump(), key);
            BMCWEB_LOG_ERROR << "Attribute type is not valid in req!";
            return false;
        }
    }

    for (const auto& key : valueTypeRequired)
    {
        if (!attrJson.contains(key))
        {
            messages::propertyMissing(asyncResp->res, key);
            BMCWEB_LOG_ERROR << "Required propery missing in req!";
            return false;
        }

        bool propertyValueTypeValid = false;
        if ((attrJson["Type"] == "Enumeration" && attrJson[key].is_string()) ||
            (attrJson["Type"] == "String" && attrJson[key].is_string()) ||
            (attrJson["Type"] == "Integer" && attrJson[key].is_number()) ||
            (attrJson["Type"] == "Boolean" && attrJson[key].is_boolean()) ||
            (key == "DefaultValue" && attrJson[key].is_null()))
        {
            propertyValueTypeValid = true;
        }

        if (propertyValueTypeValid == false)
        {
            messages::propertyValueTypeError(asyncResp->res,
                                             attrJson[key].dump(), key);
            BMCWEB_LOG_ERROR << "Attribute type is not valid in req!";
            return false;
        }
    }

    if (attrJson["Type"] == "Integer")
    {
        for (const auto& key : integerAddition)
        {
            if (!attrJson.contains(key))
            {
                messages::propertyMissing(asyncResp->res, key);
                BMCWEB_LOG_ERROR << "Required propery missing in req!";
                return false;
            }
            if (!attrJson[key].is_number())
            {
                messages::propertyValueTypeError(asyncResp->res,
                                                 attrJson[key].dump(), key);
                BMCWEB_LOG_ERROR << "Attribute type is not valid in req!";
                return false;
            }
        }
    }

    if (attrJson["Type"] == "String")
    {
        for (const auto& key : stringAddition)
        {
            if (!attrJson.contains(key))
            {
                messages::propertyMissing(asyncResp->res, key);
                BMCWEB_LOG_ERROR << "Required propery missing in req!";
                return false;
            }
            if (!attrJson[key].is_number())
            {
                messages::propertyValueTypeError(asyncResp->res,
                                                 attrJson[key].dump(), key);
                BMCWEB_LOG_ERROR << "Attribute type is not valid in req!";
                return false;
            }
        }
    }

    if (attrJson["Type"] == "Enumeration")
    {
        const auto& key = enumerationAddition;
        if (!attrJson.contains(key))
        {
            messages::propertyMissing(asyncResp->res, key);
            BMCWEB_LOG_ERROR << "Required propery missing in req!";
            return false;
        }
        if (!attrJson[key].is_array())
        {
            messages::propertyValueTypeError(asyncResp->res,
                                             attrJson[key].dump(), key);
            BMCWEB_LOG_ERROR << "Attribute type is not valid in req!";
            return false;
        }
        if (attrJson[key].empty())
        {
            messages::propertyValueIncorrect(asyncResp->res, key,
                                             attrJson[key].dump());
            BMCWEB_LOG_ERROR << "Attribute type is not valid in req!";
            return false;
        }
        if (!attrJson[key][0].is_string())
        {
            messages::propertyValueIncorrect(asyncResp->res, key,
                                             attrJson[key].dump());
            BMCWEB_LOG_ERROR << "Attribute type is not valid in req!";
            return false;
        }
    }

    if (attrJson["AttributeName"].empty())
    {
        messages::propertyValueIncorrect(asyncResp->res, "AttributeName",
                                         "empty");
        BMCWEB_LOG_ERROR << "AttributeName is not valid in req!";
        return false;
    }
    return true;
}

/**
 *@brief Sets the BIOS Base Table DBUS property with requested BIOS default
 *attributes.
 *
 * @param[in]	    baseBiosTableJson BIOS Base Table default Attribute details
 * @param[in,out]   asyncResp   Async HTTP response.
 *
 * @return Returns None.
 */
static void fillBiosTable(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          const std::vector<nlohmann::json>& baseBiosTableJson)
{
    BaseBIOSTable baseBiosTable;
    for (const nlohmann::json& attrJson : baseBiosTableJson)
    {
        // Check all the fields are present
        if (!isValidAttrJson(asyncResp, attrJson))
        {
            BMCWEB_LOG_ERROR << "Req attributes are missing!";
            return;
        }

        std::string attr;
        std::string attrDispName;
        std::string attrDescr;
        std::string attrMenuPath;
        std::string attrType;
        bool attrReadOnly = false;
        std::vector<std::tuple<std::string, std::variant<int64_t, std::string>>>
            attrValues;

        attr = attrJson["AttributeName"].get<std::string>();
        attrDispName = attrJson["DisplayName"].get<std::string>();
        attrDescr = attrJson["Description"].get<std::string>();
        attrMenuPath = attrJson["MenuPath"].get<std::string>();
        attrType = attrJson["Type"].get<std::string>();
        attrReadOnly = attrJson["ReadOnly"].get<bool>();

        if ((attrType == "String") || (attrType == "Enumeration"))
        {
            std::string currVal = attrJson["CurrentValue"].get<std::string>();

            // read and update the bound values
            if (attrType == "Enumeration")
            {
                for (const auto& value :
                     attrJson["Values"].get<std::vector<std::string>>())
                {
                    attrValues.emplace_back(
                        "xyz.openbmc_project.BIOSConfig.Manager.BoundType.OneOf",
                        value);
                }
            }
            else if (attrType == "String")
            {
                const auto& minLength = attrJson["MinLength"].get<int64_t>();
                attrValues.emplace_back(
                    "xyz.openbmc_project.BIOSConfig.Manager.BoundType.MinStringLength",
                    minLength);
                const auto& maxLength = attrJson["MaxLength"].get<int64_t>();
                attrValues.emplace_back(
                    "xyz.openbmc_project.BIOSConfig.Manager.BoundType.MaxStringLength",
                    maxLength);
            }
            attrType = getDbusBiosAttrType(attrType);
            if (attrJson["DefaultValue"].is_null())
            {
                // put a incorrect type to indicate null
                int64_t defaultVal = 0;
                baseBiosTable.insert(std::make_pair(
                    attr, std::make_tuple(attrType, attrReadOnly, attrDispName,
                                          attrDescr, attrMenuPath, currVal,
                                          defaultVal, attrValues)));
            }
            else
            {
                std::string defaultVal =
                    attrJson["DefaultValue"].get<std::string>();
                baseBiosTable.insert(std::make_pair(
                    attr, std::make_tuple(attrType, attrReadOnly, attrDispName,
                                          attrDescr, attrMenuPath, currVal,
                                          defaultVal, attrValues)));
            }
        }
        else if (attrType == "Integer")
        {
            int64_t currVal = attrJson["CurrentValue"].get<int64_t>();

            // read and update the bound values
            attrValues.emplace_back(
                "xyz.openbmc_project.BIOSConfig.Manager.BoundType.LowerBound",
                attrJson["LowerBound"].get<int64_t>());
            attrValues.emplace_back(
                "xyz.openbmc_project.BIOSConfig.Manager.BoundType.UpperBound",
                attrJson["UpperBound"].get<int64_t>());
            attrValues.emplace_back(
                "xyz.openbmc_project.BIOSConfig.Manager.BoundType.ScalarIncrement",
                attrJson["ScalarIncrement"].get<int64_t>());

            attrType = getDbusBiosAttrType(attrType);
            if (attrJson["DefaultValue"].is_null())
            {
                // put a incorrect type to indicate null
                std::string defaultVal;
                baseBiosTable.insert(std::make_pair(
                    attr, std::make_tuple(attrType, attrReadOnly, attrDispName,
                                          attrDescr, attrMenuPath, currVal,
                                          defaultVal, attrValues)));
            }
            else
            {
                int64_t defaultVal = attrJson["DefaultValue"].get<int64_t>();
                baseBiosTable.insert(std::make_pair(
                    attr, std::make_tuple(attrType, attrReadOnly, attrDispName,
                                          attrDescr, attrMenuPath, currVal,
                                          defaultVal, attrValues)));
            }
        }
        else if (attrType == "Boolean")
        {
            // for Boolean type, BaseBIOSTable DBus method will expect the data
            // in the int64_t type
            int64_t currVal =
                static_cast<int64_t>(attrJson["CurrentValue"].get<bool>());

            attrType = getDbusBiosAttrType(attrType);
            if (attrJson["DefaultValue"].is_null())
            {
                // put a incorrect type to indicate null
                std::string defaultVal;
                baseBiosTable.insert(std::make_pair(
                    attr, std::make_tuple(attrType, attrReadOnly, attrDispName,
                                          attrDescr, attrMenuPath, currVal,
                                          defaultVal, attrValues)));
            }
            else
            {
                int64_t defaultVal =
                    static_cast<int64_t>(attrJson["DefaultValue"].get<bool>());
                baseBiosTable.insert(std::make_pair(
                    attr, std::make_tuple(attrType, attrReadOnly, attrDispName,
                                          attrDescr, attrMenuPath, currVal,
                                          defaultVal, attrValues)));
            }
        }
        else
        {
            messages::propertyValueIncorrect(asyncResp->res, "Type", "UNKNOWN");
            BMCWEB_LOG_ERROR << "Attribute Type is not valid in req!";
            return;
        }
    }

    crow::connections::systemBus->async_method_call(
        [asyncResp, baseBiosTable](const boost::system::error_code ec) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG << "Error occurred in setting BaseBIOSTable";
                messages::internalError(asyncResp->res);
                return;
            }

            messages::success(asyncResp->res);
        },
        "xyz.openbmc_project.BIOSConfigManager",
        "/xyz/openbmc_project/bios_config/manager",
        "org.freedesktop.DBus.Properties", "Set",
        "xyz.openbmc_project.BIOSConfig.Manager", "BaseBIOSTable",
        std::variant<BaseBIOSTable>(baseBiosTable));
}

/**
 *@brief Reads the BIOS Pending Attributes, which are updated by oob the user
 * and update the Bios Settings Attributes response.
 *
 * @param[in,out]   asyncResp   Async HTTP response.
 *
 * @return None.
 */
static void
    getBiosSettingsAttr(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp](const boost::system::error_code ec,
                    const dbus::utility::MapperGetObject& objType) {
            if (ec || objType.empty())
            {
                BMCWEB_LOG_DEBUG << "GetObject for path " << biosConfigObj;
                return;
            }

            const std::string& biosService = objType.begin()->first;
            crow::connections::systemBus->async_method_call(
                [asyncResp](
                    const boost::system::error_code ec2,
                    const std::variant<PendingAttrType>& pendingAttrsResp) {
                    if (ec2)
                    {
                        BMCWEB_LOG_ERROR
                            << "Get PendingAttributes DBus response error"
                            << ec2;
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    const PendingAttrType* pendingAttrs =
                        std::get_if<PendingAttrType>(&pendingAttrsResp);

                    nlohmann::json& attributesJson =
                        asyncResp->res.jsonValue["Attributes"];
                    if (pendingAttrs == nullptr)
                    {
                        BMCWEB_LOG_ERROR << "Empty Pending Attributes";
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    for (const PendingAttrItemType& attrIt : *pendingAttrs)
                    {
                        const std::string& attr = attrIt.first;

                        // read the attribute type at 0th field and convert from
                        // dbus to string format
                        std::string attrType = getBiosAttrType(std::string(
                            std::get<BiosPendingAttributesIndex::
                                         biosPendingAttrType>(attrIt.second)));
                        if ((attrType == "String") ||
                            (attrType == "Enumeration"))
                        {
                            // read the current value of attribute at 1st field
                            const std::string* attrCurrValue =
                                std::get_if<std::string>(
                                    &std::get<BiosPendingAttributesIndex::
                                                  biosPendingAttrValue>(
                                        attrIt.second));
                            if (attrCurrValue != nullptr)
                            {
                                attributesJson.emplace(attr, *attrCurrValue);
                            }
                            else
                            {
                                attributesJson.emplace(attr, std::string(""));
                            }
                        }
                        else if ((attrType == "Integer") ||
                                 (attrType == "Boolean"))
                        {
                            // read the current value of attribute at 1st field
                            const int64_t* attrCurrValue = std::get_if<int64_t>(
                                &std::get<BiosPendingAttributesIndex::
                                              biosPendingAttrValue>(
                                    attrIt.second));
                            if (attrCurrValue != nullptr)
                            {
                                if (attrType == "Boolean")
                                {
                                    if (*attrCurrValue)
                                    {
                                        attributesJson.emplace(attr, true);
                                    }
                                    else
                                    {
                                        attributesJson.emplace(attr, false);
                                    }
                                }
                                else
                                {
                                    attributesJson.emplace(attr,
                                                           *attrCurrValue);
                                }
                            }
                            else
                            {
                                if (attrType == "Boolean")
                                {
                                    attributesJson.emplace(attr, false);
                                }
                                else
                                {
                                    attributesJson.emplace(attr, 0);
                                }
                            }
                        }
                        else
                        {
                            BMCWEB_LOG_ERROR << "Attribute type not supported";
                        }
                    }
                },
                biosService, biosConfigObj, "org.freedesktop.DBus.Properties",
                "Get", biosConfigIface, "PendingAttributes");
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetObject", biosConfigObj,
        std::array<const char*, 1>{biosConfigIface});
}

/**
 *@brief Updates the BIOS Pending Attributes DBUS property, which are requested
 *by the oob user.
 *
 * @param[in,out]   asyncResp   Async HTTP response.
 *
 * @return None.
 */
static void
    setBiosPendingAttr(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                       const nlohmann::json& pendingAttrJson)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp,
         pendingAttrJson](const boost::system::error_code ec,
                          const dbus::utility::MapperGetObject& objType) {
            if (ec || objType.empty())
            {
                BMCWEB_LOG_DEBUG << "GetObject for path " << biosConfigObj;
                return;
            }

            const std::string& biosService = objType.begin()->first;
            crow::connections::systemBus->async_method_call(
                [asyncResp, pendingAttrJson, biosService](
                    const boost::system::error_code ec2,
                    const std::variant<BaseBIOSTable>& baseBiosTableResp) {
                    if (ec2)
                    {
                        BMCWEB_LOG_ERROR
                            << "Get BaseBIOSTable DBus response error" << ec2;
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    const BaseBIOSTable* baseBiosTable =
                        std::get_if<BaseBIOSTable>(&baseBiosTableResp);

                    if (baseBiosTable == nullptr)
                    {
                        BMCWEB_LOG_ERROR << "Empty BaseBIOSTable";
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    PendingAttrType pendingAttrs{};
                    for (const auto& pendingAttrIt : pendingAttrJson.items())
                    {

                        // Check whether the requested attribute is available
                        // inside BaseBIOSTable or not
                        auto attrIt = baseBiosTable->find(pendingAttrIt.key());
                        if (attrIt == baseBiosTable->end())
                        {
                            BMCWEB_LOG_ERROR << "Not Found Attribute "
                                             << pendingAttrIt.key();
                            messages::propertyValueNotInList(
                                asyncResp->res, pendingAttrIt.key(),
                                "Attributes");
                            return;
                        }

                        // read the attribute type at 0th field and convert from
                        // dbus to string format
                        std::string attrItType =
                            std::get<BaseBiosTableIndex::baseBiosAttrType>(
                                attrIt->second);
                        std::string attrType = getBiosAttrType(attrItType);
                        if ((attrType == "String") ||
                            (attrType == "Enumeration"))
                        {
                            if (!pendingAttrIt.value().is_string())
                            {
                                BMCWEB_LOG_ERROR
                                    << "Requested Attribute Value invalid";
                                messages::propertyValueTypeError(
                                    asyncResp->res,
                                    std::string(pendingAttrIt.value()),
                                    pendingAttrIt.key());
                                return;
                            }
                            std::string attrReqVal = pendingAttrIt.value();

                            if (attrType == "Enumeration")
                            {
                                // read the bound values for the attribute
                                const std::vector<AttrBoundType> boundValues =
                                    std::get<BaseBiosTableIndex::
                                                 baseBiosBoundValues>(
                                        attrIt->second);
                                bool found = false;

                                for (const AttrBoundType& boundValueIt :
                                     boundValues)
                                {
                                    // read the bound value type at 0th field
                                    // and convert from dbus to string format
                                    std::string boundValType =
                                        getBiosBoundValType(std::string(
                                            std::get<BaseBiosBoundIndex::
                                                         baseBiosBoundType>(
                                                boundValueIt)));

                                    if (boundValType == "OneOf")
                                    {
                                        // read the bound value  at 1st field
                                        // for each entry
                                        const std::string* currBoundVal =
                                            std::get_if<std::string>(
                                                &std::get<
                                                    BaseBiosBoundIndex::
                                                        baseBiosBoundValue>(
                                                    boundValueIt));
                                        if (currBoundVal == nullptr)
                                        {
                                            BMCWEB_LOG_ERROR
                                                << "Bound Value not found";
                                            continue;
                                        }
                                        if (attrReqVal == *currBoundVal)
                                        {
                                            found = true;
                                        }
                                    }
                                    else
                                    {
                                        continue;
                                    }
                                }

                                if (!found)
                                {
                                    BMCWEB_LOG_ERROR
                                        << "Requested Attribute Value invalid";
                                    messages::internalError(asyncResp->res);
                                    return;
                                }
                            }
                            else if (attrType == "String")
                            {
                                const std::vector<AttrBoundType> boundValues =
                                    std::get<BaseBiosTableIndex::
                                                 baseBiosBoundValues>(
                                        attrIt->second);
                                bool valid = true;

                                for (const AttrBoundType& boundValueIt :
                                     boundValues)
                                {
                                    // read the bound value type at 0th field
                                    // and convert from dbus to string format
                                    std::string boundValType =
                                        getBiosBoundValType(std::string(
                                            std::get<BaseBiosBoundIndex::
                                                         baseBiosBoundType>(
                                                boundValueIt)));

                                    if (boundValType == "MinStringLength")
                                    {
                                        const int64_t* currBoundVal =
                                            std::get_if<int64_t>(
                                                &std::get<
                                                    BaseBiosBoundIndex::
                                                        baseBiosBoundValue>(
                                                    boundValueIt));
                                        if (currBoundVal == nullptr)
                                        {
                                            BMCWEB_LOG_ERROR
                                                << "Bound Value not found";
                                            continue;
                                        }
                                        if (static_cast<int64_t>(
                                                attrReqVal.size()) <
                                            *currBoundVal)
                                        {
                                            valid = false;
                                        }
                                    }
                                    else if (boundValType == "MaxStringLength")
                                    {
                                        const int64_t* currBoundVal =
                                            std::get_if<int64_t>(
                                                &std::get<
                                                    BaseBiosBoundIndex::
                                                        baseBiosBoundValue>(
                                                    boundValueIt));
                                        if (currBoundVal == nullptr)
                                        {
                                            BMCWEB_LOG_ERROR
                                                << "Bound Value not found";
                                            continue;
                                        }
                                        if (static_cast<int64_t>(
                                                attrReqVal.size()) >
                                            *currBoundVal)
                                        {
                                            valid = false;
                                        }
                                    }
                                    else
                                    {
                                        continue;
                                    }
                                }

                                if (!valid)
                                {
                                    BMCWEB_LOG_ERROR
                                        << "Requested Attribute Value invalid";
                                    messages::propertyValueOutOfRange(
                                        asyncResp->res, attrReqVal,
                                        pendingAttrIt.key());
                                    return;
                                }
                            }
                            pendingAttrs.insert(std::make_pair(
                                pendingAttrIt.key(),
                                std::make_tuple(attrItType, attrReqVal)));
                        }
                        else if (attrType == "Boolean")
                        {
                            if (!pendingAttrIt.value().is_boolean())
                            {
                                BMCWEB_LOG_ERROR
                                    << "Requested Attribute Value invalid";
                                messages::propertyValueTypeError(
                                    asyncResp->res,
                                    std::string(pendingAttrIt.value()),
                                    pendingAttrIt.key());
                                return;
                            }
                            int64_t attrReqVal = static_cast<int64_t>(
                                pendingAttrIt.value().get<bool>());
                            pendingAttrs.insert(std::make_pair(
                                pendingAttrIt.key(),
                                std::make_tuple(attrItType, attrReqVal)));
                        }
                        else if (attrType == "Integer")
                        {
                            if (!pendingAttrIt.value().is_number())
                            {
                                BMCWEB_LOG_ERROR
                                    << "Requested Attribute Value invalid";
                                messages::propertyValueTypeError(
                                    asyncResp->res,
                                    std::string(pendingAttrIt.value()),
                                    pendingAttrIt.key());
                                return;
                            }
                            int64_t attrReqVal = pendingAttrIt.value();
                            pendingAttrs.emplace(
                                pendingAttrIt.key(),
                                std::make_tuple(attrItType, attrReqVal));
                        }
                        else
                        {
                            BMCWEB_LOG_ERROR << "Unknown Attribute Type"
                                             << attrType;
                            messages::internalError(asyncResp->res);
                            return;
                        }
                    }

                    crow::connections::systemBus->async_method_call(
                        [asyncResp](const boost::system::error_code ec3) {
                            if (ec3)
                            {
                                BMCWEB_LOG_ERROR
                                    << "Set PendingAttributes failed " << ec3;
                                messages::internalError(asyncResp->res);
                                return;
                            }

                            messages::success(asyncResp->res);
                        },
                        biosService, biosConfigObj,
                        "org.freedesktop.DBus.Properties", "Set",
                        biosConfigIface, "PendingAttributes",
                        std::variant<PendingAttrType>(pendingAttrs));
                },
                biosService, biosConfigObj, "org.freedesktop.DBus.Properties",
                "Get", biosConfigIface, "BaseBIOSTable");
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetObject", biosConfigObj,
        std::array<const char*, 1>{biosConfigIface});
}

/**
 *@brief Reads the BIOS Base Table DBUS property and update the Bios Attribute
 *Registry response.
 *
 * @param[in,out]   asyncResp   Async HTTP response.
 *
 * @return None.
 */
static void getBiosAttributeRegistry(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp](const boost::system::error_code ec,
                    const dbus::utility::MapperGetObject& objType) {
            if (ec || objType.empty())
            {
                BMCWEB_LOG_DEBUG << "GetObject for path " << biosConfigObj;
                return;
            }

            const std::string& biosService = objType.begin()->first;
            crow::connections::systemBus->async_method_call(
                [asyncResp](
                    const boost::system::error_code ec2,
                    const std::variant<BaseBIOSTable>& baseBiosTableResp) {
                    if (ec2)
                    {
                        BMCWEB_LOG_ERROR
                            << "Get BaseBIOSTable DBus response error" << ec2;
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    const BaseBIOSTable* baseBiosTable =
                        std::get_if<BaseBIOSTable>(&baseBiosTableResp);

                    nlohmann::json& attributeArray =
                        asyncResp->res
                            .jsonValue["RegistryEntries"]["Attributes"];

                    if (baseBiosTable == nullptr)
                    {
                        BMCWEB_LOG_ERROR << "Empty BaseBIOSTable";
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    for (const BaseBIOSTableItem& attrIt : *baseBiosTable)
                    {
                        // read the attribute type at 0th field and convert from
                        // dbus to string format
                        std::string attrType = getBiosAttrType(std::string(
                            std::get<BaseBiosTableIndex::baseBiosAttrType>(
                                attrIt.second)));

                        if (attrType == "UNKNOWN")
                        {
                            BMCWEB_LOG_ERROR << "Attribute type not supported";
                            continue;
                        }

                        nlohmann::json attributeIt;
                        attributeIt["AttributeName"] = attrIt.first;
                        attributeIt["Type"] = attrType;
                        attributeIt["ReadOnly"] = std::get<
                            BaseBiosTableIndex::baseBiosReadonlyStatus>(
                            attrIt.second);
                        attributeIt["DisplayName"] =
                            std::get<BaseBiosTableIndex::baseBiosDisplayName>(
                                attrIt.second);
                        const std::string& helpText =
                            std::get<BaseBiosTableIndex::baseBiosDescription>(
                                attrIt.second);
                        if (!helpText.empty())
                        {
                            attributeIt["HelpText"] = helpText;
                        }
                        attributeIt["MenuPath"] =
                            std::get<BaseBiosTableIndex::baseBiosMenuPath>(
                                attrIt.second);

                        if ((attrType == "String") ||
                            (attrType == "Enumeration"))
                        {
                            // read the current value of attribute at 5th field
                            const std::string* attrCurrValue =
                                std::get_if<std::string>(
                                    &std::get<
                                        BaseBiosTableIndex::baseBiosCurrValue>(
                                        attrIt.second));
                            if (attrCurrValue != nullptr)
                            {
                                attributeIt["CurrentValue"] = *attrCurrValue;
                            }
                            else
                            {
                                attributeIt["CurrentValue"] = nullptr;
                            }

                            // read the default value of attribute at 6th field
                            const std::string* attrDefaultValue = std::get_if<
                                std::string>(
                                &std::get<
                                    BaseBiosTableIndex::baseBiosDefaultValue>(
                                    attrIt.second));
                            if (attrDefaultValue != nullptr)
                            {
                                attributeIt["DefaultValue"] = *attrDefaultValue;
                            }
                            else
                            {
                                attributeIt["DefaultValue"] = nullptr;
                            }
                        }
                        else if ((attrType == "Integer") ||
                                 (attrType == "Boolean"))
                        {
                            // read the current value of attribute at 5th field
                            const int64_t* attrCurrValue = std::get_if<int64_t>(
                                &std::get<
                                    BaseBiosTableIndex::baseBiosCurrValue>(
                                    attrIt.second));
                            if (attrCurrValue != nullptr)
                            {
                                if (attrType == "Boolean")
                                {
                                    if (*attrCurrValue)
                                    {
                                        attributeIt["CurrentValue"] = true;
                                    }
                                    else
                                    {
                                        attributeIt["CurrentValue"] = false;
                                    }
                                }
                                else
                                {
                                    attributeIt["CurrentValue"] =
                                        *attrCurrValue;
                                }
                            }
                            else
                            {
                                attributeIt["CurrentValue"] = nullptr;
                            }

                            // read the current value of attribute at 6th field
                            const int64_t* attrDefaultValue = std::get_if<
                                int64_t>(
                                &std::get<
                                    BaseBiosTableIndex::baseBiosDefaultValue>(
                                    attrIt.second));
                            if (attrDefaultValue != nullptr)
                            {
                                if (attrType == "Boolean")
                                {
                                    if (*attrDefaultValue)
                                    {
                                        attributeIt["DefaultValue"] = true;
                                    }
                                    else
                                    {
                                        attributeIt["DefaultValue"] = false;
                                    }
                                }
                                else
                                {
                                    attributeIt["DefaultValue"] =
                                        *attrDefaultValue;
                                }
                            }
                            else
                            {
                                attributeIt["DefaultValue"] = nullptr;
                            }
                        }

                        nlohmann::json boundValArray = nlohmann::json::array();

                        // read the bound values for the attribute
                        const std::vector<AttrBoundType> boundValues =
                            std::get<BaseBiosTableIndex::baseBiosBoundValues>(
                                attrIt.second);

                        for (const AttrBoundType& boundValueIt : boundValues)
                        {
                            nlohmann::json boundValJson;

                            // read the bound value type at 0th field
                            // and convert from dbus to string format
                            std::string boundValType =
                                getBiosBoundValType(std::string(
                                    std::get<
                                        BaseBiosBoundIndex::baseBiosBoundType>(
                                        boundValueIt)));

                            if (boundValType == "UNKNOWN")
                            {
                                BMCWEB_LOG_ERROR
                                    << "Attribute type not supported";
                                continue;
                            }

                            if (boundValType == "OneOf")
                            {
                                if ((attrType == "String") ||
                                    (attrType == "Enumeration"))
                                {
                                    // read the bound value  at 1st field
                                    // for each entry
                                    const std::string* currBoundVal =
                                        std::get_if<std::string>(
                                            &std::get<BaseBiosBoundIndex::
                                                          baseBiosBoundValue>(
                                                boundValueIt));
                                    if (currBoundVal != nullptr)
                                    {
                                        boundValJson["ValueName"] =
                                            *currBoundVal;
                                    }
                                    else
                                    {
                                        boundValJson["ValueName"] = "";
                                    }
                                }
                                else if (attrType == "Boolean")
                                {
                                    // read the bound value  at 1st field
                                    // for each entry
                                    const int64_t* currBoundVal =
                                        std::get_if<int64_t>(
                                            &std::get<BaseBiosBoundIndex::
                                                          baseBiosBoundValue>(
                                                boundValueIt));
                                    if (currBoundVal != nullptr)
                                    {
                                        if (*currBoundVal)
                                        {
                                            boundValJson["ValueName"] = true;
                                        }
                                        else
                                        {
                                            boundValJson["ValueName"] = false;
                                        }
                                    }
                                    else
                                    {
                                        boundValJson["ValueName"] = false;
                                    }
                                }
                                else
                                {
                                    continue;
                                }
                            }
                            else if (boundValType == "LowerBound")
                            {
                                const int64_t* currBoundVal = std::get_if<
                                    int64_t>(
                                    &std::get<
                                        BaseBiosBoundIndex::baseBiosBoundValue>(
                                        boundValueIt));
                                if (currBoundVal != nullptr)
                                {
                                    attributeIt["LowerBound"] = *currBoundVal;
                                }
                                else
                                {
                                    attributeIt["LowerBound"] = 0;
                                }
                            }
                            else if (boundValType == "UpperBound")
                            {
                                const int64_t* currBoundVal = std::get_if<
                                    int64_t>(
                                    &std::get<
                                        BaseBiosBoundIndex::baseBiosBoundValue>(
                                        boundValueIt));
                                if (currBoundVal != nullptr)
                                {
                                    attributeIt["UpperBound"] = *currBoundVal;
                                }
                                else
                                {
                                    attributeIt["UpperBound"] = 0;
                                }
                            }
                            else if (boundValType == "ScalarIncrement")
                            {
                                const int64_t* currBoundVal = std::get_if<
                                    int64_t>(
                                    &std::get<
                                        BaseBiosBoundIndex::baseBiosBoundValue>(
                                        boundValueIt));
                                if (currBoundVal != nullptr)
                                {
                                    attributeIt["ScalarIncrement"] =
                                        *currBoundVal;
                                }
                                else
                                {
                                    attributeIt["ScalarIncrement"] = 0;
                                }
                            }
                            else if (boundValType == "MinStringLength")
                            {
                                const int64_t* currBoundVal = std::get_if<
                                    int64_t>(
                                    &std::get<
                                        BaseBiosBoundIndex::baseBiosBoundValue>(
                                        boundValueIt));
                                if (currBoundVal != nullptr)
                                {
                                    attributeIt["MinLength"] = *currBoundVal;
                                }
                                else
                                {
                                    attributeIt["MinLength"] = 0;
                                }
                            }
                            else if (boundValType == "MaxStringLength")
                            {
                                const int64_t* currBoundVal = std::get_if<
                                    int64_t>(
                                    &std::get<
                                        BaseBiosBoundIndex::baseBiosBoundValue>(
                                        boundValueIt));
                                if (currBoundVal != nullptr)
                                {
                                    attributeIt["MaxLength"] = *currBoundVal;
                                }
                                else
                                {
                                    attributeIt["MaxLength"] = 0;
                                }
                            }
                            else
                            {
                                // read the bound value  at 1st field
                                // for each entry
                                const int64_t* currBoundVal = std::get_if<
                                    int64_t>(
                                    &std::get<
                                        BaseBiosBoundIndex::baseBiosBoundValue>(
                                        boundValueIt));
                                if (currBoundVal != nullptr)
                                {
                                    boundValJson["ValueName"] = *currBoundVal;
                                }
                                else
                                {
                                    boundValJson["ValueName"] = 0;
                                }
                            }
                            boundValArray.push_back(boundValJson);
                        }

                        if (attrType == "Enumeration" && boundValArray.empty())
                        {
                            BMCWEB_LOG_ERROR << "Bound Values Array is empty";
                            continue;
                        }
                        if (attrType == "Enumeration")
                        {
                            attributeIt["Value"] = boundValArray;
                        }
                        attributeArray.push_back(attributeIt);
                    }
                },
                biosService, biosConfigObj, "org.freedesktop.DBus.Properties",
                "Get", biosConfigIface, "BaseBIOSTable");
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetObject", biosConfigObj,
        std::array<const char*, 1>{biosConfigIface});
}
} // namespace bios

/**
 * BiosService class supports handle put method for bios.
 */
inline void
    handleBiosServicePut(crow::App& app, const crow::Request& req,
                         const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    crow::connections::systemBus->async_method_call(
        [req,
         asyncResp](const boost::system::error_code ec,
                    const std::map<std::string, dbus::utility::DbusVariantType>&
                        userInfo) {
            if (ec)
            {
                BMCWEB_LOG_ERROR << "GetUserInfo failed";
                messages::internalError(asyncResp->res);
                return;
            }

            const std::vector<std::string>* userGroupPtr = nullptr;
            auto userInfoIter = userInfo.find("UserGroups");
            if (userInfoIter != userInfo.end())
            {
                userGroupPtr = std::get_if<std::vector<std::string>>(

                    &userInfoIter->second);
            }

            if (userGroupPtr == nullptr)
            {
                BMCWEB_LOG_ERROR << "User Group not found";
                messages::internalError(asyncResp->res);
                return;
            }

            auto found = std::find_if(
                userGroupPtr->begin(), userGroupPtr->end(),
                [](const auto& group) {
                    return (group == "redfish-hostiface") ? true : false;
                });

            // Only Host Iface (redfish-hostiface) group user should
            // perform PUT operations
            if (found == userGroupPtr->end())
            {
                BMCWEB_LOG_ERROR << "Not Sufficient Privilage";
                messages::insufficientPrivilege(asyncResp->res);
                return;
            }
            std::vector<nlohmann::json> baseBiosTableJson;
            if (!redfish::json_util::readJsonAction(
                    req, asyncResp->res, "Attributes", baseBiosTableJson))
            {
                BMCWEB_LOG_ERROR << "No 'Attributes' found";
                messages::unrecognizedRequestBody(asyncResp->res);
                return;
            }

            // Set the BaseBIOSTable
            bios::fillBiosTable(asyncResp, baseBiosTableJson);
        },
        "xyz.openbmc_project.User.Manager", "/xyz/openbmc_project/user",
        "xyz.openbmc_project.User.Manager", "GetUserInfo",
        req.session->username);
}

/**
 * BiosService class supports handle get method for bios.
 */
inline void
    handleBiosServiceGet(crow::App& app, const crow::Request& req,
                         const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    asyncResp->res.jsonValue["@odata.id"] =
        "/redfish/v1/Systems/" PLATFORMSYSTEMID "/Bios";
    asyncResp->res.jsonValue["@odata.type"] = "#Bios.v1_2_0.Bios";
    asyncResp->res.jsonValue["Name"] = "BIOS Configuration";
    asyncResp->res.jsonValue["Description"] = "BIOS Configuration Service";
    asyncResp->res.jsonValue["Id"] = "BIOS";
    asyncResp->res.jsonValue["Actions"]["#Bios.ResetBios"] = {
        {"target", "/redfish/v1/Systems/" PLATFORMSYSTEMID
                   "/Bios/Actions/Bios.ResetBios"}};
    nlohmann::json biosSettings;
    biosSettings["@odata.type"] = "#Settings.v1_3_5.Settings";
    biosSettings["SettingsObject"] = {{"@odata.id",
                                       "/redfish/v1/Systems/" PLATFORMSYSTEMID
                                       "/Bios/Settings"}};
    asyncResp->res.jsonValue["@Redfish.Settings"] = biosSettings;

    // Get the ActiveSoftwareImage and SoftwareImages
    sw_util::populateSoftwareInformation(asyncResp, sw_util::biosPurpose, "",
                                         true);

    asyncResp->res.jsonValue["Attributes"] = {};
    // Get the BIOS Attributes
    bios::getBiosAttributes(asyncResp);

    // Get the ResetBiosToDefaultsPending
    bios::getResetBiosSettings(asyncResp);
}

inline void requestRoutesBiosService(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/" PLATFORMSYSTEMID "/Bios/")
        .privileges(redfish::privileges::getBios)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleBiosServiceGet, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/Systems/" PLATFORMSYSTEMID "/Bios/")
        .privileges(redfish::privileges::putBios)
        .methods(boost::beast::http::verb::put)(
            std::bind_front(handleBiosServicePut, std::ref(app)));
}

/**
 * BiosSetting class supports handle patch method for Bios Settings.
 */
inline void
    handleBiosSettingsPatch(crow::App& app, const crow::Request& req,
                            const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    nlohmann::json pendingAttrJson;
    if (!redfish::json_util::readJsonAction(req, asyncResp->res, "Attributes",
                                            pendingAttrJson))
    {
        BMCWEB_LOG_ERROR << "No 'Attributes' found";
        messages::unrecognizedRequestBody(asyncResp->res);
        return;
    }

    // Update the Pending Atttributes
    bios::setBiosPendingAttr(asyncResp, pendingAttrJson);
}

/**
 * BiosSetting class supports handle get method for Bios Settings.
 */
inline void
    handleBiosSettingsGet(crow::App& app, const crow::Request& req,
                          const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    asyncResp->res.jsonValue["@odata.id"] =
        "/redfish/v1/Systems/" PLATFORMSYSTEMID "/Bios/Settings";
    asyncResp->res.jsonValue["@odata.type"] = "#Bios.v1_1_0.Bios";
    asyncResp->res.jsonValue["Name"] = "BIOS Configuration";
    asyncResp->res.jsonValue["Description"] = "BIOS Settings";
    asyncResp->res.jsonValue["Id"] = "BIOS_Settings";

    asyncResp->res.jsonValue["Attributes"] = {};

    // get the BIOS Attributes
    bios::getBiosSettingsAttr(asyncResp);
}

inline void requestRoutesBiosSettings(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/" PLATFORMSYSTEMID "/Bios/Settings")
        .privileges(redfish::privileges::getBios)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleBiosSettingsGet, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/Systems/" PLATFORMSYSTEMID "/Bios/Settings")
        .privileges(redfish::privileges::patchBios)
        .methods(boost::beast::http::verb::patch)(
            std::bind_front(handleBiosSettingsPatch, std::ref(app)));
}

/**
 * BiosReset class supports handle POST method for Reset bios.
 * The class retrieves and sends data directly to D-Bus.
 *
 * Function handles POST method request.
 * Analyzes POST body message before sends Reset request data to D-Bus.
 */
inline void
    handleBiosResetPost(crow::App& app, const crow::Request& req,
                        const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    crow::connections::systemBus->async_method_call(
        [asyncResp](const boost::system::error_code ec) {
            if (ec)
            {
                BMCWEB_LOG_ERROR << "Failed to reset bios: " << ec;
                messages::internalError(asyncResp->res);
                return;
            }
        },
        "org.open_power.Software.Host.Updater", "/xyz/openbmc_project/software",
        "xyz.openbmc_project.Common.FactoryReset", "Reset");
}

#ifdef BMCWEB_RESET_BIOS_BY_CLEAR_NONVOLATILE
/**
 * Set ClearNonVolatileVariables.Clear to requested value
 */
inline void setClearVariables(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                              const std::string service, const std::string path,
                              const bool requestToClear)
{
    crow::connections::systemBus->async_method_call(
        [aResp, path, service,
         requestToClear](const boost::system::error_code ec,
                         sdbusplus::message::message& msg) {
            if (!ec)
            {
                BMCWEB_LOG_DEBUG << "Set ClearUefiVariable successed";
                return;
            }

            BMCWEB_LOG_DEBUG << "Set ClearUefiVariable failed: " << ec.what();

            // Read and convert dbus error message to redfish error
            const sd_bus_error* dbusError = msg.get_error();
            if (dbusError == nullptr)
            {
                messages::internalError(aResp->res);
                return;
            }

            if (strcmp(dbusError->name, "xyz.openbmc_project.Common."
                                        "Device.Error.WriteFailure") == 0)
            {
                // Service failed to change the config
                messages::operationFailed(aResp->res);
            }
            else
            {
                messages::internalError(aResp->res);
            }
        },
        service, path, "org.freedesktop.DBus.Properties", "Set",
        "xyz.openbmc_project.Control.Boot.ClearNonVolatileVariables", "Clear",
        std::variant<bool>(requestToClear));
}

/**
 * Nvidia BiosReset class supports handle POST method for Reset bios.
 */
inline void
    handleNvidiaBiosResetPost(crow::App& app, const crow::Request& req,
                              const std::shared_ptr<bmcweb::AsyncResp>& aResp)
{
    if (!redfish::setUpRedfishRoute(app, req, aResp))
    {
        return;
    }

    // set the ResetBiosToDefaultsPending
    bios::setResetBiosSettings(aResp, true);

    crow::connections::systemBus->async_method_call(
        [aResp](boost::system::error_code ec,
                const dbus::utility::MapperGetSubTreeResponse& subtree) {
            if (ec)
            {
                // No state sensors attached.
                messages::internalError(aResp->res);
                return;
            }

            // find objects which has ClearNonVolatileVariables and also
            // belongs objPath
            for (const auto& [path, mapperService] : subtree)
            {
                if (mapperService.size() != 1)
                {
                    BMCWEB_LOG_ERROR
                        << "Number of ClearNonVolatileVariables provider is not 1. size="
                        << mapperService.size();
                    messages::internalError(aResp->res);
                    return;
                }
                const auto& service = mapperService[0].first;
                setClearVariables(aResp, service, path, true);
            }
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/xyz/openbmc_project/control", 0,
        std::array<const char*, 1>{
            "xyz.openbmc_project.Control.Boot.ClearNonVolatileVariables"});
}
#endif

inline void requestRoutesBiosReset(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/" PLATFORMSYSTEMID
                      "/Bios/Actions/Bios.ResetBios/")
        .privileges(redfish::privileges::postBios)
        .methods(boost::beast::http::verb::post)(
#if BMCWEB_RESET_BIOS_BY_CLEAR_NONVOLATILE
            std::bind_front(handleNvidiaBiosResetPost, std::ref(app))
#else
            std::bind_front(handleBiosResetPost, std::ref(app))
#endif
        );
}

/**
 * BiosAttributeRegistry class supports handle get method for Bios Attribute
 * Registry.
 */
inline void handleBiosAttrRegistryGet(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    asyncResp->res.jsonValue["@odata.id"] =
        "/redfish/v1/Registries/BiosAttributeRegistry/"
        "BiosAttributeRegistry";
    asyncResp->res.jsonValue["@odata.type"] =
        "#AttributeRegistry.v1_3_2.AttributeRegistry";
    asyncResp->res.jsonValue["Name"] = "Bios Attribute Registry";
    asyncResp->res.jsonValue["Id"] = "BiosAttributeRegistry";
    asyncResp->res.jsonValue["RegistryVersion"] = "1.0.0";
    asyncResp->res.jsonValue["Language"] = "en";
    asyncResp->res.jsonValue["OwningEntity"] = "OpenBMC";
    asyncResp->res.jsonValue["RegistryEntries"]["Attributes"] =
        nlohmann::json::array();

    // Get the BIOS Attributes Registry
    bios::getBiosAttributeRegistry(asyncResp);
}

inline void requestRoutesBiosAttrRegistryService(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Registries/"
                      "BiosAttributeRegistry/BiosAttributeRegistry/")
        .privileges(redfish::privileges::getBios)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleBiosAttrRegistryGet, std::ref(app)));
}
} // namespace redfish
