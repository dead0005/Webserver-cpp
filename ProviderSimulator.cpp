#include "ProviderSimulator.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <random>
#include <string>

namespace
{
    std::string toLower(std::string s)
    {
        for (char& c : s)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }
}

std::string providerStatusToString(ProviderResultStatus s)
{
    switch (s)
    {
    case ProviderResultStatus::SUCCESS:       return "SUCCESS";
    case ProviderResultStatus::DECLINED:      return "DECLINED";
    case ProviderResultStatus::TIMEOUT:       return "TIMEOUT";
    case ProviderResultStatus::NETWORK_ERROR: return "NETWORK_ERROR";
    }
    return "UNKNOWN";
}

ProviderSimulator::ProviderSimulator()
{
}

std::string ProviderSimulator::generateAuthCode()
{
    static std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(100000, 999999);
    return "AUTH_" + std::to_string(dist(rng));
}

std::string ProviderSimulator::generateBankRef()
{
    static std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(10000000, 99999999);
    return "BNK_" + std::to_string(dist(rng));
}

ProviderResponse ProviderSimulator::authorize(int64_t amount,
                                             const std::string& /*currency*/,
                                             const std::string& mockAction)
{
    ProviderResponse resp{};
    const std::string action = toLower(mockAction);

    // Simulated deterministic scenarios via header "X-Mock-Action"
    if (action == "decline" || action == "insufficient_funds")
    {
        resp.status = ProviderResultStatus::DECLINED;
        resp.declineReason = "INSUFFICIENT_FUNDS";
        resp.latencyMs = 25;
        return resp;
    }

    if (action == "fraud")
    {
        resp.status = ProviderResultStatus::DECLINED;
        resp.declineReason = "SUSPECTED_FRAUD_DECLINE";
        resp.latencyMs = 40;
        return resp;
    }

    if (action == "timeout")
    {
        resp.status = ProviderResultStatus::TIMEOUT;
        resp.declineReason = "BANK_GATEWAY_TIMEOUT";
        resp.latencyMs = 5000;
        return resp;
    }

    if (action == "network_error")
    {
        resp.status = ProviderResultStatus::NETWORK_ERROR;
        resp.declineReason = "ACQUIRER_CONNECTION_RESET";
        resp.latencyMs = 10;
        return resp;
    }

    // Default business rule: payments over ₹10,00,000 (100000000 paise) hit bank limits
    if (amount > 100000000)
    {
        resp.status = ProviderResultStatus::DECLINED;
        resp.declineReason = "EXCEEDS_SINGLE_TRANSACTION_LIMIT";
        resp.latencyMs = 20;
        return resp;
    }

    // Clean approval
    resp.status            = ProviderResultStatus::SUCCESS;
    resp.authorizationCode = generateAuthCode();
    resp.providerRef       = generateBankRef();
    resp.latencyMs         = (action == "delay") ? 200 : 15;
    return resp;
}

ProviderResponse ProviderSimulator::capture(const std::string& /*authorizationCode*/, int64_t /*amount*/)
{
    ProviderResponse resp{};
    resp.status      = ProviderResultStatus::SUCCESS;
    resp.providerRef = generateBankRef();
    resp.latencyMs   = 15;
    return resp;
}

ProviderResponse ProviderSimulator::refund(const std::string& /*authorizationCode*/, int64_t /*amount*/)
{
    ProviderResponse resp{};
    resp.status      = ProviderResultStatus::SUCCESS;
    resp.providerRef = generateBankRef();
    resp.latencyMs   = 20;
    return resp;
}
