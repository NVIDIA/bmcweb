#pragma once

#include "event_service_store.hpp"
#include "http_request.hpp"
#include "http_response.hpp"
#include "ossl_random.hpp"
#include "sessions.hpp"

#include <boost/beast/http/fields.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <random>

namespace persistent_data
{

class ConfigFile
{
    uint64_t jsonRevision = 1;
    bool tlsAuth = false;
    bool tlsAuthToWrite = false;

  public:
    // todo(ed) should read this from a fixed location somewhere, not CWD
    static constexpr const char* filename =
        "/var/lib/bmcweb/bmcweb_persistent_data.json";

    ConfigFile()
    {
        readData();
    }

    ~ConfigFile()
    {
        // Make sure we aren't writing stale sessions
        persistent_data::SessionStore::getInstance().applySessionTimeouts();
        if (persistent_data::SessionStore::getInstance().needsWrite())
        {
            writeData();
        }
    }

    ConfigFile(const ConfigFile&) = delete;
    ConfigFile(ConfigFile&&) = delete;
    ConfigFile& operator=(const ConfigFile&) = delete;
    ConfigFile& operator=(ConfigFile&&) = delete;

#ifdef BMCWEB_ENABLE_TLS_AUTH_OPT_IN
    void enableTLSAuth()
    {
        tlsAuthToWrite = true;
        writeData();
    }

    bool isTLSAuthEnabled() const
    {
        return tlsAuth;
    }
#else
    // If BMCWEB_ENABLE_TLS_AUTH_OPT_IN is not enabled then the runtime check is
    // always true, and the enablement depends on the various #ifdef checks.
    constexpr bool isTLSAuthEnabled()
    {
        return true;
    }
#endif

    // TODO(ed) this should really use protobuf, or some other serialization
    // library, but adding another dependency is somewhat outside the scope of
    // this application for the moment
    void readData()
    {
        std::ifstream persistentFile(filename);
        uint64_t fileRevision = 0;
        if (persistentFile.is_open())
        {
            // call with exceptions disabled
            auto data = nlohmann::json::parse(persistentFile, nullptr, false);
            if (data.is_discarded())
            {
                BMCWEB_LOG_ERROR("Error parsing persistent data in json file.");
            }
            else
            {
                const nlohmann::json::object_t* obj =
                    data.get_ptr<nlohmann::json::object_t*>();
                if (obj == nullptr)
                {
                    return;
                }
                for (const auto& item : *obj)
                {
                    if (item.first == "revision")
                    {
                        fileRevision = 0;

                        const uint64_t* uintPtr =
                            item.second.get_ptr<const uint64_t*>();
                        if (uintPtr == nullptr)
                        {
                            BMCWEB_LOG_ERROR("Failed to read revision flag");
                        }
                        else
                        {
                            fileRevision = *uintPtr;
                        }
                    }
                    else if (item.first == "system_uuid")
                    {
                        const std::string* jSystemUuid =
                            item.second.get_ptr<const std::string*>();
                        if (jSystemUuid != nullptr)
                        {
                            systemUuid = *jSystemUuid;
                        }
                    }
                    else if (item.first == "tls_auth_enabled")
                    {
                        const bool* ptr = item.second.get_ptr<const bool*>();
                        if (ptr != nullptr)
                        {
                            tlsAuth = *ptr;
                            tlsAuthToWrite = *ptr;
                        }
                    }
                    else if (item.first == "auth_config")
                    {
                        SessionStore::getInstance()
                            .getAuthMethodsConfig()
                            .fromJson(item.second);
                    }
                    else if (item.first == "sessions")
                    {
                        for (const auto& elem : item.second)
                        {
                            std::shared_ptr<UserSession> newSession =
                                UserSession::fromJson(elem);

                            if (newSession == nullptr)
                            {
                                BMCWEB_LOG_ERROR("Problem reading session "
                                                 "from persistent store");
                                continue;
                            }

                            BMCWEB_LOG_DEBUG("Restored session: {} {} {}",
                                             newSession->csrfToken,
                                             newSession->uniqueId,
                                             newSession->sessionToken);
                            SessionStore::getInstance().authTokens.emplace(
                                newSession->sessionToken, newSession);
                        }
                    }
                    else if (item.first == "timeout")
                    {
                        const int64_t* jTimeout =
                            item.second.get_ptr<const int64_t*>();
                        if (jTimeout == nullptr)
                        {
                            BMCWEB_LOG_DEBUG(
                                "Problem reading session timeout value");
                            continue;
                        }
                        std::chrono::seconds sessionTimeoutInseconds(*jTimeout);
                        BMCWEB_LOG_DEBUG("Restored Session Timeout: {}",
                                         sessionTimeoutInseconds.count());
                        SessionStore::getInstance().updateSessionTimeout(
                            sessionTimeoutInseconds);
                    }
                    else if (item.first == "eventservice_config")
                    {
                        const nlohmann::json::object_t* esobj =
                            item.second
                                .get_ptr<const nlohmann::json::object_t*>();
                        if (esobj == nullptr)
                        {
                            BMCWEB_LOG_DEBUG(
                                "Problem reading EventService value");
                            continue;
                        }

                        EventServiceStore::getInstance()
                            .getEventServiceConfig()
                            .fromJson(*esobj);
                    }
                    else if (item.first == "subscriptions")
                    {
                        for (const auto& elem : item.second)
                        {
                            std::shared_ptr<UserSubscription> newSubscription =
                                UserSubscription::fromJson(elem);

                            if (newSubscription == nullptr)
                            {
                                BMCWEB_LOG_ERROR("Problem reading subscription "
                                                 "from persistent store");
                                continue;
                            }

                            BMCWEB_LOG_DEBUG("Restored subscription: {} {}",
                                             newSubscription->id,
                                             newSubscription->customText);
                            EventServiceStore::getInstance()
                                .subscriptionsConfigMap.emplace(
                                    newSubscription->id, newSubscription);
                        }
                    }
                    else
                    {
                        // Do nothing in the case of extra fields.  We may have
                        // cases where fields are added in the future, and we
                        // want to at least attempt to gracefully support
                        // downgrades in that case, even if we don't officially
                        // support it
                    }
                }
            }
        }
        bool needWrite = false;

#ifdef BMCWEB_ENABLE_TLS_AUTH_OPT_IN
        {
            AuthConfigMethods& authMethodsConfig =
                SessionStore::getInstance().getAuthMethodsConfig();
            authMethodsConfig.basic = isTLSAuthEnabled();
            authMethodsConfig.cookie = isTLSAuthEnabled();
            authMethodsConfig.xtoken = isTLSAuthEnabled();
            authMethodsConfig.sessionToken = isTLSAuthEnabled();
            authMethodsConfig.tls = isTLSAuthEnabled();
            needWrite = true;
        }
#endif

        if (systemUuid.empty())
        {
            systemUuid = bmcweb::getRandomUUID();
            needWrite = true;
        }
        if (fileRevision < jsonRevision)
        {
            needWrite = true;
        }
        // write revision changes or system uuid changes immediately
        if (needWrite)
        {
            writeData();
        }
    }

    void writeData()
    {
        boost::beast::file_posix persistentFile;
        boost::system::error_code ec;
        persistentFile.open(filename, boost::beast::file_mode::write, ec);
        if (ec)
        {
            BMCWEB_LOG_CRITICAL("Unable to store persistent data to file");
            return;
        }

        // set the permission of the file to 640
        std::filesystem::perms permission =
            std::filesystem::perms::owner_read |
            std::filesystem::perms::owner_write |
            std::filesystem::perms::group_read;
        std::filesystem::permissions(filename, permission);
        const AuthConfigMethods& c =
            SessionStore::getInstance().getAuthMethodsConfig();
        const auto& eventServiceConfig =
            EventServiceStore::getInstance().getEventServiceConfig();
        nlohmann::json::object_t data;
        nlohmann::json& authConfig = data["auth_config"];

        authConfig["XToken"] = c.xtoken;
        authConfig["Cookie"] = c.cookie;
        authConfig["SessionToken"] = c.sessionToken;
        authConfig["BasicAuth"] = c.basic;
        authConfig["TLS"] = c.tls;
        authConfig["TLSCommonNameParseMode"] =
            static_cast<int>(c.mTLSCommonNameParsingMode);

        nlohmann::json& eventserviceConfig = data["eventservice_config"];
        eventserviceConfig["ServiceEnabled"] = eventServiceConfig.enabled;
        eventserviceConfig["DeliveryRetryAttempts"] =
            eventServiceConfig.retryAttempts;
        eventserviceConfig["DeliveryRetryIntervalSeconds"] =
            eventServiceConfig.retryTimeoutInterval;

        data["system_uuid"] = systemUuid;
        data["tls_auth_enabled"] = tlsAuthToWrite;
        data["revision"] = jsonRevision;
        data["timeout"] = SessionStore::getInstance().getTimeoutInSeconds();

        nlohmann::json& sessions = data["sessions"];
        sessions = nlohmann::json::array();
        for (const auto& p : SessionStore::getInstance().authTokens)
        {
            if (p.second->persistence !=
                persistent_data::PersistenceType::SINGLE_REQUEST)
            {
                nlohmann::json::object_t session;
                session["unique_id"] = p.second->uniqueId;
                session["session_token"] = p.second->sessionToken;
                session["username"] = p.second->username;
                session["csrf_token"] = p.second->csrfToken;
                session["client_ip"] = p.second->clientIp;
                const std::optional<std::string>& clientId = p.second->clientId;
                if (clientId)
                {
                    session["client_id"] = *clientId;
                }
                sessions.emplace_back(std::move(session));
            }
        }
        nlohmann::json& subscriptions = data["subscriptions"];
        subscriptions = nlohmann::json::array();
        for (const auto& it :
             EventServiceStore::getInstance().subscriptionsConfigMap)
        {
            std::shared_ptr<UserSubscription> subValue = it.second;
            if (subValue->subscriptionType == "SSE")
            {
                BMCWEB_LOG_DEBUG("The subscription type is SSE, so skipping.");
                continue;
            }
            nlohmann::json::object_t headers;
            for (const boost::beast::http::fields::value_type& header :
                 subValue->httpHeaders)
            {
                // Note, these are technically copies because nlohmann doesn't
                // support key lookup by std::string_view.  At least the
                // following code can use move
                // https://github.com/nlohmann/json/issues/1529
                std::string name(header.name_string());
                headers[std::move(name)] = header.value();
            }

            nlohmann::json::object_t subscription;

            subscription["Id"] = subValue->id;
            subscription["Context"] = subValue->customText;
            subscription["DeliveryRetryPolicy"] = subValue->retryPolicy;
            subscription["Destination"] = subValue->destinationUrl;
            subscription["EventFormatType"] = subValue->eventFormatType;
            subscription["HttpHeaders"] = std::move(headers);
            subscription["MessageIds"] = subValue->registryMsgIds;
            subscription["Protocol"] = subValue->protocol;
            subscription["RegistryPrefixes"] = subValue->registryPrefixes;
            subscription["OriginResources"] = subValue->originResources;
            subscription["ResourceTypes"] = subValue->resourceTypes;
            subscription["SubscriptionType"] = subValue->subscriptionType;
            subscription["MetricReportDefinitions"] =
                subValue->metricReportDefinitions;

            subscriptions.emplace_back(std::move(subscription));
        }
        std::string dump = nlohmann::json(data).dump(
            -1, ' ', true, nlohmann::json::error_handler_t::replace);
        size_t writeBytes = persistentFile.write(dump.data(), dump.size(), ec);
        if (ec || !writeBytes)
        {
            BMCWEB_LOG_CRITICAL("Failed to log persistent File");
        }
    }

    std::string systemUuid;
};

inline ConfigFile& getConfig()
{
    static ConfigFile f;
    return f;
}

} // namespace persistent_data
