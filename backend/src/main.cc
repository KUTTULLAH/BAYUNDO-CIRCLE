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

#include "database_config.h"

namespace
{
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
    if (!jirani::readDatabaseConfig(databaseConfig))
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
                    LOG_ERROR << "Database health check failed.";
                    (*responseCallback)(healthResponse(false));
                });
        },
        {drogon::Get});

    drogon::app().addListener("0.0.0.0", port).run();
    return EXIT_SUCCESS;
}
