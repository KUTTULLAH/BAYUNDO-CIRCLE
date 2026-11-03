#include <drogon/drogon.h>

#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>

namespace
{
bool readRequiredEnvironment(const char *name, std::string &value)
{
    const char *configuredValue = std::getenv(name);
    if (configuredValue == nullptr || configuredValue[0] == '\0')
    {
        std::cerr << name << " must be set.\n";
        return false;
    }

    value = configuredValue;
    return true;
}

bool readPort(std::uint16_t &port)
{
    const char *configuredPort = std::getenv("PORT");
    if (configuredPort == nullptr)
    {
        port = 8080;
        return true;
    }

    const std::string_view value{configuredPort};
    unsigned int parsedPort = 0;
    const auto [end, error] =
        std::from_chars(value.data(), value.data() + value.size(), parsedPort);
    if (error != std::errc{} || end != value.data() + value.size() ||
        parsedPort == 0 || parsedPort > 65535)
    {
        std::cerr << "PORT must be an integer between 1 and 65535.\n";
        return false;
    }

    port = static_cast<std::uint16_t>(parsedPort);
    return true;
}

bool readDatabaseConfig(drogon::orm::MysqlConfig &config)
{
    unsigned int parsedPort = 3306;
    const char *configuredPort = std::getenv("DB_PORT");
    if (configuredPort != nullptr)
    {
        const std::string_view value{configuredPort};
        const auto [end, error] =
            std::from_chars(value.data(), value.data() + value.size(), parsedPort);
        if (error != std::errc{} || end != value.data() + value.size() ||
            parsedPort == 0 || parsedPort > 65535)
        {
            std::cerr << "DB_PORT must be an integer between 1 and 65535.\n";
            return false;
        }
    }

    config.port = static_cast<unsigned short>(parsedPort);
    config.connectionNumber = 1;
    config.name = "default";
    config.isFast = false;
    config.characterSet = "utf8mb4";
    config.timeout = 5.0;

    return readRequiredEnvironment("DB_HOST", config.host) &&
           readRequiredEnvironment("DB_NAME", config.databaseName) &&
           readRequiredEnvironment("DB_USER", config.username) &&
           readRequiredEnvironment("DB_PASSWORD", config.password);
}

drogon::HttpResponsePtr healthResponse(bool databaseReady)
{
    Json::Value body;
    body["status"] = databaseReady ? "ok" : "unavailable";
    auto response = drogon::HttpResponse::newHttpJsonResponse(body);
    if (!databaseReady)
    {
        response->setStatusCode(drogon::k503ServiceUnavailable);
    }
    return response;
}
}  // namespace

int main()
{
    std::uint16_t port = 0;
    if (!readPort(port))
    {
        return EXIT_FAILURE;
    }

    drogon::orm::MysqlConfig databaseConfig{};
    if (!readDatabaseConfig(databaseConfig))
    {
        return EXIT_FAILURE;
    }

    drogon::app().addDbClient(databaseConfig);
    drogon::app().registerHandler(
        "/api/v1/health",
        [](const drogon::HttpRequestPtr &,
           std::function<void(const drogon::HttpResponsePtr &)> &&callback) {
            using HealthCallback =
                std::function<void(const drogon::HttpResponsePtr &)>;
            auto responseCallback =
                std::make_shared<HealthCallback>(std::move(callback));
            drogon::app().getDbClient()->execSqlAsync(
                "SELECT 1",
                [responseCallback](const drogon::orm::Result &) {
                    (*responseCallback)(healthResponse(true));
                },
                [responseCallback](const drogon::orm::DrogonDbException &) {
                    (*responseCallback)(healthResponse(false));
                });
        },
        {drogon::Get});

    drogon::app().addListener("0.0.0.0", port).run();
    return EXIT_SUCCESS;
}
