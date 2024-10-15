/*
// Copyright (c) 2019 Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
*/
#pragma once

#include "app.hpp"
#include "async_resp.hpp"
#include "dbus_singleton.hpp"
#include "dbus_utility.hpp"

#include <app.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/container/flat_set.hpp>
#include <dbus_singleton.hpp>
#include <dbus_utility.hpp>
#include <nlohmann/json.hpp>
#include <sdbusplus/asio/property.hpp>
#include <sdbusplus/bus.hpp>
#include <sdbusplus/message.hpp>

#include <array>
#include <ranges>
#include <string_view>
#include <variant>

namespace redfish
{
struct HealthPopulate : std::enable_shared_from_this<HealthPopulate>
{
    // By default populate status to "/Status" of |asyncResp->res.jsonValue|.
    explicit HealthPopulate(
        const std::shared_ptr<bmcweb::AsyncResp>& asyncRespIn) :
        asyncResp(asyncRespIn),
        statusPtr("/Status")
    {}

    // Takes a JSON pointer rather than a reference. This is pretty useful when
    // the address of the status JSON might change, for example, elements in an
    // array.
    HealthPopulate(const std::shared_ptr<bmcweb::AsyncResp>& asyncRespIn,
                   const nlohmann::json::json_pointer& ptr) :
        asyncResp(asyncRespIn),
        statusPtr(ptr)
    {}

    HealthPopulate(const HealthPopulate&) = delete;
    HealthPopulate(HealthPopulate&&) = delete;
    HealthPopulate& operator=(const HealthPopulate&) = delete;
    HealthPopulate& operator=(const HealthPopulate&&) = delete;

    ~HealthPopulate()
    {
        nlohmann::json& jsonStatus = asyncResp->res.jsonValue[statusPtr];
        nlohmann::json& health = jsonStatus["Health"];
        nlohmann::json& rollup = jsonStatus["HealthRollup"];

        health = "OK";
        rollup = "OK";

        for (const std::shared_ptr<HealthPopulate>& healthChild : children)
        {
            healthChild->globalInventoryPath = globalInventoryPath;
            healthChild->statuses = statuses;
        }

        for (const auto& [path, interfaces] : statuses)
        {
            bool isSelf = false;
            if (selfPath)
            {
                if ((path.str == *selfPath) ||
                    path.str.starts_with(*selfPath + "/"))
                {
                    isSelf = true;
                }
            }

            // managers inventory is all the inventory, don't skip any
            if (!isManagersHealth && !isSelf)
            {
                // We only want to look at this association if either the path
                // of this association is an inventory item, or one of the
                // endpoints in this association is a child

                bool isChild = false;
                for (const std::string& child : inventory)
                {
                    if (path.str.starts_with(child))
                    {
                        isChild = true;
                        break;
                    }
                }
                if (!isChild)
                {
                    for (const auto& [interface, association] : interfaces)
                    {
                        if (interface != "xyz.openbmc_project.Association")
                        {
                            continue;
                        }
                        for (const auto& [name, value] : association)
                        {
                            if (name != "endpoints")
                            {
                                continue;
                            }

                            const std::vector<std::string>* endpoints =
                                std::get_if<std::vector<std::string>>(&value);
                            if (endpoints == nullptr)
                            {
                                BMCWEB_LOG_ERROR("Illegal association at {}",
                                                 path.str);
                                continue;
                            }
                            bool containsChild = false;
                            for (const std::string& endpoint : *endpoints)
                            {
                                if (std::ranges::find(inventory, endpoint) !=
                                    inventory.end())
                                {
                                    containsChild = true;
                                    break;
                                }
                            }
                            if (!containsChild)
                            {
                                continue;
                            }
                        }
                    }
                }
            }

            if (path.str.starts_with(globalInventoryPath) &&
                path.str.ends_with("critical"))
            {
                health = "Critical";
                rollup = "Critical";
                return;
            }
            if (path.str.starts_with(globalInventoryPath) &&
                path.str.ends_with("warning"))
            {
                health = "Warning";
                if (rollup != "Critical")
                {
                    rollup = "Warning";
                }
            }
            else if (path.str.ends_with("critical"))
            {
                rollup = "Critical";
                if (isSelf)
                {
                    health = "Critical";
                    return;
                }
            }
            else if (path.str.ends_with("warning"))
            {
                if (rollup != "Critical")
                {
                    rollup = "Warning";
                }

                if (isSelf)
                {
                    health = "Warning";
                }
            }
        }
    }

    // this should only be called once per url, others should get updated by
    // being added as children to the 'main' health object for the page
    void populate()
    {
        if (populated)
        {
            return;
        }
        populated = true;
        getAllStatusAssociations();
        getGlobalPath();
    }

    void getGlobalPath()
    {
        constexpr std::array<std::string_view, 1> interfaces = {
            "xyz.openbmc_project.Inventory.Item.Global"};
        std::shared_ptr<HealthPopulate> self = shared_from_this();
        dbus::utility::getSubTreePaths(
            "/", 0, interfaces,
            [self](const boost::system::error_code& ec,
                   const dbus::utility::MapperGetSubTreePathsResponse& resp) {
            if (ec || resp.size() != 1)
            {
                // no global item, or too many
                return;
            }
            self->globalInventoryPath = resp[0];
        });
    }

    void getAllStatusAssociations()
    {
        std::shared_ptr<HealthPopulate> self = shared_from_this();
        sdbusplus::message::object_path path("/");
        dbus::utility::getManagedObjects(
            "xyz.openbmc_project.ObjectMapper", path,
            [self](const boost::system::error_code& ec,
                   const dbus::utility::ManagedObjectType& resp) {
            if (ec)
            {
                return;
            }
            self->statuses = resp;
            for (auto it = self->statuses.begin(); it != self->statuses.end();)
            {
                if (it->first.str.ends_with("critical") ||
                    it->first.str.ends_with("warning"))
                {
                    it++;
                    continue;
                }
                it = self->statuses.erase(it);
            }
        });
    }

    std::shared_ptr<bmcweb::AsyncResp> asyncResp;

    // Will populate the health status into |asyncResp_json[statusPtr]|
    nlohmann::json::json_pointer statusPtr;

    // we store pointers to other HealthPopulate items so we can update their
    // members and reduce dbus calls. As we hold a shared_ptr to them, they get
    // destroyed last, and they need not call populate()
    std::vector<std::shared_ptr<HealthPopulate>> children;

    // self is used if health is for an individual items status, as this is the
    // 'lowest most' item, the rollup will equal the health
    std::optional<std::string> selfPath;

    std::vector<std::string> inventory;
    bool isManagersHealth = false;
    dbus::utility::ManagedObjectType statuses;
    std::string globalInventoryPath = "-"; // default to illegal dbus path
    bool populated = false;
};

namespace health_state
{

/**
 * @brief Represents the same health across different domains of dbus property
 * (@dbusHealthName), bmcweb json response (@jsonHealthName), and associated
 * devices traversal logic (@severityLevel).
 */
struct Type
{
    /** @brief Eg "Critical" **/
    const std::string jsonHealthName;
    /** @brief Eg
     * "xyz.openbmc_project.State.Decorator.Health.HealthType.Critical" **/
    const std::string dbusHealthName;
    /** @brief The lower the worse. **/
    const unsigned severityLevel;
};

static const Type ok{
    "OK", "xyz.openbmc_project.State.Decorator.Health.HealthType.OK", 2u};
static const Type warning{
    "Warning", "xyz.openbmc_project.State.Decorator.Health.HealthType.Warning",
    1u};
static const Type critical{
    "Critical",
    "xyz.openbmc_project.State.Decorator.Health.HealthType.Critical", 0u};

static const std::map<std::string, const Type*>
    dbusNameMapHealthState({{ok.dbusHealthName, &ok},
                            {warning.dbusHealthName, &warning},
                            {critical.dbusHealthName, &critical}});

} // namespace health_state

const std::string dbusIntfHealth{"xyz.openbmc_project.State.Decorator.Health"};
const std::string dbusPropHealth{"Health"};
const std::string dbusIntfProperties{"org.freedesktop.DBus.Properties"};
const std::string dbusIntfDefinitions{
    "xyz.openbmc_project.Association.Definitions"};
const std::string dbusPropAssociations{"Associations"};
const std::string dbusAssocHealthCategory{"health_rollup"};

/**
 * @brief Represent the state of the HealthRollup module
 */
enum HealthRollupState
{

    INITIALIZED,

    ROOT_Q_HEALTH_SERVICE,
    ROOT_Q_HEALTH,

    ROOT_Q_ASSOCS_SERVICE,
    ROOT_Q_ASSOCS,

    ASSOC_Q_HEALTH_SERVICE,
    ASSOC_Q_HEALTH,

    STOP_ERROR,
    STOP_OK

};

using Association =
    std::vector<std::tuple<std::string, std::string, std::string>>;

/**
 * @brief Class representing the asynchronous Health Rollup functionality
 *
 * 1 Sequential code
 * =================
 *
 *   The algorithm expressed in pseudocode is as follows:
 *
 *   ,----
 *   | healthService <- getService(rootObject, Health interface)
 *   | @rootHealth   <- getHealth(healthService, rootObject)
 *   | @globalHealth <- @rootHealth
 *   | if @globalHealth != critical:
 *   |     assocsServ      <- getService(rootObject, Associations interface)
 *   |     @devicesToVisit <- getAssociations(assocsServ, rootObject)
 *   |     while @devicesToVisit is not empty and @globalHealth != critical:
 *   |         assocDev           <- pop first element of @devicesToVisit
 *   |         assocDevHealthServ <- getService(assocDev, Health interface)
 *   |         assocDevHealth     <- getHealth(assocDevHealthServ, assocDev)
 *   |         @globalHealth      <- min(@globalHealth, assocDevHealth)
 *   `----
 *
 *
 *   where variables starting with `@' correspond directly to the actual
 *   fields of the class, and function correspondance is as follows:
 *
 *    Pseudocode       Real code
 *   -------------------------------------
 *    getAssociations  rootQueryForAssocs
 *    getService       queryForService
 *    getHealth        queryForHealth
 *
 *
 * 2 State machine
 * ===============
 *
 *   The class is implemented using a finite state machine with a stack of
 *   @devicesToVisit.
 *
 *   ,----
 *   | .
 *   |
 *   |                        |
 *   |                        | a
 *   |                        v
 *   |                      +-------------------+
 *   |                      |    INITIALIZED    |
 *   |                      +-------------------+
 *   |                        |
 *   |                        | b
 *   |                        v
 *   |                      +-------------------+
 *   |                  +-- | ROOT_Q_HEALTH_S.. | -+
 *   |                  |   +-------------------+  |
 *   |                  |     |                    |
 *   |                  |     | c                  |
 *   |                  |     v                    |
 *   |                  |   +-------------------+  |
 *   |             +----+-- |   ROOT_Q_HEALTH   | -+-----------+
 *   |             |    |   +-------------------+  |           |
 *   |             |    |     |                    |           |
 *   |             |    |     | h                  | d         |
 *   |             |    |     v                    v           |
 *   |             |    |   +-------------------------------+  |
 *   |             |    |   |     ROOT_Q_ASSOCS_SERVICE     |  |
 *   |             |    |   +-------------------------------+  |
 *   |             |    |     |                    |     |     |
 *   |        +----+    |     | k                  |     |     |
 *   |        |         |     v                    |     |     |
 *   |        |         |   +-------------------+  |     |     |
 *   |        |    +----+-- |   ROOT_Q_ASSOCS   |  |     +-----+---------------+
 *   |        |    |    |   +-------------------+  |           |               |
 *   |        |    |    |     |                    |           |               |
 *   |        |    |    |     | l                  +-----------+-----+         |
 *   |        |    |    |     v                                |     |         |
 *   |        |    |    |   +-------------------------------+  |     |         |
 *   |        |    |    |   |    ASSOC_Q_HEALTH_SERVICE     | -+-----+----+    |
 *   |        |    |    |   +-------------------------------+  |     |    |    |
 *   |        |    |    |     |      |      ^      |     ^     |     |    |    |
 *   |        |    |    |     | n    +------+      |     | t   |     |    |    |
 *   |        |    |    |     v         u          |     |     |     |    |    |
 *   |        |    |    |   +-------------------+  |     |     |     |    |    |
 *   |   +----+----+----+-- |  ASSOC_Q_HEALTH   | -+-----+     | g   |    |    |
 *   |   |    |    |    |   +-------------------+  |           |     |    |    |
 *   |   |    |    |    |     |                    |           |     |    |    |
 *   |   |    |    |    | e   | s                  | p         |     |    |    |
 *   |   |    |    |    |     v                    v           |     |    |    |
 *   |   |    |    |    |   +-------------------------------+  |     |    |    |
 *   |   |    |    |    +-> |                               | <+     |    |    |
 *   |   |    |    |        |          STOP_ERROR           |        |    |    |
 *   |   |    |    |   m    |                               |  i     |    |    |
 *   |   |    |    +------> |                               | <------+    |    |
 *   |   |    |             +-------------------------------+             |    |
 *   |   |    |        f    +-------------------------------+  q          |    |
 *   |   |    +-----------> |            STOP_OK            | <-----------+    |
 *   |   |                  +-------------------------------+                  |
 *   |   |   r                ^                          ^    j                |
 *   |   +--------------------+                          +---------------------+
 *   `----
 *
 *
 *   where `ROOT_Q_HEALTH_S..' = `ROOT_Q_HEALTH_SERVICE'.
 *
 *
 * 2.1 State naming
 * ~~~~~~~~~~~~~~~~
 *
 *   Every state with a name matching `<subject>_Q_<object>' corresponds to
 *   a dbus query call, as in "querying for <object> of <subject>", where
 *   `<subject>' is
 *
 *    <subject>
 *   ------------------------------------------------------
 *    ROOT       Device on which health rollup was invoked
 *    ASSOC      One of the devices associated with ROOT
 *
 *   Further, the `<object>' part follows the pattern of either
 *   1. `<property>', or
 *   2. `<property>_SERVICE',
 *
 *   where `<property>' corresponds to the DBus property
 *
 *    <property>
 *   --------------------------
 *    HEALTH      Health
 *    ASSOCS      Associations
 *
 *   provided by the interface
 *
 *    <property>
 *   ---------------------------------------------------------
 *    HEALTH      xyz.openbmc_project.State.Decorator.Health
 *    ASSOCS      xyz.openbmc_project.Association.Definitions
 *
 *   In case of `<subject>_Q_<property>_SERVICE' state the
 *   `xyz.openbmc_project.ObjectMapper' service is being queried for the
 *   manager service providing for `<subject>' the interface associated
 *   with `<property>'.
 *
 *
 * 2.2 Transitions
 * ~~~~~~~~~~~~~~~
 *
 *    Edge  Function call sequence        Possible scenario
 *          realizing transition
 *   -----------------------------------------------------------------
 *    (a)   (constructor)
 *   -----------------------------------------------------------------
 *    (b)   start
 *          queryForService
 *   -----------------------------------------------------------------
 *    (c)   queryForService: callback
 *          queryForHealth
 *   -----------------------------------------------------------------
 *    (d)   queryForService: callback
 *          proceedWithCurrentNodeHealth
 *          queryForService
 *   -----------------------------------------------------------------
 *    (e)   queryForService: callback
 *          stopRollup
 *   -----------------------------------------------------------------
 *    (f)   queryForHealth: callback
 *          proceedWithCurrentNodeHealth
 *          stopRollup
 *   -----------------------------------------------------------------
 *    (g)   queryForHealth: callback
 *          proceedWithCurrentNodeHealth
 *          stopRollup
 *   -----------------------------------------------------------------
 *    (h)   queryForHealth: callback
 *          proceedWithCurrentNodeHealth
 *          queryForService
 *   -----------------------------------------------------------------
 *    (i)   queryForService: callback
 *          stopRollup
 *   -----------------------------------------------------------------
 *    (j)   queryForService: callback
 *          assocQueryForService
 *          && devicesToVisit == {}
 *          stopRollup
 *   -----------------------------------------------------------------
 *    (k)   queryForService: callback
 *          rootQueryForAssocs
 *   -----------------------------------------------------------------
 *    (l)   rootQueryForAssocs: callback
 *          assocQueryForService
 *   -----------------------------------------------------------------
 *    (m)   rootQueryForAssocs: callback
 *          stopRollup
 *   -----------------------------------------------------------------
 *    (n)   queryForService: callback
 *          queryForHealth
 *   -----------------------------------------------------------------
 *    (p)   queryForService: callback
 *          stopRollup
 *   -----------------------------------------------------------------
 *    (q)   queryForService: callback     No health service found for
 *          proceedWithCurrentNodeHealth  last device, assume
 *          assocQueryForService          @assumedHealthWhenMissing
 *          && devicesToVisit == {}       and finish
 *          stopRollup
 *   -----------------------------------------------------------------
 *    (r)   queryForHealth: callback      Critical health found,
 *          proceedWithCurrentNodeHealth  stopping iteration.
 *          stopRollup
 *   -----------------------------------------------------------------
 *    (r)   queryForHealth: callback      No more devices to
 *          proceedWithCurrentNodeHealth  visit and check health of
 *          assocQueryForService
 *          && devicesToVisit == {}
 *          stopRollup
 *   -----------------------------------------------------------------
 *    (s)   queryForHealth: callback
 *          proceedWithCurrentNodeHealth
 *          stopRollup
 *   -----------------------------------------------------------------
 *    (t)   queryForHealth: callback
 *          proceedWithCurrentNodeHealth
 *          assocQueryForService
 *          && devicesToVisit != {}
 *          queryForService
 *   -----------------------------------------------------------------
 *    (u)   queryForService: callback     No service prviding
 *          proceedWithCurrentNodeHealth  health interface found for
 *          assocQueryForService          this device. Moving to next
 *          && devicesToVisit != {}
 *          queryForService
 *   -----------------------------------------------------------------
 */

class HealthRollup : public std::enable_shared_from_this<HealthRollup>
{
  public:
    /**
     * @brief Create the `HealthRollup' object leaving it in the INITIALIZED
     * state (no operation performed until 'start()' is called)
     *
     * Use it only via `make_shared' function, as in
     * `make_shared<HealthRollup>(conn, rootObj, callback)'. Otherwise segfaults
     * or `bad_weak_ptr' exceptions will occur after calling `start()'.
     *
     * @param[in,out] conn DBus connection object on which `async_method_call'
     * method calls should be performed.
     *
     * @param[in] rootObject An object path in the object tree of the
     * `xyz.openbmc_project.ObjectMapper' service, eg
     * "/xyz/openbmc_project/inventory/system/chassis/Baseboard", the check of
     * health of which and all its associated devices is to be performed.
     *
     * @param[in] finishCallback A callback function to be called when, and only
     * when, this object moves to the `STOP_OK' state. The values of
     * `rootHealth' and `healthRollup' parameters are the `jsonHealthName'
     * fields of the corresponding `health_state::Type' objects. The
     * `healthRollup' value is defined as the one whose `health_state::Type'
     * object has the maximal severity level, with the ordering being ok <
     * warning < critical, aggregated across all health statuses of the
     * `rootObject' and its associated devices.
     *
     * @param[in] assumedHealthWhenMissing Address of one of the
     * `health_state::Type' constants defined in `health_state'. This is the
     * assumed health of the device for which the actual health status could not
     * be obtain (either the health property is missing, or the whole interface
     * providing it). In addition to `&&health_state::ok',
     * `&health_state::warning' and `&health_state::critical' the `nullptr' can
     * be passed, in case of which every such situation will result in this
     * object stopping the crawling and moving to STOP_ERROR state.
     */
    HealthRollup(const std::string& rootObject,
                 std::function<void(const std::string& rootHealth,
                                    const std::string& healthRollup)>
                     finishCallback,
                 // 'critical' value used as an issue exposure
                 const health_state::Type* assumedHealthWhenMissing =
                     &health_state::ok) :
        rootHealth(&health_state::ok),
        globalHealth(&health_state::ok), state(INITIALIZED), devicesToVisit(),
        assumedHealthWhenMissing(assumedHealthWhenMissing),
        rootObject(rootObject), finishCallback(std::move(finishCallback))
    {}

    /**
     * @brief Move the object to ROOT_Q_HEALTH_SERVICE state triggering the
     * health rollup for the parameters given in the constructor.
     *
     * It can be called only once. After the call object becomes useless, except
     * for the `finishCallback' call through which the results are communicated.
     *
     */
    void start()
    {
        // assert(state == INITIALIZED);
        state = ROOT_Q_HEALTH_SERVICE;
        queryForService(rootObject,
                        "xyz.openbmc_project.State.Decorator.Health");
    }

  private:
    // Dynamic fields (state-defining)
    const health_state::Type* rootHealth;
    /** @brief A value such that its @severityLevel is lowest among all the node
     * healths checked. (3) **/
    const health_state::Type* globalHealth;
    HealthRollupState state;
    std::deque<std::string> devicesToVisit;

    // Static fields (constant throughout the whole life of the object)
    const health_state::Type* assumedHealthWhenMissing;
    std::string rootObject;
    std::function<void(const std::string& rootHealth,
                       const std::string& healthRollup)>
        finishCallback;

    /**
     * 1 `busctl' equivalent
     * =====================
     *
     *   ,----
     *   | busctl get-property                                \
     *   |        @serviceManager                             \
     *   |        @objPath                                    \
     *   |        xyz.openbmc_project.Association.Definitions \
     *   |        Associations
     *   `----
     *
     *
     * 1.1 Exemplary command
     * ~~~~~~~~~~~~~~~~~~~~~
     *
     *   ,----
     *   | busctl get-property                                            \
     *   |        xyz.openbmc_project.GpuMgr                              \
     *   |        /xyz/openbmc_project/inventory/system/chassis/Baseboard \
     *   |        xyz.openbmc_project.Association.Definitions             \
     *   |        Associations
     *   `----
     *
     *
     * 1.2 Exemplary output
     * ~~~~~~~~~~~~~~~~~~~~
     *
     *   ,----
     *   | a(sss)
     *   | 78
     *   | "all_chassis" "parent_chassis"
     *   |     "/xyz/openbmc_project/inventory/system/chassis/GPU7"
     *   | "all_processors" ""
     *   |     "/xyz/openbmc_project/inventory/system/processors/GPU7"
     *   | "all_chassis" "parent_chassis"
     *   |     "/xyz/openbmc_project/inventory/system/chassis/GPU6"
     *   |     ...
     *   | "fabrics" ""
     *   |     "/xyz/openbmc_project/inventory/system/fabrics/NVLinkFabric"
     *   | "health_rollup" ""
     *   |     "/xyz/openbmc_project/inventory/system/chassis/NVSwitch3"
     *   | "health_rollup" ""
     *   |     "/xyz/openbmc_project/inventory/system/chassis/NVSwitch2"
     *   | "health_rollup" ""
     *   |     "/xyz/openbmc_project/inventory/system/chassis/NVSwitch1"
     *   | "health_rollup" ""
     *   |     "/xyz/openbmc_project/inventory/system/chassis/NVSwitch0"
     *   | "health_rollup" ""
     *   |     "/xyz/openbmc_project/inventory/system/chassis/GPU7"
     *   | "health_rollup" ""
     *   |     "/xyz/openbmc_project/inventory/system/chassis/
     *   |                          Baseboard/PCIeDevices/PCIeRetimer7"
     *   |     ...
     *   | "health_rollup" ""
     *   |     "/xyz/openbmc_project/inventory/system/chassis/GPU0"
     *   | "health_rollup" ""
     *   |     "/xyz/openbmc_project/inventory/system/chassis/
     *   |                          Baseboard/PCIeDevices/PCIeRetimer0"
     *   `----
     *
     *   The list of associations is filtered for "health_rollup" entries, and
     *   the resulting device object paths, in this case
     *
     *   ,----
     *   | "/xyz/openbmc_project/inventory/system/chassis/NVSwitch3"
     *   | "/xyz/openbmc_project/inventory/system/chassis/NVSwitch2"
     *   | "/xyz/openbmc_project/inventory/system/chassis/NVSwitch1"
     *   | "/xyz/openbmc_project/inventory/system/chassis/NVSwitch0"
     *   | "/xyz/openbmc_project/inventory/system/chassis/GPU7"
     *   | "/xyz/openbmc_project/inventory/system/chassis/
     *   |                      Baseboard/PCIeDevices/PCIeRetimer7"
     *   | ...
     *   | "/xyz/openbmc_project/inventory/system/chassis/GPU0"
     *   | "/xyz/openbmc_project/inventory/system/chassis/
     *   |                      Baseboard/PCIeDevices/PCIeRetimer0"
     *   `----
     *
     *   are put in the @devicesToVisit field.
     *
     *
     * 2 State machine's subset covered
     * ================================
     *
     *   ,----
     *   | .
     *   |
     *   |
     *   |
     *   |
     *   |                      +-------------------+
     *   |                      |    INITIALIZED    |
     *   |                      +-------------------+
     *   |
     *   |
     *   |
     *   |                      +-------------------+
     *   |                      | ROOT_Q_HEALTH_S.. |
     *   |                      +-------------------+
     *   |
     *   |
     *   |
     *   |                      +-------------------+
     *   |                      |   ROOT_Q_HEALTH   |
     *   |                      +-------------------+
     *   |
     *   |
     *   |
     *   |                      +-------------------------------+
     *   |                      |     ROOT_Q_ASSOCS_SERVICE     |
     *   |                      +-------------------------------+
     *   |
     *   |
     *   |
     *   |                      #####################
     *   |             +------- #   ROOT_Q_ASSOCS   #
     *   |             |        #####################
     *   |             |          |
     *   |             |          | l
     *   |             |          v
     *   |             |        +-------------------------------+
     *   |             |        |    ASSOC_Q_HEALTH_SERVICE     |
     *   |             |        +-------------------------------+
     *   |             |
     *   |             |
     *   |             |
     *   |             |        +-------------------+
     *   |             |        |  ASSOC_Q_HEALTH   |
     *   |             |        +-------------------+
     *   |             |
     *   |             |
     *   |             |
     *   |             |        +-------------------------------+
     *   |             |        |                               |
     *   |             |        |          STOP_ERROR           |
     *   |             |   m    |                               |
     *   |             +------> |                               |
     *   |                      +-------------------------------+
     *   |                      +-------------------------------+
     *   |                      |            STOP_OK            |
     *   |                      +-------------------------------+
     *   `----
     */
    void rootQueryForAssocs(const std::string& serviceManager,
                            const std::string& objPath)
    {
        // assert(state == ROOT_Q_ASSOCS);
        std::shared_ptr<HealthRollup> self = shared_from_this();
        crow::connections::systemBus->async_method_call(
            [self, serviceManager,
             objPath](const boost::system::error_code ec,
                      const std::variant<Association>& result) mutable {
            if (ec)
            {
                self->getPropertyFailFeedback(serviceManager, objPath,
                                              dbusIntfDefinitions,
                                              dbusPropAssociations, ec);
                // TODO: move to ASSOC_Q_HEALTH_SERVICE state (assume
                // that error in getting associations is no associations)
                self->stopRollup(STOP_ERROR);
            }
            else // ! ec
            {
                const Association* associations = get_if<Association>(&result);
                if (associations == nullptr)
                {
                    self->invalidPropertyTypeFeedback(
                        serviceManager, objPath, dbusIntfDefinitions,
                        dbusPropAssociations, "a(sss)");
                    self->stopRollup(STOP_ERROR);
                }
                else // ! associations == nullptr
                {
                    for (const auto& tup : *associations)
                    {
                        if (get<0>(tup) == dbusAssocHealthCategory)
                        {
                            self->devicesToVisit.push_back(get<2>(tup));
                        }
                    }
                    self->assocQueryForService();
                }
            }
        },
            serviceManager.c_str(), objPath.c_str(), dbusIntfProperties.c_str(),
            "Get", dbusIntfDefinitions.c_str(), dbusPropAssociations.c_str());
    }

    /** @brief Pop first element from @devicesToVisit; obtain its managing
     * service. Call @queryForHealth passing the element as `objPath' and the
     * service as `serviceManager'. **/
    void assocQueryForService()
    {
        // assert(state == ROOT_Q_ASSOCS || state == ASSOC_Q_HEALTH);
        state = ASSOC_Q_HEALTH_SERVICE;
        if (!devicesToVisit.empty())
        {
            auto objPath = devicesToVisit.front();
            devicesToVisit.pop_front();
            queryForService(objPath,
                            "xyz.openbmc_project.State.Decorator.Health");
        }
        else // devicesToVisit.empty()
        {
            stopRollup(STOP_OK);
        }
    }

    /**
     * @brief
     *
     * Let's assume the current state is `ROOT_Q_ASSOCS_SERVICE'. The semantics
     * of the `ServiceQueryingResult' values is as follows:
     *
     * ,----
     * |                     ...
     * |
     * |            |
     * |            |
     * |            v
     * |          ##########################
     * |   +----- # ROOT_Q_ASSOCS_SERVICE  # -+
     * |   |      ##########################  |
     * |   |        |                         |
     * |   |        | CONTINUE                |
     * |   |        v                         |
     * |   |      +------------------------+  |
     * |   |      |     ROOT_Q_ASSOCS      |  | SKIP
     * |   |      +------------------------+  |
     * |   |        |                         |
     * |   |        |                         |
     * |   |        v                         |
     * |   |      +------------------------+  |
     * |   | STOP | ASSOC_Q_HEALTH_SERVICE | <+
     * |   |      +------------------------+
     * |   |        |
     * |   |        |
     * |   |        v
     * |   |
     * |   |                 ...
     * |   |
     * |   |
     * |   |
     * |   |
     * |   |      +------------------------+
     * |   +----> |       STOP_ERROR       |
     * |          +------------------------+
     * `----
     *
     *
     * for:
     *
     *  Label     ServiceQueryingResult
     * ---------------------------------
     *  CONTINUE  SERVICE_OK_CONTINUE
     *  SKIP      SERVICE_ERROR_SKIP
     *  STOP      SERVICE_ERROR_STOP
     *
     * Analogously for the `ROOT_Q_HEALTH_SERVICE' and `ASSOC_Q_HEALTH_SERVICE'
     * states to which this logic also applies.
     */
    enum ServiceQueryingResult
    {
        SERVICE_OK_CONTINUE,
        SERVICE_ERROR_SKIP,
        SERVICE_ERROR_STOP
    };

    ServiceQueryingResult determineQueryingServiceNextMove(
        const boost::system::error_code ec,
        const std::map<std::string, std::vector<std::string>>& result,
        const std::string& objPath, const char* interface)
    {
        ServiceQueryingResult nextMove = SERVICE_ERROR_STOP;
        if (ec)
        {
            BMCWEB_LOG_WARNING(
                "Failed to get manager service for object path '{}' implementing the interface '{}'",
                objPath, interface);
            nextMove = SERVICE_ERROR_SKIP;
        }
        else if (result.size() == 0)
        {
            BMCWEB_LOG_WARNING(
                "No managers found for object path '{}' implementing the interface '{}'",
                objPath, interface);
            nextMove = SERVICE_ERROR_SKIP;
        }
        else if (result.size() > 1)
        {
            BMCWEB_LOG_ERROR(
                "Multiple managers found ({}) for object path '{}' implementing the interface '{}'",
                result.size(), objPath, interface);
            nextMove = SERVICE_ERROR_STOP;
        }
        else
        {
            nextMove = SERVICE_OK_CONTINUE;
        }
        return nextMove;
    }

    /**
     * 1 `busctl' equivalent
     * =====================
     *
     *   ,----
     *   | busctl call                               \
     *   |        xyz.openbmc_project.ObjectMapper   \
     *   |        /xyz/openbmc_project/object_mapper \
     *   |        xyz.openbmc_project.ObjectMapper   \
     *   |        GetObject                          \
     *   |        sas                                \
     *   |        @objPath                           \
     *   |        1                                  \
     *   |        @interface
     *   `----
     *
     *
     * 1.1 Exemplary command
     * ~~~~~~~~~~~~~~~~~~~~~
     *
     *   ,----
     *   | busctl call                                                    \
     *   |        xyz.openbmc_project.ObjectMapper                        \
     *   |        /xyz/openbmc_project/object_mapper                      \
     *   |        xyz.openbmc_project.ObjectMapper                        \
     *   |        GetObject                                               \
     *   |        sas                                                     \
     *   |        /xyz/openbmc_project/inventory/system/chassis/Baseboard \
     *   |        1                                                       \
     *   |        xyz.openbmc_project.State.Decorator.Health
     *   `----
     *
     *
     * 1.2 Exemplary output
     * ~~~~~~~~~~~~~~~~~~~~
     *
     *   ,----
     *   | a{sas}
     *   | 1
     *   | "xyz.openbmc_project.GpuMgr"
     *   | 11
     *   | "org.freedesktop.DBus.Introspectable"
     *   | "org.freedesktop.DBus.Peer"
     *   | "org.freedesktop.DBus.Properties"
     *   | "xyz.openbmc_project.Association.Definitions"
     *   | "xyz.openbmc_project.Control.Mode"
     *   | "xyz.openbmc_project.Control.Power.Cap"
     *   | "xyz.openbmc_project.Inventory.Decorator.Asset"
     *   | "xyz.openbmc_project.Inventory.Decorator.PowerLimit"
     *   | "xyz.openbmc_project.Inventory.Item.Chassis"
     *   | "xyz.openbmc_project.State.Chassis"
     *   | "xyz.openbmc_project.State.Decorator.Health"
     *   `----
     *
     *
     *   Only the "xyz.openbmc_project.GpuMgr" part is being used.
     *
     *
     * 2 State machine's subset covered
     * ================================
     *
     *   ,----
     *   | .
     *   |
     *   |
     *   |
     *   |
     *   |                  +-------------------+
     *   |                  |    INITIALIZED    |
     *   |                  +-------------------+
     *   |
     *   |
     *   |
     *   |                  #####################
     *   |              +-- # ROOT_Q_HEALTH_S.. # -+
     *   |              |   #####################  |
     *   |              |     |                    |
     *   |              |     | c                  |
     *   |              |     v                    |
     *   |              |   +-------------------+  |
     *   |              |   |   ROOT_Q_HEALTH   |  |
     *   |              |   +-------------------+  |
     *   |              |                          |
     *   |              |                          | d
     *   |              |                          v
     *   |              |   #################################
     *   |              |   #     ROOT_Q_ASSOCS_SERVICE     #
     *   |              |   #################################
     *   |              |     |                    |     |
     *   |              |     | k                  |     |
     *   |              |     v                    |     |
     *   |              |   +-------------------+  |     |
     *   |              |   |   ROOT_Q_ASSOCS   |  |     +---------------------+
     *   |              |   +-------------------+  |                           |
     *   |              |                          |                           |
     *   |              |                          +-----------------+         |
     *   |              |                                            |         |
     *   |              |   #################################        |         |
     *   |              |   #    ASSOC_Q_HEALTH_SERVICE     # -------+----+    |
     *   |              |   #################################        |    |    |
     *   |              |     |      |      ^      |                 |    |    |
     *   |              |     | n    +------+      |                 |    |    |
     *   |              |     v         u          |                 |    |    |
     *   |              |   +-------------------+  |                 |    |    |
     *   |              |   |  ASSOC_Q_HEALTH   |  |                 |    |    |
     *   |              |   +-------------------+  |                 |    |    |
     *   |              |                          |                 |    |    |
     *   |              | e                        | p               |    |    |
     *   |              |                          v                 |    |    |
     *   |              |   +-------------------------------+        |    |    |
     *   |              +-> |                               |        |    |    |
     *   |                  |          STOP_ERROR           |        |    |    |
     *   |                  |                               |  i     |    |    |
     *   |                  |                               | <------+    |    |
     *   |                  +-------------------------------+             |    |
     *   |                  +-------------------------------+  q          |    |
     *   |                  |            STOP_OK            | <-----------+    |
     *   |                  +-------------------------------+                  |
     *   |                                               ^    j                |
     *   |                                               +---------------------+
     *   `----
     */
    void queryForService(const std::string& objPath, const char* interface)
    {
        // assert(state == ROOT_Q_HEALTH_SERVICE ||
        //        state == ROOT_Q_ASSOCS_SERVICE ||
        //        state == ASSOC_Q_HEALTH_SERVICE); // (2)
        std::shared_ptr<HealthRollup> self = shared_from_this();
        crow::connections::systemBus->async_method_call(
            [self, objPath, interface](
                const boost::system::error_code ec,
                const std::map<std::string, std::vector<std::string>>& result) {
            ServiceQueryingResult nextMove =
                self->determineQueryingServiceNextMove(ec, result, objPath,
                                                       interface);
            if (nextMove == SERVICE_OK_CONTINUE)
            {
                auto manager = result.cbegin()->first;
                BMCWEB_LOG_INFO(
                    "Found manager service for object path '{}' implementing the interface '{}': '{}'",
                    objPath, interface, manager);
                if (self->state == ROOT_Q_HEALTH_SERVICE)
                {
                    self->state = ROOT_Q_HEALTH;
                    self->queryForHealth(manager, objPath);
                }
                else if (self->state == ROOT_Q_ASSOCS_SERVICE)
                {
                    self->state = ROOT_Q_ASSOCS;
                    self->rootQueryForAssocs(manager, objPath);
                }
                else // self->state == ASSOC_Q_HEALTH_SERVICE
                {
                    // 'state == ASSOC_Q_HEALTH_SERVICE' by virtue of
                    // assertion (2)
                    self->state = ASSOC_Q_HEALTH;
                    self->queryForHealth(manager, objPath);
                }
            }
            else if (nextMove == SERVICE_ERROR_SKIP)
            {
                if (self->state == ROOT_Q_HEALTH_SERVICE)
                {
                    self->state = ROOT_Q_HEALTH;
                    self->proceedWithCurrentNodeHealth(
                        self->assumedHealthWhenMissing, objPath);
                }
                else if (self->state == ROOT_Q_ASSOCS_SERVICE)
                {
                    self->state = ROOT_Q_ASSOCS;
                    self->assocQueryForService();
                }
                else // self->state == ASSOC_Q_HEALTH_SERVICE
                {
                    self->state = ASSOC_Q_HEALTH;
                    self->proceedWithCurrentNodeHealth(
                        self->assumedHealthWhenMissing);
                }
            }
            else // nextMove == SERVICE_ERROR_STOP
            {
                self->stopRollup(STOP_ERROR);
            }
        },
            "xyz.openbmc_project.ObjectMapper",
            "/xyz/openbmc_project/object_mapper",
            "xyz.openbmc_project.ObjectMapper", "GetObject", objPath,
            std::vector<std::string>{interface});
    }

    const health_state::Type*
        determineNodeHealth(const boost::system::error_code ec,
                            const std::variant<std::string>& result,
                            const std::string& serviceManager,
                            const std::string& objPath)
    {
        const health_state::Type* nodeHealth = nullptr;
        if (ec)
        {
            getPropertyFailFeedback(serviceManager, objPath, dbusIntfHealth,
                                    dbusPropHealth, ec);
            nodeHealth = assumedHealthWhenMissing;
        }
        else
        {
            const std::string* dbusHealthState = get_if<std::string>(&result);
            if (dbusHealthState == nullptr)
            {
                invalidPropertyTypeFeedback(serviceManager, objPath,
                                            dbusIntfDefinitions,
                                            dbusPropAssociations, "s");
                nodeHealth = nullptr;
            }
            else // ! dbusHealthState == nullptr
            {
                if (!health_state::dbusNameMapHealthState.contains(
                        *dbusHealthState))
                {
                    BMCWEB_LOG_ERROR("Unrecognized health value: '{}'",
                                     *dbusHealthState);
                    nodeHealth = nullptr;
                }
                else
                {
                    nodeHealth = health_state::dbusNameMapHealthState.at(
                        *dbusHealthState);
                    // TODO: debug
                    BMCWEB_LOG_INFO("Health of '{}': '{}'", objPath,
                                    nodeHealth->jsonHealthName);
                }
            }
        }
        return nodeHealth;
    }

    void proceedWithCurrentNodeHealth(const health_state::Type* nodeHealth,
                                      const std::string& objPath = "")
    {
        // assert(state == ROOT_Q_HEALTH || state == ASSOC_Q_HEALTH);
        if (nodeHealth == nullptr)
        {
            stopRollup(STOP_ERROR);
        }
        else // nodeHealth != nullptr
        {
            if (state == ROOT_Q_HEALTH)
            {
                rootHealth = nodeHealth;
            }
            if (nodeHealth->severityLevel < globalHealth->severityLevel)
            {
                globalHealth = nodeHealth;
            }
            if (globalHealth->severityLevel == 0)
            {
                // The highst possible severity level reached,
                // so there is no point of further checking
                stopRollup(STOP_OK);
            }
            else
            {
                // Otherwise continue
                if (state == ROOT_Q_HEALTH)
                {
                    state = ROOT_Q_ASSOCS_SERVICE;
                    queryForService(
                        objPath, "xyz.openbmc_project.Association.Definitions");
                }
                else // state == ASSOC_Q_HEALTH
                {
                    // 'state == ASSOC_Q_HEALTH' by
                    // virtue of assertion (1)
                    assocQueryForService();
                }
            }
        }
    }

    /**
     * @brief
     *
     * 1 `busctl' equivalent
     * =====================
     *
     *   ,----
     *   | busctl get-property    \
     *   |        @serviceManager \
     *   |        @objPath        \
     *   |        xyz.openbmc_project.State.Decorator.Health Health
     *   `----
     *
     *
     * 1.1 Exemplary command
     * ~~~~~~~~~~~~~~~~~~~~~
     *
     *   ,----
     *   | busctl get-property                                            \
     *   |        xyz.openbmc_project.GpuMgr                              \
     *   |        /xyz/openbmc_project/inventory/system/chassis/Baseboard \
     *   |        xyz.openbmc_project.State.Decorator.Health Health
     *   `----
     *
     *
     * 1.2 Exemplary output
     * ~~~~~~~~~~~~~~~~~~~~
     *
     *   ,----
     *   | s "xyz.openbmc_project.State.Decorator.Health.HealthType.OK"
     *   `----
     *
     *
     *   See @health_state::Type for the mapping from this value to what would
     *   be put in the response json.
     *
     *
     * 2 State machine's subset covered
     * ================================
     *
     *   ,----
     *   | .
     *   |
     *   |
     *   |
     *   |
     *   |                      +-------------------+
     *   |                      |    INITIALIZED    |
     *   |                      +-------------------+
     *   |
     *   |
     *   |
     *   |                      +-------------------+
     *   |                      | ROOT_Q_HEALTH_S.. |
     *   |                      +-------------------+
     *   |
     *   |
     *   |
     *   |                      #####################
     *   |             +------- #   ROOT_Q_HEALTH   # -------------+
     *   |             |        #####################              |
     *   |             |          |                                |
     *   |             |          | h                              |
     *   |             |          v                                |
     *   |             |        +-------------------------------+  |
     *   |             |        |     ROOT_Q_ASSOCS_SERVICE     |  |
     *   |             |        +-------------------------------+  |
     *   |             |                                           |
     *   |        +----+                                           |
     *   |        |                                                |
     *   |        |             +-------------------+              |
     *   |        |             |   ROOT_Q_ASSOCS   |              |
     *   |        |             +-------------------+              |
     *   |        |                                                |
     *   |        |                                                |
     *   |        |                                                |
     *   |        |             +-------------------------------+  |
     *   |        |             |    ASSOC_Q_HEALTH_SERVICE     |  |
     *   |        |             +-------------------------------+  |
     *   |        |                                          ^     |
     *   |        |                                          | t   |
     *   |        |                                          |     |
     *   |        |             #####################        |     |
     *   |   +----+------------ #  ASSOC_Q_HEALTH   # -------+     | g
     *   |   |    |             #####################              |
     *   |   |    |               |                                |
     *   |   |    |               | s                              |
     *   |   |    |               v                                |
     *   |   |    |             +-------------------------------+  |
     *   |   |    |             |                               | <+
     *   |   |    |             |          STOP_ERROR           |
     *   |   |    |             |                               |
     *   |   |    |             |                               |
     *   |   |    |             +-------------------------------+
     *   |   |    |        f    +-------------------------------+
     *   |   |    +-----------> |            STOP_OK            |
     *   |   |                  +-------------------------------+
     *   |   |   r                ^
     *   |   +--------------------+
     *   `----
     */
    void queryForHealth(const std::string& serviceManager,
                        const std::string& objPath)
    {
        // assert(state == ROOT_Q_HEALTH || state == ASSOC_Q_HEALTH); // (1)
        std::shared_ptr<HealthRollup> self = shared_from_this();
        crow::connections::systemBus->async_method_call(
            [self, serviceManager,
             objPath](const boost::system::error_code ec,
                      const std::variant<std::string>& result) {
            const health_state::Type* nodeHealth =
                self->determineNodeHealth(ec, result, serviceManager, objPath);
            self->proceedWithCurrentNodeHealth(nodeHealth, objPath);
        },
            serviceManager.c_str(), objPath.c_str(), dbusIntfProperties, "Get",
            dbusIntfHealth, dbusPropHealth);
    }

    /** @brief -> STOP_ERROR | STOP_OK **/
    void stopRollup(HealthRollupState exitState)
    {
        // assert(exitState == STOP_OK || exitState == STOP_ERROR);
        if (exitState == STOP_OK)
        {
            finishCallback(rootHealth->jsonHealthName,
                           globalHealth->jsonHealthName);
        }
        state = exitState;
    }

    // Debugging tools ////////////////////////////////////////////////////////

    static void printErrno(const boost::system::error_code ec)
    {
        BMCWEB_LOG_ERROR("errno = {}, \"{}\"", ec, ec.message());
    }

    static void printProperty(const std::string& service,
                              const std::string& object,
                              const std::string& interface,
                              const std::string& property)
    {
        BMCWEB_LOG_ERROR("'{}' (service: '{}', object: '{}', interface: '{}')",
                         property, service, object, interface);
    }

    static void getPropertyFailFeedback(const std::string& service,
                                        const std::string& object,
                                        const std::string& interface,
                                        const std::string& property,
                                        const boost::system::error_code ec)

    {
        BMCWEB_LOG_ERROR("Failed to get ");
        printProperty(service, object, interface, property);
        BMCWEB_LOG_ERROR(", ");
        printErrno(ec);
    }

    static void invalidPropertyTypeFeedback(const std::string& service,
                                            const std::string& object,
                                            const std::string& interface,
                                            const std::string& property,
                                            const char* desiredType)
    {
        BMCWEB_LOG_ERROR("Invalid non-'{}' value of property ", desiredType);
        printProperty(service, object, interface, property);
    }
};

} // namespace redfish
