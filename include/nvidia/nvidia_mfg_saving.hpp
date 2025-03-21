namespace nvidia_mfg_saving
{

struct CBCFruRequest
{
    uint8_t nodeNumber = 0;
    std::string topology = "none";
};

inline void
    systemdStartUnit(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                     const CBCFruRequest& cbcFruRequest)
{
    std::string unit = std::format("nv-cbc-mfg-fix@{}-{}.service",
                                   cbcFruRequest.nodeNumber,
                                   cbcFruRequest.topology);

    crow::connections::systemBus->async_method_call(
        [asyncResp, unit](const boost::system::error_code& ec,
                          const std::string& jobId) {
        if (ec)
        {
            BMCWEB_LOG_ERROR("Failed to start unit: {}", ec.message());
            asyncResp->res.result(
                boost::beast::http::status::internal_server_error);
            return;
        }
        asyncResp->res.result(boost::beast::http::status::ok);

        BMCWEB_LOG_ERROR("Started unit: {} with job id: {}", unit, jobId);
    },
        "org.freedesktop.systemd1", "/org/freedesktop/systemd1",
        "org.freedesktop.systemd1.Manager", "StartUnit", unit, "fail");
}

inline void doMfgSaving(const crow::Request& req,
                        const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    nlohmann::json requestMfgData;
    JsonParseResult ret = parseRequestAsJson(req, requestMfgData);
    if (ret == JsonParseResult::BadContentType)
    {
        asyncResp->res.result(boost::beast::http::status::bad_request);
        return;
    }
    if (ret != JsonParseResult::Success)
    {
        asyncResp->res.result(boost::beast::http::status::bad_request);
        return;
    }

    CBCFruRequest cbcFruRequest;

    for (const auto& element : requestMfgData.items())
    {
        const std::string& key = element.key();
        const nlohmann::json& value = element.value();

        if (key == "nodeNumber")
        {
            cbcFruRequest.nodeNumber = value.get<uint8_t>();
        }
        else if (key == "topology")
        {
            cbcFruRequest.topology = value.get<std::string>();
        }
        else
        {
            BMCWEB_LOG_ERROR("Invalid key: {}", key);
            asyncResp->res.result(boost::beast::http::status::bad_request);
            return;
        }
    }

    systemdStartUnit(asyncResp, cbcFruRequest);
}

inline void requestRoutes(App& app)
{
    BMCWEB_ROUTE(app, "/nvidia/cbc_mfg_fix")
        .privileges({{"ConfigureComponents", "ConfigureManager"}})
        .methods(boost::beast::http::verb::post)(doMfgSaving);
}

} // namespace nvidia_mfg_saving
