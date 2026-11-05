#include <drogon/drogon.h>
#include <sodium.h>

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
#include "api_routes.h"

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

bool readBindAddress(std::string &address)
{
    const char *configuredAddress = std::getenv("API_BIND_ADDRESS");
    if (configuredAddress == nullptr)
    {
        address = "127.0.0.1";
        return true;
    }

    const std::string_view value{configuredAddress};
    if (value.empty() || value.size() > 255)
    {
        std::cerr << "API_BIND_ADDRESS must contain 1-255 characters.\n";
        return false;
    }
    address = value;
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
    std::string bindAddress;
    if (!readPort(port) || !readBindAddress(bindAddress))
    {
        return EXIT_FAILURE;
    }

    drogon::orm::MysqlConfig databaseConfig{};
    if (!jirani::readDatabaseConfig(databaseConfig))
    {
        return EXIT_FAILURE;
    }

    drogon::app().addDbClient(databaseConfig);
    if (sodium_init() < 0)
    {
        std::cerr << "Could not initialize the cryptographic library.\n";
        return EXIT_FAILURE;
    }
    jirani::registerApiRoutes();
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

    drogon::app().addListener(bindAddress, port).run();
    return EXIT_SUCCESS;
}
