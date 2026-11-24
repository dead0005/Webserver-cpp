#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Every possible state a payment can be in.
// Money flows: INITIATED -> PROCESSING -> AUTHORIZED -> CAPTURED -> SETTLED
enum class PaymentStatus
{
    INITIATED,
    PROCESSING,
    AUTHORIZED,
    CAPTURED,
    SETTLED,
    FAILED,
    CANCELLED,
    REFUNDED
};

// A single payment record. Amount is always in integer minor units (paise for INR)
// so we never touch floating-point for money.
struct Payment
{
    std::string   id;              // e.g. txn_1700000000_a3f9b2
    std::string   idempotencyKey;  // client-supplied dedup key
    int64_t       amount;          // minor units (paise). 10000 = ₹100.00
    std::string   currency;        // "INR"
    std::string   description;
    PaymentStatus status;
    std::string   createdAt;
    std::string   updatedAt;

    // Every status change is appended here so we have a full audit trail.
    // e.g. ["INITIATED @ 2026-11-24T12:00:00Z", "PROCESSING @ 2026-11-24T12:00:01Z"]
    std::vector<std::string> history;
};

// State machine guard — returns true only for valid transitions.
bool canTransition(PaymentStatus from, PaymentStatus to);

// Convert enum <-> string for JSON responses and history logs.
std::string     statusToString(PaymentStatus s);
PaymentStatus   stringToStatus(const std::string& s);  // throws std::invalid_argument on unknown
