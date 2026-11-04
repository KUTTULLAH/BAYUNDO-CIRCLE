#pragma once

#include <drogon/drogon.h>

#include <charconv>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>

namespace jirani
{
inline bool readRequiredEnvironment(const char *name, std::string &value)
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

inline bool readDatabaseConfig(drogon::orm::MysqlConfig &config)
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

inline std::string databaseConnectionInfo(
    const drogon::orm::MysqlConfig &config)
{
    const auto quote = [](const std::string &value) {
        std::string escaped;
        escaped.reserve(value.size() + 2);
        escaped.push_back('\'');
        for (const char ch : value)
        {
            if (ch == '\'' || ch == '\\')
            {
                escaped.push_back('\\');
            }
            escaped.push_back(ch);
        }
        escaped.push_back('\'');
        return escaped;
    };

    return "host=" + quote(config.host) +
           " port=" + std::to_string(config.port) +
           " dbname=" + quote(config.databaseName) +
           " user=" + quote(config.username) +
           " password=" + quote(config.password) +
           " client_encoding=" + quote(config.characterSet);
}

}  // namespace jirani
