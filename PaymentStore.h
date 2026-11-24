#pragma once

#include "Payment.h"

#include <string>
#include <unordered_map>
#include <vector>
#include <windows.h>   // CRITICAL_SECTION — GCC 6.3 win32 thread model breaks <mutex>

// Thread-safe in-memory payment store.
// Uses Win32 CRITICAL_SECTION instead of std::mutex for GCC 6.3 compatibility.
class PaymentStore
{
public:
    struct CreateRequest
    {
        std::string idempotencyKey;
        int64_t     amount;
        std::string currency;
        std::string description;
    };

    enum class CreateResult
    {
        Created,         // new payment created
        Duplicate,       // same key, same payload -> return cached payment (idempotent)
        KeyConflict      // same key, different payload -> reject with 409
    };

    PaymentStore();
    ~PaymentStore();

    // Create a new payment. Returns the result code and the payment record.
    // Uses pair<> instead of structured bindings (GCC 6.3 C++17 partial support).
    std::pair<CreateResult, Payment> create(const CreateRequest& req);

    // Find a payment by ID. Copies into 'out' and returns true if found.
    // Returns false and leaves 'out' unchanged if not found.
    bool find(const std::string& id, Payment& out) const;

    // Transition a payment to a new status.
    // Returns false if the transition is illegal or the payment doesn't exist.
    bool transition(const std::string& id, PaymentStatus to);

    // List all payments (snapshot copy — safe to call from any thread).
    std::vector<Payment> all() const;

    // Hydrate store from persistent storage (called on boot)
    void loadFrom(const std::vector<Payment>& payments);

private:
    mutable CRITICAL_SECTION                        lock_;
    std::unordered_map<std::string, Payment>        store_;
    std::unordered_map<std::string, std::string>    idempotencyIndex_;

    static std::string generateId();
    static std::string nowIso();
};
