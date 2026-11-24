#include "PaymentStore.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <random>
#include <sstream>

// --- RAII guard for CRITICAL_SECTION ---
// Works like std::lock_guard but for Win32 CRITICAL_SECTION.
struct CriticalLock
{
    CRITICAL_SECTION& cs;
    CriticalLock(CRITICAL_SECTION& c) : cs(c) { EnterCriticalSection(&cs); }
    ~CriticalLock()                            { LeaveCriticalSection(&cs); }
};

// --- Constructor / Destructor ---

PaymentStore::PaymentStore()
{
    InitializeCriticalSection(&lock_);
}

PaymentStore::~PaymentStore()
{
    DeleteCriticalSection(&lock_);
}

// --- Helpers ---

std::string PaymentStore::generateId()
{
    // Format: txn_<unix_seconds>_<6 random hex chars>
    // Enough uniqueness for a demo; don't use this in a real system.
    const auto now  = std::chrono::system_clock::now();
    const auto secs = std::chrono::duration_cast<std::chrono::seconds>(
        now.time_since_epoch()).count();

    static std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(0, 15);
    const char hex[] = "0123456789abcdef";

    std::string suffix(6, '0');
    for (char& c : suffix)
        c = hex[dist(rng)];

    return "txn_" + std::to_string(secs) + "_" + suffix;
}

std::string PaymentStore::nowIso()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    const std::tm* utc = std::gmtime(&t);
    std::ostringstream ss;
    if (utc)
        ss << std::put_time(utc, "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

// --- Public API ---

std::pair<PaymentStore::CreateResult, Payment>
PaymentStore::create(const CreateRequest& req)
{
    CriticalLock guard(lock_);

    // Check for existing idempotency key
    const auto it = idempotencyIndex_.find(req.idempotencyKey);
    if (it != idempotencyIndex_.end())
    {
        const Payment& existing = store_.at(it->second);

        // Same key but different amount or currency = conflict
        if (existing.amount != req.amount || existing.currency != req.currency)
            return std::make_pair(CreateResult::KeyConflict, existing);

        // Same key same payload = return the original response unchanged
        return std::make_pair(CreateResult::Duplicate, existing);
    }

    // New payment
    const std::string ts = nowIso();
    Payment p;
    p.id             = generateId();
    p.idempotencyKey = req.idempotencyKey;
    p.amount         = req.amount;
    p.currency       = req.currency;
    p.description    = req.description;
    p.status         = PaymentStatus::INITIATED;
    p.createdAt      = ts;
    p.updatedAt      = ts;
    p.history.push_back("INITIATED @ " + ts);

    idempotencyIndex_[p.idempotencyKey] = p.id;
    store_[p.id] = p;
    return std::make_pair(CreateResult::Created, p);
}

bool PaymentStore::find(const std::string& id, Payment& out) const
{
    CriticalLock guard(lock_);
    const auto it = store_.find(id);
    if (it == store_.end()) return false;
    out = it->second;
    return true;
}

bool PaymentStore::transition(const std::string& id, PaymentStatus to)
{
    CriticalLock guard(lock_);
    const auto it = store_.find(id);
    if (it == store_.end()) return false;

    Payment& p = it->second;
    if (!canTransition(p.status, to)) return false;

    p.status    = to;
    p.updatedAt = nowIso();
    p.history.push_back(statusToString(to) + " @ " + p.updatedAt);
    return true;
}

std::vector<Payment> PaymentStore::all() const
{
    CriticalLock guard(lock_);
    std::vector<Payment> result;
    result.reserve(store_.size());
    for (const auto& kv : store_)
        result.push_back(kv.second);
    return result;
}
