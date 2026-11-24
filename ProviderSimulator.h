#pragma once

#include <cstdint>
#include <string>

// Status of the simulated banking / acquiring network response
enum class ProviderResultStatus
{
    SUCCESS,
    DECLINED,
    TIMEOUT,
    NETWORK_ERROR
};

std::string providerStatusToString(ProviderResultStatus s);

// Response returned by the simulated card network / bank
struct ProviderResponse
{
    ProviderResultStatus status;
    std::string          authorizationCode; // e.g. "AUTH_84920"
    std::string          declineReason;     // e.g. "INSUFFICIENT_FUNDS", "CARD_EXPIRED"
    std::string          providerRef;       // e.g. "BNK_REF_938102"
    int                  latencyMs;         // simulated network roundtrip latency in ms
};

// Payment Provider Simulator
// Simulates an upstream banking network (like Visa/Mastercard/Razorpay gateway)
// Supports deterministic testing via the "X-Mock-Action" request header or randomized behavior.
class ProviderSimulator
{
public:
    ProviderSimulator();

    // Execute simulated authorization.
    // mockAction values (case-insensitive):
    // - "decline" / "insufficient_funds" / "fraud" -> forces decline
    // - "timeout" -> simulated upstream network timeout
    // - "network_error" -> gateway 502/503 dropped connection
    // - "delay" -> simulates a 500ms bank delay before success
    // - default / empty -> clean instant approval
    ProviderResponse authorize(int64_t amount, const std::string& currency,
                               const std::string& mockAction = "");

    // Execute simulated capture with the bank
    ProviderResponse capture(const std::string& authorizationCode, int64_t amount);

    // Execute simulated refund with the bank
    ProviderResponse refund(const std::string& authorizationCode, int64_t amount);

private:
    static std::string generateAuthCode();
    static std::string generateBankRef();
};
