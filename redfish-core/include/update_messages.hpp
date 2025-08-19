<<<<<<< HEAD
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
#include "nvidia_messages.hpp"

namespace redfish
{
namespace messages
{

inline nlohmann::json targetDetermined(const std::string& arg1,
                                       const std::string& arg2)
{
    return nlohmann::json{
        {"@odata.type", "#MessageRegistry.v1_4_1.MessageRegistry"},
        {"MessageId", "Update.1.0.TargetDetermined"},
        {"Message", "The target device '" + arg1 +
                        "' will be updated with image '" + arg2 + "'."},
        {"MessageArgs", {arg1, arg2}},
        {"Severity", "OK"},
        {"Resolution", "None."}};
}
inline nlohmann::json allTargetsDetermined()
{
    return nlohmann::json{
        {"@odata.type", "#MessageRegistry.v1_4_1.MessageRegistry"},
        {"MessageId", "Update.1.0.AllTargetsDetermined"},
        {"Message",
         "All the target device to be updated have been determined."},
        {"MessageArgs", {}},
        {"Severity", "OK"},
        {"Resolution", "None."}};
}
inline nlohmann::json updateInProgress()
{
    return nlohmann::json{
        {"@odata.type", "#MessageRegistry.v1_4_1.MessageRegistry"},
        {"MessageId", "Update.1.0.UpdateInProgress"},
        {"Message", "An update is in progress."},
        {"MessageArgs", {}},
        {"Severity", "OK"},
        {"Resolution", "None."}};
}
inline nlohmann::json updateInProgress(const std::string& arg1)
{
    return nlohmann::json{
        {"@odata.type", "#MessageRegistry.v1_4_1.MessageRegistry"},
        {"MessageId", "Update.1.0.UpdateInProgress"},
        {"Message", "An update is in progress."},
        {"MessageArgs", {arg1}},
        {"Severity", "OK"},
        {"Resolution", "None."}};
}
inline nlohmann::json transferringToComponent(const std::string& arg1,
                                              const std::string& arg2)
{
    return nlohmann::json{
        {"@odata.type", "#MessageRegistry.v1_4_1.MessageRegistry"},
        {"MessageId", "Update.1.0.TransferringToComponent"},
        {"Message",
         "Image '" + arg1 + "' is being transferred to '" + arg2 + "'."},
        {"MessageArgs", {arg1, arg2}},
        {"Severity", "OK"},
        {"Resolution", "None."}};
}
inline nlohmann::json verifyingAtComponent(const std::string& arg1,
                                           const std::string& arg2)
{
    return nlohmann::json{
        {"@odata.type", "#MessageRegistry.v1_4_1.MessageRegistry"},
        {"MessageId", "Update.1.0.VerifyingAtComponent"},
        {"Message",
         "Image '" + arg1 + "' is being verified at '" + arg2 + "'."},
        {"MessageArgs", {arg1, arg2}},
        {"Severity", "OK"},
        {"Resolution", "None."}};
}
inline nlohmann::json installingOnComponent(const std::string& arg1,
                                            const std::string& arg2)
{
    return nlohmann::json{
        {"@odata.type", "#MessageRegistry.v1_4_1.MessageRegistry"},
        {"MessageId", "Update.1.0.InstallingOnComponent"},
        {"Message",
         "Image '" + arg1 + "' is being installed on '" + arg2 + "'."},
        {"MessageArgs", {arg1, arg2}},
        {"Severity", "OK"},
        {"Resolution", "None."}};
}
inline nlohmann::json applyingOnComponent(const std::string& arg1,
                                          const std::string& arg2)
{
    return nlohmann::json{
        {"@odata.type", "#MessageRegistry.v1_4_1.MessageRegistry"},
        {"MessageId", "Update.1.0.ApplyingOnComponent"},
        {"Message", "Image '" + arg1 + "' is being applied on '" + arg2 + "'."},
        {"MessageArgs", {arg1, arg2}},
        {"Severity", "OK"},
        {"Resolution", "None."}};
}
inline nlohmann::json transferFailed(const std::string& arg1,
                                     const std::string& arg2)
{
    return nlohmann::json{
        {"@odata.type", "#MessageRegistry.v1_4_1.MessageRegistry"},
        {"MessageId", "Update.1.0.TransferFailed"},
        {"Message",
         "Transfer of image '" + arg1 + "' to '" + arg2 + "' failed."},
        {"MessageArgs", {arg1, arg2}},
        {"Severity", "Critical"},
        {"Resolution", "None."}};
}
inline nlohmann::json verificationFailed(const std::string& arg1,
                                         const std::string& arg2)
{
    return nlohmann::json{
        {"@odata.type", "#MessageRegistry.v1_4_1.MessageRegistry"},
        {"MessageId", "Update.1.0.VerificationFailed"},
        {"Message",
         "Verification of image '" + arg1 + "' at '" + arg2 + "' failed."},
        {"MessageArgs", {arg1, arg2}},
        {"Severity", "Critical"},
        {"Resolution", "None."}};
}
inline nlohmann::json applyFailed(const std::string& arg1,
                                  const std::string& arg2)
{
    return nlohmann::json{
        {"@odata.type", "#MessageRegistry.v1_4_1.MessageRegistry"},
        {"MessageId", "Update.1.0.ApplyFailed"},
        {"Message",
         "Installation of image '" + arg1 + "' to '" + arg2 + "' failed."},
        {"MessageArgs", {arg1, arg2}},
        {"Severity", "Critical"},
        {"Resolution", "None."}};
}
inline nlohmann::json activateFailed(const std::string& arg1,
                                     const std::string& arg2)
{
    return nlohmann::json{
        {"@odata.type", "#MessageRegistry.v1_4_1.MessageRegistry"},
        {"MessageId", "Update.1.0.ActivateFailed"},
        {"Message",
         "Activation of image '" + arg1 + "' on '" + arg2 + "' failed."},
        {"MessageArgs", {arg1, arg2}},
        {"Severity", "Critical"},
        {"Resolution", "None."}};
}
inline nlohmann::json awaitToUpdate(const std::string& arg1,
                                    const std::string& arg2)
{
    return nlohmann::json{
        {"@odata.type", "#MessageRegistry.v1_4_1.MessageRegistry"},
        {"MessageId", "Update.1.0.AwaitToUpdate"},
        {"Message",
         "Awaiting for an action to proceed with installing image '" + arg1 +
             "' on '" + arg2 + "'."},
        {"MessageArgs", {arg1, arg2}},
        {"Severity", "OK"},
        {"Resolution",
         "Perform the requested action to advance the update operation."}};
}
inline nlohmann::json awaitToActivate(const std::string& arg1,
                                      const std::string& arg2)
{
    return nlohmann::json{
        {"@odata.type", "#MessageRegistry.v1_4_1.MessageRegistry"},
        {"MessageId", "Update.1.0.AwaitToActivate"},
        {"Message",
         "Awaiting for an action to proceed with activating image '" + arg1 +
             "' on '" + arg2 + "'."},
        {"MessageArgs", {arg1, arg2}},
        {"Severity", "OK"},
        {"Resolution",
         "Perform the requested action to advance the update operation."}};
}
inline nlohmann::json updateSuccessful(const std::string& arg1,
                                       const std::string& arg2)
{
    return nlohmann::json{
        {"@odata.type", "#MessageRegistry.v1_4_1.MessageRegistry"},
        {"MessageId", "Update.1.0.UpdateSuccessful"},
        {"Message", "Device '" + arg1 + "' successfully updated with image '" +
                        arg2 + "'."},
        {"MessageArgs", {arg1, arg2}},
        {"Severity", "OK"},
        {"Resolution", "None."}};
}
inline nlohmann::json operationTransitionedToJob(const std::string& arg1)
{
    return nlohmann::json{
        {"@odata.type", "#MessageRegistry.v1_4_1.MessageRegistry"},
        {"MessageId", "Update.1.0.OperationTransitionedToJob"},
        {"Message",
         "The update operation has transitioned to the job at URI '" + arg1 +
             "'."},
        {"MessageArgs", {arg1}},
        {"Severity", "OK"},
        {"Resolution",
         "Follow the referenced job and monitor the job for further updates."}};
}

inline nlohmann::json resourceErrorsDetected(const std::string& arg1,
                                             const std::string& arg2)
{
    return nlohmann::json{
        {"@odata.type", "#MessageRegistry.v1_4_1.MessageRegistry"},
        {"MessageId", "ResourceEvent.1.0.ResourceErrorsDetected"},
        {"Message", "The resource property '" + arg1 +
                        "' has detected errors of type '" + arg2 + "'."},
        {"MessageArgs", {arg1, arg2}},
        {"Severity", "Critical"},
        {"Resolution", "None."}};
}

inline nlohmann::json componentUpdateSkipped(const std::string& arg1,
                                             const std::string& arg2)
{
    return nlohmann::json{
        {"@odata.type", "#MessageRegistry.v1_4_1.MessageRegistry"},
        {"MessageId", "NvidiaUpdate.1.0.ComponentUpdateSkipped"},
        {"Message", "The update operation for the component '" + arg1 +
                        "' is skipped because '" + arg2 + "'."},
        {"MessageArgs", {arg1, arg2}},
        {"Severity", "OK"},
        {"Resolution", "None."}};
}

inline nlohmann::json recoveryStarted(const std::string& arg1)
{
    return nlohmann::json{
        {"@odata.type", "#MessageRegistry.v1_4_1.MessageRegistry"},
        {"MessageId", "NvidiaUpdate.1.0.RecoveryStarted"},
        {"Message", "Firmware Recovery Started on '" + arg1 + "'."},
        {"MessageArgs", {arg1}},
        {"Severity", "OK"},
        {"Resolution", "None."}};
}

inline nlohmann::json recoverySuccessful(const std::string& arg1)
{
    return nlohmann::json{
        {"@odata.type", "#MessageRegistry.v1_4_1.MessageRegistry"},
        {"MessageId", "NvidiaUpdate.1.0.RecoverySuccessful"},
        {"Message", "Firmware '" + arg1 + "' is successfully recovered."},
        {"MessageArgs", {arg1}},
        {"Severity", "OK"},
        {"Resolution", "None."}};
}

inline nlohmann::json firmwareNotInRecovery(const std::string& arg1)
{
    return nlohmann::json{
        {"@odata.type", "#MessageRegistry.v1_4_1.MessageRegistry"},
        {"MessageId", "NvidiaUpdate.1.0.FirmwareNotInRecovery"},
        {"Message", "Firmware '" + arg1 + "' is not in Recovery."},
        {"MessageArgs", {arg1}},
        {"Severity", "OK"},
        {"Resolution", "None."}};
}

inline nlohmann::json stageSuccessful(const std::string& arg1,
                                      const std::string& arg2)
{
    return nlohmann::json{
        {"@odata.type", "#MessageRegistry.v1_4_1.MessageRegistry"},
        {"MessageId", "NvidiaUpdate.1.0.StageSuccessful"},
        {"Message", "Device '" + arg1 + "' successfully staged with image '" +
                        arg2 + "'."},
        {"MessageArgs", {arg1, arg2}},
        {"Severity", "OK"},
        {"Resolution", "None."}};
}

inline nlohmann::json getUpdateMessage(const std::string& msgId,
                                       std::vector<std::string>& args)
{
    std::string arg1;
    std::string arg2;
    if (!args.empty())
    {
        arg1 = args[0];
    }
    if (args.size() >= 2)
    {
        arg2 = args[1];
    }

    if (msgId == "Update.1.0.TargetDetermined")
    {
        return targetDetermined(arg1, arg2);
    }
    if (msgId == "Update.1.0.AllTargetsDetermined")
    {
        return allTargetsDetermined();
    }
    if (msgId == "Update.1.0.UpdateInProgress")
    {
        return updateInProgress();
    }
    if (msgId == "Update.1.0.TransferringToComponent")
    {
        return transferringToComponent(arg1, arg2);
    }
    if (msgId == "Update.1.0.VerifyingAtComponent")
    {
        return verifyingAtComponent(arg1, arg2);
    }
    if (msgId == "Update.1.0.InstallingOnComponent")
    {
        return installingOnComponent(arg1, arg2);
    }
    if (msgId == "Update.1.0.ApplyingOnComponent")
    {
        return applyingOnComponent(arg1, arg2);
    }
    if (msgId == "Update.1.0.TransferFailed")
    {
        return transferFailed(arg1, arg2);
    }
    if (msgId == "Update.1.0.VerificationFailed")
    {
        return verificationFailed(arg1, arg2);
    }
    if (msgId == "Update.1.0.ApplyFailed")
    {
        return applyFailed(arg1, arg2);
    }
    if (msgId == "Update.1.0.ActivateFailed")
    {
        return activateFailed(arg1, arg2);
    }
    if (msgId == "Update.1.0.AwaitToUpdate")
    {
        return awaitToUpdate(arg1, arg2);
    }
    if (msgId == "Update.1.0.AwaitToActivate")
    {
        return awaitToActivate(arg1, arg2);
    }
    if (msgId == "Update.1.0.UpdateSuccessful")
    {
        return updateSuccessful(arg1, arg2);
    }
    if (msgId == "Update.1.0.OperationTransitionedToJob")
    {
        return operationTransitionedToJob(arg1);
    }
    if (msgId == "ResourceEvent.1.0.ResourceErrorsDetected")
    {
        return resourceErrorsDetected(arg1, arg2);
    }
    if (msgId == "NvidiaUpdate.1.0.ComponentUpdateSkipped")
    {
        return componentUpdateSkipped(arg1, arg2);
    }
    if (msgId == "NvidiaUpdate.1.0.RecoveryStarted")
    {
        return recoveryStarted(arg1);
    }
    if (msgId == "NvidiaUpdate.1.0.RecoverySuccessful")
    {
        return recoverySuccessful(arg1);
    }
    if (msgId == "NvidiaUpdate.1.0.FirmwareNotInRecovery")
    {
        return firmwareNotInRecovery(arg1);
    }
    if (msgId == "NvidiaUpdate.1.0.StageSuccessful")
    {
        return stageSuccessful(arg1, arg2);
    }
    if (msgId == "NvidiaUpdate.1.0.DebugTokenEraseFailed")
    {
        return messages::debugTokenEraseFailed(arg1, arg2);
    }

    return {};
}
} // namespace messages
} // namespace redfish
||||||| 80d2ef31c
=======
#pragma once
/****************************************************************
 *                 READ THIS WARNING FIRST
 * This is an auto-generated header which contains definitions
 * for Redfish DMTF defined messages.
 * DO NOT modify this registry outside of running the
 * parse_registries.py script.  The definitions contained within
 * this file are owned by DMTF.  Any modifications to these files
 * should be first pushed to the relevant registry in the DMTF
 * github organization.
 ***************************************************************/
// These generated headers are a superset of what is needed.
// clang sees them as an error, so ignore
// NOLINTBEGIN(misc-include-cleaner)
#include "http_response.hpp"

#include <boost/url/url_view_base.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <source_location>
#include <string_view>
// NOLINTEND(misc-include-cleaner)

namespace redfish
{

namespace messages
{
nlohmann::json::object_t targetDetermined(std::string_view arg1,
                                          std::string_view arg2);

nlohmann::json::object_t allTargetsDetermined();

nlohmann::json::object_t noTargetsDetermined(std::string_view arg1);

nlohmann::json::object_t updateInProgress();

nlohmann::json::object_t transferringToComponent(std::string_view arg1,
                                                 std::string_view arg2);

nlohmann::json::object_t verifyingAtComponent(std::string_view arg1,
                                              std::string_view arg2);

nlohmann::json::object_t installingOnComponent(std::string_view arg1,
                                               std::string_view arg2);

nlohmann::json::object_t applyingOnComponent(std::string_view arg1,
                                             std::string_view arg2);

nlohmann::json::object_t transferFailed(std::string_view arg1,
                                        std::string_view arg2);

nlohmann::json::object_t verificationFailed(std::string_view arg1,
                                            std::string_view arg2);

nlohmann::json::object_t applyFailed(std::string_view arg1,
                                     std::string_view arg2);

nlohmann::json::object_t activateFailed(std::string_view arg1,
                                        std::string_view arg2);

nlohmann::json::object_t awaitToUpdate(std::string_view arg1,
                                       std::string_view arg2);

nlohmann::json::object_t awaitToActivate(std::string_view arg1,
                                         std::string_view arg2);

nlohmann::json::object_t updateSuccessful(std::string_view arg1,
                                          std::string_view arg2);

nlohmann::json::object_t operationTransitionedToJob(std::string_view arg1);

nlohmann::json::object_t updateSkipped(std::string_view arg1,
                                       std::string_view arg2);

nlohmann::json::object_t updateSkippedSameVersion(std::string_view arg1,
                                                  std::string_view arg2);

nlohmann::json::object_t updateNotApplicable(std::string_view arg1,
                                             std::string_view arg2);

} // namespace messages
} // namespace redfish
>>>>>>> origin/master
