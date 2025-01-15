# Nvidia Bmcweb development 

### Motivation 

- Upstream Nvidia’s bmcweb contribution to Openbmc bmcweb repo  
- Ease the process of performing upstream sync and reduce merge conflicts 
- Upstream Nvidia OEM code to OpenBMC repo 

The followings are the guidelines to be following while enhancing the functionalities 
of schema which does not exist in bmcweb upstream repo. These are applicable to schema and properties 
supported by DMTF and Nvidia Specific OEM Schema. 

## Schema Enhancement 

1. Create separate file for with naming nvidia_\<schema\>.hpp to manage to newly added code. 
2. Extend the existing route handlers to call the nvidia extended code handlers.
3. Separate out DMTF compliant code addition vs Nvidia OEM Code addition. This separation can help us easily distinguish potential DMtF schema enhancements which can be upstream vs OEM/ODM code. 

Extending Get/Patch handlers 

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

                        // nvidia extension 
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

                 // nvidia extension 
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

4. As much as possible keep all the new routes handlers in nvidia_\<schema\>.hpp. The only exception can be too much functional dependency with the util codes used by the original handler.
4. If the schmea does not exist in upstream then it can be still added with nvidia file prefix. This helps us identify later in the upstream merge if the code is added by nvidia vs upstream.  
5. Create [gerrit](https://gerrit.openbmc.org/q/project:openbmc%252Fbmcweb) MR for the code enhanced. Follow up with community to get the MR merged upstream. 
6. Once Upstream MR is merged, ensure downstream code is aligned by migrating code back to original schema from nvidia_\<schema\>.hpp file. 


## Testing 

1. For non-trivial cases extend the UT for the newly added schema support. File name nvidia_\<schema\>_test.cpp.
2. Ensure DMTF service validator passes.
3. Ensure Nvidia service[^1] validator passes.
4. If the changes are expected to impact performance then ensure nvidia performance tests[^2] are covered.  

## Footnote 

[^1]: [RF Perf Tests](https://gitlab-master.nvidia.com/dgx/bmc/openbmc-test-automation/-/blob/develop/resiliency_tests/README.md?ref_type=heads#performance-test) 
[^2]: Nvidia Service Validator 
