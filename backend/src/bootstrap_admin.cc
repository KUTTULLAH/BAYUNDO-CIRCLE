#include <drogon/drogon.h>
#include <sodium.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <regex>
#include <stdexcept>
#include <string>
#include <string_view>

#ifdef _WIN32
#include <conio.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

#include "database_config.h"

namespace
{
constexpr std::string_view kBootstrapLock = "jirani_circle_initial_admin";

bool validName(const std::string &value)
{
    return !value.empty() && value.size() <= 120 &&
           std::any_of(value.begin(), value.end(), [](unsigned char ch) {
               return !std::isspace(ch);
           });
}

bool validPhone(const std::string &phone)
{
    static const std::regex pattern(R"(^\+[1-9][0-9]{7,14}$)");
    return std::regex_match(phone, pattern);
}

std::string readPassword()
{
    std::string password;
    std::cout << "Admin password (12-128 characters): " << std::flush;
#ifdef _WIN32
    for (;;)
    {
        const int ch = _getch();
        if (ch == '\r' || ch == '\n')
        {
            std::cout << '\n';
            break;
        }
        if (ch == 8)
        {
            if (!password.empty())
            {
                password.pop_back();
            }
            continue;
        }
        if (ch == 3)
        {
            throw std::runtime_error("Password entry was cancelled.");
        }
        if (ch >= 32 && ch <= 126)
        {
            password.push_back(static_cast<char>(ch));
        }
    }
#else
    termios previousSettings{};
    const bool isTerminal = isatty(STDIN_FILENO) == 1;
    if (isTerminal)
    {
        if (tcgetattr(STDIN_FILENO, &previousSettings) != 0)
        {
            throw std::runtime_error("Could not configure terminal password input.");
        }
        auto hiddenSettings = previousSettings;
        hiddenSettings.c_lflag &= static_cast<tcflag_t>(~ECHO);
        if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &hiddenSettings) != 0)
        {
            throw std::runtime_error("Could not hide terminal password input.");
        }
    }
    if (!std::getline(std::cin, password))
    {
        if (isTerminal)
        {
            tcsetattr(STDIN_FILENO, TCSAFLUSH, &previousSettings);
            std::cerr << '\n';
        }
        throw std::runtime_error("Could not read the admin password.");
    }
    if (isTerminal)
    {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &previousSettings);
        std::cout << '\n';
    }
#endif
    return password;
}

void releaseBootstrapLock(const drogon::orm::DbClientPtr &client)
{
    client->execSqlSync("SELECT RELEASE_LOCK(?)", std::string(kBootstrapLock));
}

void createInitialAdmin(const drogon::orm::DbClientPtr &client,
                        const std::string &chamaName,
                        const std::string &fullName,
                        const std::string &phone,
                        const std::string &passwordHash)
{
    const auto lockResult =
        client->execSqlSync("SELECT GET_LOCK(?, 10) AS acquired",
                            std::string(kBootstrapLock));
    if (lockResult.empty() || lockResult[0]["acquired"].isNull() ||
        lockResult[0]["acquired"].as<int>() != 1)
    {
        throw std::runtime_error("Could not acquire the initial-admin bootstrap lock.");
    }

    bool transactionStarted = false;
    try
    {
        const auto admins =
            client->execSqlSync("SELECT COUNT(*) AS admin_count FROM members "
                                "WHERE role = 'admin'");
        if (admins[0]["admin_count"].as<std::uint64_t>() != 0)
        {
            throw std::runtime_error(
                "An administrator already exists; bootstrap is disabled.");
        }

        const auto chamaId = drogon::utils::getUuid();
        const auto userId = drogon::utils::getUuid();
        const auto memberId = drogon::utils::getUuid();
        client->execSqlSync("START TRANSACTION");
        transactionStarted = true;
        client->execSqlSync("INSERT INTO chamas (id, name) VALUES (?, ?)",
                            chamaId,
                            chamaName);
        client->execSqlSync(
            "INSERT INTO users (id, phone_e164, password_hash, status) "
            "VALUES (?, ?, ?, 'active')",
            userId,
            phone,
            passwordHash);
        client->execSqlSync(
            "INSERT INTO members "
            "(id, chama_id, user_id, member_code, full_name, role, joined_on) "
            "VALUES (?, ?, ?, 'ADMIN-001', ?, 'admin', CURRENT_DATE)",
            memberId,
            chamaId,
            userId,
            fullName);
        client->execSqlSync(
            "INSERT INTO audit_events "
            "(chama_id, actor_member_id, event_type, resource_type, resource_id) "
            "VALUES (?, ?, 'initial_admin_bootstrapped', 'member', ?)",
            chamaId,
            memberId,
            memberId);
        client->execSqlSync("COMMIT");
        transactionStarted = false;
    }
    catch (const std::exception &)
    {
        if (transactionStarted)
        {
            try
            {
                client->execSqlSync("ROLLBACK");
            }
            catch (const std::exception &error)
            {
                std::cerr << "Bootstrap rollback failed: " << error.what() << '\n';
            }
        }
        try
        {
            releaseBootstrapLock(client);
        }
        catch (const std::exception &error)
        {
            std::cerr << "Could not release bootstrap lock: " << error.what() << '\n';
        }
        throw;
    }
    releaseBootstrapLock(client);
}
}  // namespace

int main()
{
    std::string password;
    try
    {
        std::string chamaName;
        std::string fullName;
        std::string phone;
        std::cout << "Chama name: " << std::flush;
        if (!std::getline(std::cin, chamaName) || !validName(chamaName))
        {
            std::cerr << "Chama name must contain 1-120 characters.\n";
            return EXIT_FAILURE;
        }
        std::cout << "Admin full name: " << std::flush;
        if (!std::getline(std::cin, fullName) || !validName(fullName))
        {
            std::cerr << "Admin name must contain 1-120 characters.\n";
            return EXIT_FAILURE;
        }
        std::cout << "Admin phone in E.164 format: " << std::flush;
        if (!std::getline(std::cin, phone) || !validPhone(phone))
        {
            std::cerr << "Phone must use E.164 format, for example +254712345678.\n";
            return EXIT_FAILURE;
        }

        password = readPassword();
        if (password.size() < 12 || password.size() > 128)
        {
            sodium_memzero(password.data(), password.size());
            std::cerr << "Password must contain 12-128 characters.\n";
            return EXIT_FAILURE;
        }
        if (sodium_init() < 0)
        {
            sodium_memzero(password.data(), password.size());
            std::cerr << "Could not initialize the password hashing library.\n";
            return EXIT_FAILURE;
        }

        std::array<char, crypto_pwhash_STRBYTES> passwordHash{};
        if (crypto_pwhash_str(passwordHash.data(),
                              password.data(),
                              password.size(),
                              crypto_pwhash_OPSLIMIT_INTERACTIVE,
                              crypto_pwhash_MEMLIMIT_INTERACTIVE) != 0)
        {
            sodium_memzero(password.data(), password.size());
            std::cerr << "Could not hash the admin password.\n";
            return EXIT_FAILURE;
        }
        sodium_memzero(password.data(), password.size());

        drogon::orm::MysqlConfig databaseConfig{};
        if (!jirani::readDatabaseConfig(databaseConfig))
        {
            return EXIT_FAILURE;
        }
        const auto client = drogon::orm::DbClient::newMysqlClient(
            jirani::databaseConnectionInfo(databaseConfig),
            databaseConfig.connectionNumber);
        createInitialAdmin(client,
                           chamaName,
                           fullName,
                           phone,
                           passwordHash.data());
        sodium_memzero(passwordHash.data(), passwordHash.size());
        std::cout << "Initial administrator created. Keep these database "
                     "credentials secure and remove bootstrap access.\n";
        return EXIT_SUCCESS;
    }
    catch (const std::exception &error)
    {
        if (!password.empty())
        {
            sodium_memzero(password.data(), password.size());
        }
        std::cerr << "Admin bootstrap failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
