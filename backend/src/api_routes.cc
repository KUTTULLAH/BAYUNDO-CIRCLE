#include "api_routes.h"

#include <drogon/drogon.h>
#include <sodium.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <regex>
#include <string>
#include <string_view>
#include <utility>

namespace
{
using Callback =
    std::function<void(const drogon::HttpResponsePtr &)>;

struct Principal
{
    std::string memberId;
    std::string userId;
    std::string chamaId;
    std::string phone;
    std::string memberCode;
    std::string fullName;
    std::string role;
    std::string memberStatus;
    std::string accountStatus;
    std::string approvalStatus;
};

std::mutex databaseMutex;

bool isAllowedBrowserOrigin(const std::string &origin)
{
    static const std::regex localOrigin(
        R"(^https?://(localhost|127\.0\.0\.1)(:[0-9]{1,5})?$)");
    return std::regex_match(origin, localOrigin);
}

void registerLocalCors()
{
    drogon::app().registerPreRoutingAdvice(
        [](const drogon::HttpRequestPtr &request,
           drogon::AdviceCallback &&stop,
           drogon::AdviceChainCallback &&next) {
            if (request->method() == drogon::Options &&
                request->path().starts_with("/api/v1/"))
            {
                auto response = drogon::HttpResponse::newHttpResponse();
                response->setStatusCode(drogon::k204NoContent);
                response->addHeader("Access-Control-Allow-Methods",
                                     "GET, POST, OPTIONS");
                response->addHeader("Access-Control-Allow-Headers",
                                     "Authorization, Content-Type, "
                                     "X-Bootstrap-Token");
                response->addHeader("Access-Control-Max-Age", "600");
                stop(response);
                return;
            }
            next();
        });
    drogon::app().registerPreSendingAdvice(
        [](const drogon::HttpRequestPtr &request,
           const drogon::HttpResponsePtr &response) {
            const auto origin = request->getHeader("origin");
            if (!origin.empty() && isAllowedBrowserOrigin(origin))
            {
                response->addHeader("Access-Control-Allow-Origin", origin);
                response->addHeader("Vary", "Origin");
            }
        });
}

drogon::HttpResponsePtr jsonResponse(drogon::HttpStatusCode status,
                                     const Json::Value &body)
{
    auto response = drogon::HttpResponse::newHttpJsonResponse(body);
    response->setStatusCode(status);
    return response;
}

drogon::HttpResponsePtr errorResponse(drogon::HttpStatusCode status,
                                       const std::string &code,
                                       const std::string &message)
{
    Json::Value body;
    body["code"] = code;
    body["message"] = message;
    return jsonResponse(status, body);
}

bool validPhone(const std::string &phone)
{
    static const std::regex pattern(R"(^\+[1-9][0-9]{7,14}$)");
    return std::regex_match(phone, pattern);
}

bool validUuid(const std::string &value)
{
    static const std::regex pattern(
        R"(^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$)");
    return std::regex_match(value, pattern);
}

bool validName(const std::string &name)
{
    return !name.empty() && name.size() <= 120 &&
           std::any_of(name.begin(), name.end(), [](unsigned char ch) {
               return std::isspace(ch) == 0;
           });
}

std::string hashHex(const unsigned char *data, std::size_t size)
{
    std::array<unsigned char, crypto_hash_sha256_BYTES> digest{};
    crypto_hash_sha256(digest.data(), data, static_cast<unsigned long long>(size));
    std::array<char, crypto_hash_sha256_BYTES * 2 + 1> encoded{};
    sodium_bin2hex(encoded.data(), encoded.size(), digest.data(), digest.size());
    return std::string(encoded.data());
}

std::string hashToken(const std::string &token)
{
    return hashHex(reinterpret_cast<const unsigned char *>(token.data()),
                   token.size());
}

std::string newToken()
{
    constexpr std::size_t tokenBytes = 32;
    std::array<unsigned char, tokenBytes> bytes{};
    randombytes_buf(bytes.data(), bytes.size());
    std::array<char, sodium_base64_ENCODED_LEN(
                         tokenBytes, sodium_base64_VARIANT_URLSAFE_NO_PADDING)>
        encoded{};
    sodium_bin2base64(encoded.data(),
                      encoded.size(),
                      bytes.data(),
                      bytes.size(),
                      sodium_base64_VARIANT_URLSAFE_NO_PADDING);
    return std::string(encoded.data());
}

std::string bearerToken(const drogon::HttpRequestPtr &request)
{
    const auto header = request->getHeader("authorization");
    constexpr std::string_view prefix = "Bearer ";
    if (header.size() <= prefix.size() ||
        std::string_view(header).substr(0, prefix.size()) != prefix)
    {
        return {};
    }
    return header.substr(prefix.size());
}

bool authenticate(const drogon::HttpRequestPtr &request,
                  Principal &principal,
                  drogon::HttpResponsePtr &failure)
{
    const auto token = bearerToken(request);
    if (token.empty() || token.size() > 256)
    {
        failure = errorResponse(drogon::k401Unauthorized,
                                "unauthorized",
                                "A valid bearer token is required.");
        return false;
    }

    const auto tokenHash = hashToken(token);
    const auto rows = drogon::app().getDbClient()->execSqlSync(
        "SELECT m.id AS member_id, m.user_id, m.chama_id, m.member_code, "
        "m.full_name, m.role, m.status AS member_status, "
        "m.approval_status, u.phone_e164, u.status AS account_status "
        "FROM auth_sessions s "
        "JOIN users u ON u.id = s.user_id "
        "JOIN members m ON m.user_id = u.id "
        "WHERE s.token_hash = UNHEX(?) AND s.revoked_at IS NULL "
        "AND s.expires_at > UTC_TIMESTAMP() "
        "AND u.status = 'active' AND m.status = 'active' "
        "LIMIT 1",
        tokenHash);
    if (rows.empty())
    {
        failure = errorResponse(drogon::k401Unauthorized,
                                "unauthorized",
                                "The session is invalid or expired.");
        return false;
    }

    principal.memberId = rows[0]["member_id"].as<std::string>();
    principal.userId = rows[0]["user_id"].as<std::string>();
    principal.chamaId = rows[0]["chama_id"].as<std::string>();
    principal.memberCode = rows[0]["member_code"].as<std::string>();
    principal.fullName = rows[0]["full_name"].as<std::string>();
    principal.role = rows[0]["role"].as<std::string>();
    principal.memberStatus = rows[0]["member_status"].as<std::string>();
    principal.approvalStatus = rows[0]["approval_status"].as<std::string>();
    principal.phone = rows[0]["phone_e164"].as<std::string>();
    principal.accountStatus = rows[0]["account_status"].as<std::string>();
    return true;
}

Json::Value memberJson(const Principal &principal)
{
    Json::Value member;
    member["id"] = principal.memberId;
    member["memberCode"] = principal.memberCode;
    member["fullName"] = principal.fullName;
    member["phone"] = principal.phone;
    member["role"] = principal.role;
    member["status"] = principal.memberStatus;
    member["accountStatus"] = principal.accountStatus;
    return member;
}

std::string expiryTimestamp()
{
    const auto expiry = std::chrono::system_clock::now() +
                        std::chrono::hours(24);
    const auto time = std::chrono::system_clock::to_time_t(expiry);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &time);
#else
    gmtime_r(&time, &utc);
#endif
    std::array<char, 32> buffer{};
    std::strftime(buffer.data(), buffer.size(), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return buffer.data();
}

bool validPassword(const std::string &password)
{
    return password.size() >= 12 && password.size() <= 128;
}

bool makePasswordHash(const std::string &password, std::string &passwordHash)
{
    std::array<char, crypto_pwhash_STRBYTES> hash{};
    if (crypto_pwhash_str(hash.data(),
                          password.c_str(),
                          password.size(),
                          crypto_pwhash_OPSLIMIT_INTERACTIVE,
                          crypto_pwhash_MEMLIMIT_INTERACTIVE) != 0)
    {
        return false;
    }
    passwordHash = hash.data();
    sodium_memzero(hash.data(), hash.size());
    return true;
}

bool validMoney(const Json::Value &value, std::uint64_t &minorUnits)
{
    if (!value.isObject() || !value["currency"].isString() ||
        value["currency"].asString() != "KES" || !value["amount"].isString())
    {
        return false;
    }
    const auto amount = value["amount"].asString();
    static const std::regex pattern(R"(^(0|[1-9][0-9]*)(\.[0-9]{1,2})?$)");
    if (!std::regex_match(amount, pattern))
    {
        return false;
    }

    const auto decimal = amount.find('.');
    const auto wholePart = std::string_view(amount).substr(0, decimal);
    std::uint64_t whole = 0;
    const auto [wholeEnd, wholeError] =
        std::from_chars(wholePart.data(),
                        wholePart.data() + wholePart.size(),
                        whole);
    if (wholeError != std::errc{} ||
        wholeEnd != wholePart.data() + wholePart.size() ||
        whole > (std::numeric_limits<std::uint64_t>::max() - 99) / 100)
    {
        return false;
    }

    unsigned int cents = 0;
    if (decimal != std::string::npos)
    {
        auto fraction = std::string_view(amount).substr(decimal + 1);
        if (fraction.size() == 1)
        {
            cents = static_cast<unsigned int>(fraction[0] - '0') * 10;
        }
        else
        {
            cents = static_cast<unsigned int>(fraction[0] - '0') * 10 +
                    static_cast<unsigned int>(fraction[1] - '0');
        }
    }
    minorUnits = whole * 100 + cents;
    return minorUnits > 0;
}

std::string moneyString(std::uint64_t minorUnits)
{
    return std::to_string(minorUnits / 100) + "." +
           (minorUnits % 100 < 10 ? "0" : "") +
           std::to_string(minorUnits % 100);
}

bool validEffectiveDate(const std::string &date)
{
    static const std::regex pattern(R"(^[0-9]{4}-(0[1-9]|1[0-2])-01$)");
    if (!std::regex_match(date, pattern))
    {
        return false;
    }
    const auto year = std::stoi(date.substr(0, 4));
    const auto month = std::stoi(date.substr(5, 2));
    return std::chrono::year_month_day{
               std::chrono::year{year},
               std::chrono::month{static_cast<unsigned int>(month)},
               std::chrono::day{1}}
        .ok();
}

Json::Value moneyJson(const std::string &amount)
{
    Json::Value money;
    money["amount"] = amount;
    money["currency"] = "KES";
    return money;
}

void startTransaction(const drogon::orm::DbClientPtr &client)
{
    client->execSqlSync("START TRANSACTION");
}

void commitTransaction(const drogon::orm::DbClientPtr &client)
{
    client->execSqlSync("COMMIT");
}

void rollbackTransaction(const drogon::orm::DbClientPtr &client)
{
    try
    {
        client->execSqlSync("ROLLBACK");
    }
    catch (const std::exception &)
    {
        LOG_ERROR << "Database transaction rollback failed.";
    }
}

void registerLogin()
{
    drogon::app().registerHandler(
        "/api/v1/auth/login",
        [](const drogon::HttpRequestPtr &request, Callback &&callback) {
            auto responseCallback =
                std::make_shared<Callback>(std::move(callback));
            const auto body = request->getJsonObject();
            if (!body || !(*body)["phone"].isString() ||
                !(*body)["password"].isString())
            {
                (*responseCallback)(errorResponse(
                    drogon::k400BadRequest,
                    "bad_request",
                    "A phone number and password are required."));
                return;
            }
            const auto phone = (*body)["phone"].asString();
            auto password = (*body)["password"].asString();
            if (!validPhone(phone) || !validPassword(password))
            {
                if (!password.empty())
                {
                    sodium_memzero(password.data(), password.size());
                }
                (*responseCallback)(errorResponse(
                    drogon::k400BadRequest,
                    "invalid_request",
                    "The phone number or password is invalid."));
                return;
            }

            try
            {
                std::lock_guard<std::mutex> lock(databaseMutex);
                const auto rows = drogon::app().getDbClient()->execSqlSync(
                    "SELECT u.id AS user_id, u.password_hash, "
                    "m.id AS member_id, m.chama_id, m.member_code, "
                    "m.full_name, m.role, m.status AS member_status, "
                    "m.approval_status "
                    "FROM users u JOIN members m ON m.user_id = u.id "
                    "WHERE u.phone_e164 = ? AND u.status = 'active' "
                    "AND m.status = 'active' AND m.approval_status "
                    "IN ('not_required', 'approved') LIMIT 1",
                    phone);
                bool validCredentials = false;
                if (!rows.empty() && !rows[0]["password_hash"].isNull())
                {
                    const auto passwordHash =
                        rows[0]["password_hash"].as<std::string>();
                    validCredentials =
                        crypto_pwhash_str_verify(passwordHash.c_str(),
                                                 password.c_str(),
                                                 password.size()) == 0;
                }
                sodium_memzero(password.data(), password.size());
                (*body)["password"] = "";
                if (!validCredentials)
                {
                    (*responseCallback)(errorResponse(
                        drogon::k401Unauthorized,
                        "unauthorized",
                        "Phone number or password is incorrect."));
                    return;
                }

                Principal principal;
                principal.memberId = rows[0]["member_id"].as<std::string>();
                principal.userId = rows[0]["user_id"].as<std::string>();
                principal.chamaId = rows[0]["chama_id"].as<std::string>();
                principal.phone = phone;
                principal.memberCode = rows[0]["member_code"].as<std::string>();
                principal.fullName = rows[0]["full_name"].as<std::string>();
                principal.role = rows[0]["role"].as<std::string>();
                principal.memberStatus =
                    rows[0]["member_status"].as<std::string>();
                principal.accountStatus = "active";
                principal.approvalStatus =
                    rows[0]["approval_status"].as<std::string>();

                const auto token = newToken();
                const auto tokenHash = hashToken(token);
                drogon::app().getDbClient()->execSqlSync(
                    "INSERT INTO auth_sessions "
                    "(id, user_id, token_hash, expires_at) "
                    "VALUES (?, ?, UNHEX(?), DATE_ADD(UTC_TIMESTAMP(), "
                    "INTERVAL 24 HOUR))",
                    drogon::utils::getUuid(),
                    principal.userId,
                    tokenHash);

                Json::Value response;
                response["accessToken"] = token;
                response["tokenType"] = "Bearer";
                response["expiresAt"] = expiryTimestamp();
                response["member"] = memberJson(principal);
                (*responseCallback)(
                    jsonResponse(drogon::k200OK, response));
            }
            catch (const std::exception &)
            {
                LOG_ERROR << "Login database operation failed.";
                (*responseCallback)(errorResponse(
                    drogon::k500InternalServerError,
                    "internal_error",
                    "The request could not be completed."));
            }
        },
        {drogon::Post});
}

void registerBootstrapAdmin()
{
    const char *configuredToken = std::getenv("BOOTSTRAP_TOKEN");
    const std::string expectedToken =
        configuredToken == nullptr ? std::string{} : configuredToken;
    const bool tokenConfigured = !expectedToken.empty();
    const bool tokenIsStrongEnough = expectedToken.size() >= 32;
    if (tokenConfigured && !tokenIsStrongEnough)
    {
        LOG_ERROR << "BOOTSTRAP_TOKEN must contain at least 32 characters; "
                     "the bootstrap endpoint is disabled.";
    }

    drogon::app().registerHandler(
        "/api/v1/admin/bootstrap",
        [expectedToken, tokenConfigured, tokenIsStrongEnough](
            const drogon::HttpRequestPtr &request, Callback &&callback) {
            auto responseCallback =
                std::make_shared<Callback>(std::move(callback));
            if (!tokenConfigured)
            {
                (*responseCallback)(errorResponse(
                    drogon::k404NotFound,
                    "not_found",
                    "The bootstrap endpoint is not enabled."));
                return;
            }
            if (!tokenIsStrongEnough)
            {
                (*responseCallback)(errorResponse(
                    drogon::k503ServiceUnavailable,
                    "bootstrap_unavailable",
                    "The bootstrap endpoint is not securely configured."));
                return;
            }

            const auto suppliedToken = request->getHeader("x-bootstrap-token");
            if (suppliedToken.size() != expectedToken.size() ||
                sodium_memcmp(suppliedToken.data(),
                              expectedToken.data(),
                              expectedToken.size()) != 0)
            {
                (*responseCallback)(errorResponse(
                    drogon::k401Unauthorized,
                    "unauthorized",
                    "A valid bootstrap token is required."));
                return;
            }

            const auto body = request->getJsonObject();
            if (!body || !(*body)["chamaName"].isString() ||
                !(*body)["fullName"].isString() ||
                !(*body)["phone"].isString() ||
                !(*body)["password"].isString())
            {
                (*responseCallback)(errorResponse(
                    drogon::k400BadRequest,
                    "bad_request",
                    "Chama name, admin name, phone, and password are required."));
                return;
            }

            const auto chamaName = (*body)["chamaName"].asString();
            const auto fullName = (*body)["fullName"].asString();
            const auto phone = (*body)["phone"].asString();
            auto password = (*body)["password"].asString();
            auto clearPassword = [&]() {
                if (!password.empty())
                {
                    sodium_memzero(password.data(), password.size());
                }
                (*body)["password"] = "";
            };
            if (!validName(chamaName) || chamaName.size() > 120 ||
                !validName(fullName) || fullName.size() > 120 ||
                !validPhone(phone) || !validPassword(password))
            {
                clearPassword();
                (*responseCallback)(errorResponse(
                    drogon::k422UnprocessableEntity,
                    "validation_error",
                    "The chama, administrator, phone, or password is invalid."));
                return;
            }

            std::string passwordHash;
            if (!makePasswordHash(password, passwordHash))
            {
                clearPassword();
                (*responseCallback)(errorResponse(
                    drogon::k500InternalServerError,
                    "internal_error",
                    "The request could not be completed."));
                return;
            }
            clearPassword();

            const auto client = drogon::app().getDbClient();
            bool lockAcquired = false;
            bool transactionStarted = false;
            try
            {
                std::lock_guard<std::mutex> lock(databaseMutex);
                const auto lockResult = client->execSqlSync(
                    "SELECT GET_LOCK(?, 10) AS acquired",
                    std::string("jirani_circle_initial_admin"));
                if (lockResult.empty() || lockResult[0]["acquired"].isNull() ||
                    lockResult[0]["acquired"].as<int>() != 1)
                {
                    (*responseCallback)(errorResponse(
                        drogon::k503ServiceUnavailable,
                        "bootstrap_busy",
                        "The bootstrap operation is currently unavailable."));
                    sodium_memzero(passwordHash.data(), passwordHash.size());
                    return;
                }
                lockAcquired = true;

                const auto admins = client->execSqlSync(
                    "SELECT COUNT(*) AS admin_count FROM members "
                    "WHERE role = 'admin'");
                if (admins[0]["admin_count"].as<std::uint64_t>() != 0)
                {
                    client->execSqlSync(
                        "SELECT RELEASE_LOCK(?)",
                        std::string("jirani_circle_initial_admin"));
                    lockAcquired = false;
                    sodium_memzero(passwordHash.data(), passwordHash.size());
                    (*responseCallback)(errorResponse(
                        drogon::k409Conflict,
                        "bootstrap_closed",
                        "An administrator already exists; bootstrap is disabled."));
                    return;
                }

                const auto chamaId = drogon::utils::getUuid();
                const auto userId = drogon::utils::getUuid();
                const auto memberId = drogon::utils::getUuid();
                startTransaction(client);
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
                commitTransaction(client);
                transactionStarted = false;
                client->execSqlSync(
                    "SELECT RELEASE_LOCK(?)",
                    std::string("jirani_circle_initial_admin"));
                lockAcquired = false;
                sodium_memzero(passwordHash.data(), passwordHash.size());

                Json::Value response;
                response["chama"] = Json::Value(Json::objectValue);
                response["chama"]["id"] = chamaId;
                response["chama"]["name"] = chamaName;
                response["admin"] = Json::Value(Json::objectValue);
                response["admin"]["id"] = memberId;
                response["admin"]["fullName"] = fullName;
                response["admin"]["phone"] = phone;
                response["admin"]["role"] = "admin";
                (*responseCallback)(jsonResponse(drogon::k201Created, response));
            }
            catch (const std::exception &)
            {
                if (transactionStarted)
                {
                    rollbackTransaction(client);
                }
                if (lockAcquired)
                {
                    try
                    {
                        client->execSqlSync(
                            "SELECT RELEASE_LOCK(?)",
                            std::string("jirani_circle_initial_admin"));
                    }
                    catch (const std::exception &)
                    {
                        LOG_ERROR << "Could not release initial-admin bootstrap lock.";
                    }
                }
                sodium_memzero(passwordHash.data(), passwordHash.size());
                LOG_ERROR << "Initial administrator bootstrap failed.";
                (*responseCallback)(errorResponse(
                    drogon::k500InternalServerError,
                    "internal_error",
                    "The request could not be completed."));
            }
        },
        {drogon::Post});
}

void registerLogout()
{
    drogon::app().registerHandler(
        "/api/v1/auth/logout",
        [](const drogon::HttpRequestPtr &request, Callback &&callback) {
            auto responseCallback =
                std::make_shared<Callback>(std::move(callback));
            const auto token = bearerToken(request);
            if (token.empty() || token.size() > 256)
            {
                (*responseCallback)(errorResponse(
                    drogon::k401Unauthorized,
                    "unauthorized",
                    "A valid bearer token is required."));
                return;
            }

            try
            {
                std::lock_guard<std::mutex> lock(databaseMutex);
                const auto tokenHash = hashToken(token);
                const auto result = drogon::app().getDbClient()->execSqlSync(
                    "UPDATE auth_sessions SET revoked_at = UTC_TIMESTAMP() "
                    "WHERE token_hash = UNHEX(?) AND revoked_at IS NULL "
                    "AND expires_at > UTC_TIMESTAMP()",
                    tokenHash);
                if (result.affectedRows() != 1)
                {
                    (*responseCallback)(errorResponse(
                        drogon::k401Unauthorized,
                        "unauthorized",
                        "The session is invalid or expired."));
                    return;
                }

                auto response = drogon::HttpResponse::newHttpResponse();
                response->setStatusCode(drogon::k204NoContent);
                (*responseCallback)(response);
            }
            catch (const std::exception &)
            {
                LOG_ERROR << "Logout database operation failed.";
                (*responseCallback)(errorResponse(
                    drogon::k500InternalServerError,
                    "internal_error",
                    "The request could not be completed."));
            }
        },
        {drogon::Post});
}

void registerAcceptInvite()
{
    drogon::app().registerHandler(
        "/api/v1/auth/accept-invite",
        [](const drogon::HttpRequestPtr &request, Callback &&callback) {
            auto responseCallback =
                std::make_shared<Callback>(std::move(callback));
            const auto body = request->getJsonObject();
            if (!body || !(*body)["token"].isString() ||
                !(*body)["password"].isString())
            {
                (*responseCallback)(errorResponse(
                    drogon::k400BadRequest,
                    "bad_request",
                    "An invitation token and password are required."));
                return;
            }
            const auto token = (*body)["token"].asString();
            auto password = (*body)["password"].asString();
            if (token.size() < 32 || token.size() > 256 ||
                !validPassword(password))
            {
                (*responseCallback)(errorResponse(
                    drogon::k400BadRequest,
                    "invalid_request",
                    "The invitation token or password is invalid."));
                return;
            }

            std::string passwordHash;
            if (!makePasswordHash(password, passwordHash))
            {
                (*responseCallback)(errorResponse(
                    drogon::k500InternalServerError,
                    "internal_error",
                    "The request could not be completed."));
                return;
            }
            sodium_memzero(password.data(), password.size());
            (*body)["password"] = "";
            const auto tokenHash = hashToken(token);
            try
            {
                std::lock_guard<std::mutex> lock(databaseMutex);
                const auto client = drogon::app().getDbClient();
                startTransaction(client);
                try
                {
                    const auto rows = client->execSqlSync(
                        "SELECT i.id, i.member_id, i.consumed_at, i.revoked_at, "
                        "i.expires_at, m.user_id, m.approval_status, m.status "
                        "FROM member_invitations i "
                        "JOIN members m ON m.id = i.member_id "
                        "AND m.chama_id = i.chama_id "
                        "WHERE i.token_hash = UNHEX(?) FOR UPDATE",
                        tokenHash);
                    if (rows.empty())
                    {
                        rollbackTransaction(client);
                        (*responseCallback)(errorResponse(
                            drogon::k400BadRequest,
                            "invalid_invitation",
                            "The invitation is invalid."));
                        return;
                    }
                    if (!rows[0]["consumed_at"].isNull() ||
                        !rows[0]["revoked_at"].isNull() ||
                        rows[0]["status"].as<std::string>() != "active" ||
                        (rows[0]["approval_status"].as<std::string>() !=
                             "approved" &&
                         rows[0]["approval_status"].as<std::string>() !=
                             "not_required"))
                    {
                        rollbackTransaction(client);
                        (*responseCallback)(errorResponse(
                            drogon::k409Conflict,
                            "invitation_unavailable",
                            "The invitation is no longer available."));
                        return;
                    }
                    const auto expiry = client->execSqlSync(
                        "SELECT expires_at <= UTC_TIMESTAMP() AS expired "
                        "FROM member_invitations WHERE id = ?",
                        rows[0]["id"].as<std::string>());
                    if (expiry.empty() ||
                        expiry[0]["expired"].as<int>() != 0)
                    {
                        rollbackTransaction(client);
                        (*responseCallback)(errorResponse(
                            drogon::k410Gone,
                            "invitation_expired",
                            "The invitation has expired."));
                        return;
                    }

                    const auto userId = rows[0]["user_id"].as<std::string>();
                    const auto updated = client->execSqlSync(
                        "UPDATE users SET password_hash = ?, status = 'active' "
                        "WHERE id = ? AND status = 'invited' "
                        "AND password_hash IS NULL",
                        passwordHash,
                        userId);
                    if (updated.affectedRows() != 1)
                    {
                        rollbackTransaction(client);
                        (*responseCallback)(errorResponse(
                            drogon::k409Conflict,
                            "account_unavailable",
                            "The member account cannot be activated."));
                        return;
                    }
                    client->execSqlSync(
                        "UPDATE member_invitations SET consumed_at = "
                        "UTC_TIMESTAMP() WHERE id = ? AND consumed_at IS NULL "
                        "AND revoked_at IS NULL",
                        rows[0]["id"].as<std::string>());
                    commitTransaction(client);
                    auto response =
                        drogon::HttpResponse::newHttpResponse();
                    response->setStatusCode(drogon::k204NoContent);
                    (*responseCallback)(response);
                    sodium_memzero(passwordHash.data(), passwordHash.size());
                }
                catch (const std::exception &)
                {
                    rollbackTransaction(client);
                    throw;
                }
            }
            catch (const std::exception &)
            {
                sodium_memzero(passwordHash.data(), passwordHash.size());
                LOG_ERROR << "Invitation activation database operation failed.";
                (*responseCallback)(errorResponse(
                    drogon::k500InternalServerError,
                    "internal_error",
                    "The request could not be completed."));
            }
        },
        {drogon::Post});
}

void registerListMembers()
{
    drogon::app().registerHandler(
        "/api/v1/admin/members",
        [](const drogon::HttpRequestPtr &request, Callback &&callback) {
            auto responseCallback =
                std::make_shared<Callback>(std::move(callback));
            const auto status = request->getParameter("status");
            const auto search = request->getParameter("search");
            const auto cursor = request->getParameter("cursor");
            const bool validStatus =
                status.empty() || status == "active" || status == "inactive";
            if (!validStatus || search.size() > 120 ||
                (!cursor.empty() && !validUuid(cursor)))
            {
                (*responseCallback)(errorResponse(
                    drogon::k422UnprocessableEntity,
                    "validation_error",
                    "One or more member-list filters are invalid."));
                return;
            }

            try
            {
                std::lock_guard<std::mutex> lock(databaseMutex);
                Principal principal;
                drogon::HttpResponsePtr failure;
                if (!authenticate(request, principal, failure))
                {
                    (*responseCallback)(failure);
                    return;
                }
                if (principal.role != "admin")
                {
                    (*responseCallback)(errorResponse(
                        drogon::k403Forbidden,
                        "forbidden",
                        "Administrator access is required."));
                    return;
                }

                const auto rows = drogon::app().getDbClient()->execSqlSync(
                    "SELECT m.id, m.member_code, m.full_name, u.phone_e164, "
                    "m.role, m.status AS member_status, m.approval_status, "
                    "u.status AS account_status, "
                    "COALESCE(t.amount_minor, 0) AS target_minor, "
                    "COALESCE((SELECT SUM(ft.amount_minor) "
                    "FROM financial_transactions ft "
                    "WHERE ft.member_id = m.id AND ft.chama_id = m.chama_id "
                    "AND ft.kind = 'contribution' AND ft.status = 'posted' "
                    "AND ft.occurred_at >= DATE_FORMAT(CURRENT_DATE, '%Y-%m-01') "
                    "AND ft.occurred_at < DATE_ADD("
                    "DATE_FORMAT(CURRENT_DATE, '%Y-%m-01'), INTERVAL 1 MONTH)), "
                    "0) AS contributed_minor "
                    "FROM members m JOIN users u ON u.id = m.user_id "
                    "LEFT JOIN member_monthly_targets t "
                    "ON t.member_id = m.id AND t.chama_id = m.chama_id "
                    "AND t.effective_from = (SELECT MAX(t2.effective_from) "
                    "FROM member_monthly_targets t2 "
                    "WHERE t2.member_id = m.id AND t2.chama_id = m.chama_id "
                    "AND t2.effective_from <= DATE_SUB(CURRENT_DATE, "
                    "INTERVAL (DAYOFMONTH(CURRENT_DATE) - 1) DAY)) "
                    "WHERE m.chama_id = ? "
                    "AND (? = '' OR m.status = ?) "
                    "AND (? = '' OR m.full_name LIKE CONCAT('%', ?, '%') "
                    "OR u.phone_e164 LIKE CONCAT('%', ?, '%') "
                    "OR m.member_code LIKE CONCAT('%', ?, '%')) "
                    "AND (? = '' OR m.id > ?) "
                    "ORDER BY m.id LIMIT 101",
                    principal.chamaId,
                    status,
                    status,
                    search,
                    search,
                    search,
                    search,
                    cursor,
                    cursor);

                Json::Value items(Json::arrayValue);
                const auto resultCount = std::min<std::size_t>(rows.size(), 100);
                for (std::size_t index = 0; index < resultCount; ++index)
                {
                    const auto &row = rows[index];
                    Principal member;
                    member.memberId = row["id"].as<std::string>();
                    member.memberCode = row["member_code"].as<std::string>();
                    member.fullName = row["full_name"].as<std::string>();
                    member.phone = row["phone_e164"].as<std::string>();
                    member.role = row["role"].as<std::string>();
                    member.memberStatus =
                        row["member_status"].as<std::string>();
                    member.accountStatus =
                        row["account_status"].as<std::string>();
                    member.approvalStatus =
                        row["approval_status"].as<std::string>();

                    const auto targetMinor =
                        row["target_minor"].as<std::uint64_t>();
                    const auto contributedMinor =
                        row["contributed_minor"].as<std::uint64_t>();
                    const auto outstandingMinor =
                        targetMinor > contributedMinor
                            ? targetMinor - contributedMinor
                            : 0;
                    auto item = memberJson(member);
                    item["monthlyTarget"] =
                        moneyJson(moneyString(targetMinor));
                    item["contributedThisMonth"] =
                        moneyJson(moneyString(contributedMinor));
                    item["outstandingThisMonth"] =
                        moneyJson(moneyString(outstandingMinor));
                    if (member.memberStatus != "active" ||
                        (member.approvalStatus != "approved" &&
                         member.approvalStatus != "not_required"))
                    {
                        item["paymentStatus"] = "inactive";
                    }
                    else if (targetMinor > 0 &&
                             contributedMinor >= targetMinor)
                    {
                        item["paymentStatus"] = "paid";
                    }
                    else if (contributedMinor > 0)
                    {
                        item["paymentStatus"] = "partial";
                    }
                    else
                    {
                        item["paymentStatus"] = "unpaid";
                    }
                    items.append(std::move(item));
                }

                Json::Value response;
                response["items"] = std::move(items);
                response["nextCursor"] =
                    rows.size() > resultCount
                        ? Json::Value(rows[resultCount - 1]["id"].as<std::string>())
                        : Json::Value(Json::nullValue);
                (*responseCallback)(jsonResponse(drogon::k200OK, response));
            }
            catch (const std::exception &)
            {
                LOG_ERROR << "Member list database operation failed.";
                (*responseCallback)(errorResponse(
                    drogon::k500InternalServerError,
                    "internal_error",
                    "The request could not be completed."));
            }
        },
        {drogon::Get});
}

void registerCreateMember()
{
    drogon::app().registerHandler(
        "/api/v1/admin/members",
        [](const drogon::HttpRequestPtr &request, Callback &&callback) {
            auto responseCallback =
                std::make_shared<Callback>(std::move(callback));
            const auto body = request->getJsonObject();
            if (!body || !(*body)["fullName"].isString() ||
                !(*body)["phone"].isString() ||
                !(*body)["monthlyTarget"].isObject())
            {
                (*responseCallback)(errorResponse(
                    drogon::k400BadRequest,
                    "bad_request",
                    "Full name, phone, and monthly target are required."));
                return;
            }
            const auto fullName = (*body)["fullName"].asString();
            const auto phone = (*body)["phone"].asString();
            std::uint64_t targetMinor = 0;
            if (!validName(fullName) || !validPhone(phone) ||
                !validMoney((*body)["monthlyTarget"], targetMinor))
            {
                (*responseCallback)(errorResponse(
                    drogon::k422UnprocessableEntity,
                    "validation_error",
                    "One or more member fields are invalid."));
                return;
            }

            auto role = std::string("member");
            if ((*body).isMember("role"))
            {
                if (!(*body)["role"].isString() ||
                    ((*body)["role"].asString() != "member" &&
                     (*body)["role"].asString() != "admin"))
                {
                    (*responseCallback)(errorResponse(
                        drogon::k422UnprocessableEntity,
                        "validation_error",
                        "Role must be admin or member."));
                    return;
                }
                role = (*body)["role"].asString();
            }
            std::string effectiveDate;
            const bool hasEffectiveDate =
                (*body).isMember("targetEffectiveFrom");
            if (hasEffectiveDate)
            {
                if (!(*body)["targetEffectiveFrom"].isString())
                {
                    (*responseCallback)(errorResponse(
                        drogon::k422UnprocessableEntity,
                        "validation_error",
                        "The target effective date must be the first day of a month."));
                    return;
                }
                effectiveDate = (*body)["targetEffectiveFrom"].asString();
                if (!validEffectiveDate(effectiveDate))
                {
                    (*responseCallback)(errorResponse(
                        drogon::k422UnprocessableEntity,
                        "validation_error",
                        "The target effective date must be the first day of a month."));
                    return;
                }
            }

            try
            {
                std::lock_guard<std::mutex> lock(databaseMutex);
                const auto client = drogon::app().getDbClient();
                Principal principal;
                drogon::HttpResponsePtr failure;
                if (!authenticate(request, principal, failure))
                {
                    (*responseCallback)(failure);
                    return;
                }
                if (principal.role != "admin")
                {
                    (*responseCallback)(errorResponse(
                        drogon::k403Forbidden,
                        "forbidden",
                        "Administrator access is required."));
                    return;
                }
                const auto duplicate = client->execSqlSync(
                    "SELECT id FROM users WHERE phone_e164 = ? LIMIT 1",
                    phone);
                if (!duplicate.empty())
                {
                    (*responseCallback)(errorResponse(
                        drogon::k409Conflict,
                        "phone_in_use",
                        "A member with this phone number already exists."));
                    return;
                }

                const auto userId = drogon::utils::getUuid();
                const auto memberId = drogon::utils::getUuid();
                const auto userCode =
                    "M" + memberId.substr(0, 8);
                const auto invitationToken = newToken();
                startTransaction(client);
                try
                {
                    client->execSqlSync(
                        "INSERT INTO users (id, phone_e164, status) "
                        "VALUES (?, ?, 'invited')",
                        userId,
                        phone);
                    client->execSqlSync(
                        "INSERT INTO members "
                        "(id, chama_id, user_id, member_code, full_name, role, "
                        "status, approval_status, created_by_member_id, "
                        "joined_on) VALUES (?, ?, ?, ?, ?, ?, 'active', "
                        "'not_required', ?, "
                        "CURRENT_DATE)",
                        memberId,
                        principal.chamaId,
                        userId,
                        userCode,
                        fullName,
                        role,
                        principal.memberId);

                    client->execSqlSync(
                        "INSERT INTO member_invitations "
                        "(id, chama_id, member_id, token_hash, expires_at, "
                        "created_by_member_id) "
                        "VALUES (?, ?, ?, UNHEX(?), "
                        "DATE_ADD(UTC_TIMESTAMP(), INTERVAL 24 HOUR), ?)",
                        drogon::utils::getUuid(),
                        principal.chamaId,
                        memberId,
                        hashToken(invitationToken),
                        principal.memberId);

                    if (hasEffectiveDate)
                    {
                        client->execSqlSync(
                            "INSERT INTO member_monthly_targets "
                            "(id, chama_id, member_id, effective_from, "
                            "amount_minor, set_by_member_id) "
                            "VALUES (?, ?, ?, ?, ?, ?)",
                            drogon::utils::getUuid(),
                            principal.chamaId,
                            memberId,
                            effectiveDate,
                            targetMinor,
                            principal.memberId);
                    }
                    else
                    {
                        client->execSqlSync(
                            "INSERT INTO member_monthly_targets "
                            "(id, chama_id, member_id, effective_from, "
                            "amount_minor, set_by_member_id) "
                            "VALUES (?, ?, ?, DATE_SUB(CURRENT_DATE, "
                            "INTERVAL (DAYOFMONTH(CURRENT_DATE) - 1) DAY), "
                            "?, ?)",
                            drogon::utils::getUuid(),
                            principal.chamaId,
                            memberId,
                            targetMinor,
                            principal.memberId);
                    }
                    client->execSqlSync(
                        "INSERT INTO audit_events "
                        "(chama_id, actor_member_id, event_type, "
                        "resource_type, resource_id) "
                        "VALUES (?, ?, 'member_created', 'member', ?)",
                        principal.chamaId,
                        principal.memberId,
                        memberId);
                    commitTransaction(client);
                }
                catch (const std::exception &)
                {
                    rollbackTransaction(client);
                    throw;
                }

                Principal created;
                created.memberId = memberId;
                created.memberCode = userCode;
                created.fullName = fullName;
                created.phone = phone;
                created.role = role;
                created.memberStatus = "active";
                created.accountStatus = "invited";
                Json::Value createdMember = memberJson(created);
                createdMember["monthlyTarget"] =
                    moneyJson(moneyString(targetMinor));
                createdMember["contributedThisMonth"] = moneyJson("0.00");
                createdMember["outstandingThisMonth"] =
                    moneyJson(moneyString(targetMinor));
                createdMember["paymentStatus"] = "unpaid";
                Json::Value result;
                result["member"] = std::move(createdMember);
                result["invitationToken"] = invitationToken;
                (*responseCallback)(jsonResponse(drogon::k202Accepted, result));
            }
            catch (const std::exception &)
            {
                LOG_ERROR << "Member creation database operation failed.";
                (*responseCallback)(errorResponse(
                    drogon::k500InternalServerError,
                    "internal_error",
                    "The request could not be completed."));
            }
        },
        {drogon::Post});
}

}  // namespace

namespace jirani
{
void registerApiRoutes()
{
    registerLocalCors();
    registerBootstrapAdmin();
    registerLogin();
    registerLogout();
    registerAcceptInvite();
    registerListMembers();
    registerCreateMember();
}
}  // namespace jirani
