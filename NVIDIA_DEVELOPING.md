# NVIDIA bmcweb development

## Motivation

Keep NVIDIA contributions to bmcweb maintainable, sharable to partners, and
reusable to future products. What follows are intended to be guidelines, not
rules, and good judgement should _always_ prevail over following a guildelines
to the letter. The goal of this is to ship products. this repository is one tool
of many to accomplish that goal.

If there are questions on the specific applications of these guildelines, please
reach out to the bmcweb Nvidia maintainers and they will clarify intent.

## Creating Additional schemas

1. Follow the Nvidia Redfish schema pack guidelines[1] to create a new schema,
   or modification to the schema, including mockup changes. Ensure that the
   schema is merged ahead of primary development to reduce rework.
2. Run scripts/update*nvidia_schemas.py. This will pull the merged changes from
   #1 and incorporate them into bmcweb. Push this schema change into an MR ahead
   of code. Note, this script \_may* pull in schema changes unrelated to yours,
   this is expected.
3. Create a new file, following the pattern
   `redfish-core/lib/nvidia_<schema_name>.hpp`. Ensure that schema_name is
   synced with the schema being extended.
4. Add a single line callback to extend the existing route handlers to call the
   NVIDIA callbacks.
5. Ensure that DMTF-compliant code is kept separate from NVIDIA OEM code
   additions through the use of more files. This separation helps distinguish
   potential DMTF schema enhancements that can be upstreamed versus OEM/ODM
   code.

Examples of extension handlers.

redfish-core/lib/managers.hpp

```cpp
void handleManagersGet(App& app, const crow::Request& req,
                        const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                        const std::string& managerId) {

   // Ensure that nvidia extensions are a single line change to existing code
   nvidiaManagerGet(req, asyncResp, managerId);
}
```

redfish-core/lib/nvidia_managers.hpp

```cpp
   void nvidiaManagerGet(....){
      // Ensure that this is before any Nvidia OEM extension.
      if (!BMCWEB_NVIDIA_OEM_PROPERTIES){
         return;
      }
      // Nvidia specific implementation
   }
```

1. Unless immediately being sent upstream, keep new route handlers in separate
   files, with the convention `redfish-core/lib/nvidia_<schema>.hpp`. Exceptions
   may be made on a case by case basis.
2. Create [gerrit](https://gerrit.openbmc.org) MR for the code enhanced. Follow
   up with community to get the MR merged upstream.
3. Once Upstream MR is merged, ensure downstream code is aligned by migrating
   code back to original schema from `nvidia_<schema>.hpp` file.
4. Downstream changes up to 3 lines can be kept in the same file. This is
   intentionally very small to ensure rebases can be performed in the future.

Nvidia specific Redfish implementations should be placed in the redfish::nvidia
c++ namespace.

## Testing

1. Nvidia functions shall include unit tests with target 80% coverage.
2. Ensure DMTF service validator passes on target platform.
3. Consider running performance tests [2].

## Footnote

[1]:
  https://gitlab-master.nvidia.com/dgx/redfish/-/blob/develop/README.md?ref_type=heads
[2]:
  https://gitlab-master.nvidia.com/dgx/bmc/openbmc-test-automation/-/blob/develop/resiliency_tests/README.md?ref_type=heads#performance-test
