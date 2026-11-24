#include "Payment.h"

#include <stdexcept>

// Define which state transitions are legal.
// Think of this as the guard table for a state machine.
bool canTransition(PaymentStatus from, PaymentStatus to)
{
    switch (from)
    {
    case PaymentStatus::INITIATED:
        return to == PaymentStatus::PROCESSING;

    case PaymentStatus::PROCESSING:
        return to == PaymentStatus::AUTHORIZED
            || to == PaymentStatus::FAILED;

    case PaymentStatus::AUTHORIZED:
        return to == PaymentStatus::CAPTURED
            || to == PaymentStatus::CANCELLED;

    case PaymentStatus::CAPTURED:
        return to == PaymentStatus::SETTLED
            || to == PaymentStatus::REFUNDED;

    // Terminal states — no way out
    case PaymentStatus::SETTLED:
    case PaymentStatus::FAILED:
    case PaymentStatus::CANCELLED:
    case PaymentStatus::REFUNDED:
        return false;
    }
    return false;
}

std::string statusToString(PaymentStatus s)
{
    switch (s)
    {
    case PaymentStatus::INITIATED:  return "INITIATED";
    case PaymentStatus::PROCESSING: return "PROCESSING";
    case PaymentStatus::AUTHORIZED: return "AUTHORIZED";
    case PaymentStatus::CAPTURED:   return "CAPTURED";
    case PaymentStatus::SETTLED:    return "SETTLED";
    case PaymentStatus::FAILED:     return "FAILED";
    case PaymentStatus::CANCELLED:  return "CANCELLED";
    case PaymentStatus::REFUNDED:   return "REFUNDED";
    }
    return "UNKNOWN";
}

PaymentStatus stringToStatus(const std::string& s)
{
    if (s == "INITIATED")  return PaymentStatus::INITIATED;
    if (s == "PROCESSING") return PaymentStatus::PROCESSING;
    if (s == "AUTHORIZED") return PaymentStatus::AUTHORIZED;
    if (s == "CAPTURED")   return PaymentStatus::CAPTURED;
    if (s == "SETTLED")    return PaymentStatus::SETTLED;
    if (s == "FAILED")     return PaymentStatus::FAILED;
    if (s == "CANCELLED")  return PaymentStatus::CANCELLED;
    if (s == "REFUNDED")   return PaymentStatus::REFUNDED;
    throw std::invalid_argument("Unknown payment status: " + s);
}
