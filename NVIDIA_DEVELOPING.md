# NVIDIA bmcweb development

## Motivation

- Upstream NVIDIA’s bmcweb contributions to the OpenBMC bmcweb repository
- Simplify upstream sync and reduce merge conflicts
- Upstream NVIDIA OEM code to the OpenBMC repository

The following guidelines apply when adding schema functionality that does not
exist in the upstream bmcweb repository. These apply to schemas and properties
defined by DMTF as well as NVIDIA-specific OEM schemas.

## Schema Enhancement

1. Create a separate file named `nvidia_<schema>.hpp` to contain newly added
   code.
2. Extend the existing route handlers to call the NVIDIA extension handlers.
3. Separate DMTF-compliant code additions from NVIDIA OEM code additions. This
   separation helps distinguish potential DMTF schema enhancements that can be
   upstreamed versus OEM/ODM code.

Extending GET/PATCH handlers

```cpp
BMCWEB_ROUTE(app, "/redfish/v1/Managers/<str>/")
    .privileges(redfish::privileges::getManager)
    .methods(
        boost::beast::http::verb::
            get)([&app,
                  uuid](const crow::Request& req,
                        const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                        const std::string& managerId) {

                        // upstream code

                        // NVIDIA extension
                        extendManagerGet(req, asyncResp, managerId);
                        }
```

```cpp
BMCWEB_ROUTE(app, "/redfish/v1/Managers/<str>/")
    .privileges(redfish::privileges::patchManager)
    .methods(boost::beast::http::verb::patch)(
        [&app](const crow::Request& req,
                const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                const std::string& managerId) {

                // upstream code

                 // NVIDIA extension
                extendManagerPatch(req, asyncResp, managerId);
                }

```

OEM/ODM code

```cpp
inline void
    extendManagerGet(const crow::Request& /*req*/,
                     const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                     const std::string& managerId)
{
    // DMTF code


    // OEM Code

    if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
    {
        extendManagerOEMGet(req, asyncResp, managerId);
    }
}

```

1. As much as possible keep all the new routes handlers in
   `nvidia_<schema>.hpp`. The only exception can be too much functional
   dependency with the util codes used by the original handler.
2. If the schmea does not exist in upstream then it can be still added with
   nvidia file prefix. This helps us identify later in the upstream merge if the
   code is added by nvidia vs upstream.
3. Create [gerrit](https://gerrit.openbmc.org/q/project:openbmc%252Fbmcweb) MR
   for the code enhanced. Follow up with community to get the MR merged
   upstream.
4. Once Upstream MR is merged, ensure downstream code is aligned by migrating
   code back to original schema from `nvidia_<schema>.hpp` file.

If the downstream has additional handling which is more than 5 lines then, it
can be added in new function at end of the file if there are not much issue in
passing the arguments to new function like managerid or asyncResp etc. if it is
dependent on local file data like static variables

```cpp
inline void
    ManagerGet(const crow::Request& /*req*/,
                     const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                     const std::string& managerId)
{
   // Upstream code

   nvidiaManagerIdHandle(asyncResp, managerId);
}

// At the end of the file
void nvidiaManagerIdHandle(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp, const std::string& managerId)
{

}

```

If the downstream has additional handling of more than five lines and it depends
on class member data, add a new function within the same class when creating a
free function is not a good option.

```cpp
class ConnectionInfo
{
   void shutdownConn(bool retry)
   {
      nvidiaShutdown(retry);
   }

   void nvidiaShutdown(bool retry)
   {
      // NVIDIA handling
   }
}

```

If the downstream implementation is different from the upstream implementation,
it can be placed in a new namespace.

```cpp
// Downstream implementation in new namespace example
namespace persistent_data::nvidia
{
   class Config
   {

   }
}
```

If the upstream code is not used and downstream has its own implementation, the
upstream code can be gated behind a Meson option macro to disable it.

```text
upstream-<featurename>-unused-code

```

## Testing

1. For non-trivial cases extend the UT for the newly added schema support. File
   name `nvidia_<schema>_test.cpp`.
2. Ensure DMTF service validator passes.
3. Ensure Nvidia service[^1] validator passes.
4. If the changes are expected to impact performance then ensure nvidia
   performance tests[^2] are covered.

## Footnote

[^1]:
    [RF Perf Tests](https://gitlab-master.nvidia.com/dgx/bmc/openbmc-test-automation/-/blob/develop/resiliency_tests/README.md?ref_type=heads#performance-test)

[^2]: Nvidia Service Validator
