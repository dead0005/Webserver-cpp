#include "Ledger.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <random>
#include <sstream>

namespace
{
    struct CriticalLock
    {
        CRITICAL_SECTION& cs;
        explicit CriticalLock(CRITICAL_SECTION& c) : cs(c) { EnterCriticalSection(&cs); }
        ~CriticalLock()                                    { LeaveCriticalSection(&cs); }
    };
}

std::string entryTypeToString(EntryType type)
{
    return type == EntryType::DEBIT ? "DEBIT" : "CREDIT";
}

Ledger::Ledger()
{
    InitializeCriticalSection(&lock_);
}

Ledger::~Ledger()
{
    DeleteCriticalSection(&lock_);
}

std::string Ledger::generateEntryId(std::size_t legIndex)
{
    const auto now = std::chrono::system_clock::now();
    const auto secs = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();

    static std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(0, 15);
    const char hex[] = "0123456789abcdef";

    std::string suffix(4, '0');
    for (char& c : suffix)
        c = hex[dist(rng)];

    return "ent_" + std::to_string(secs) + "_" + suffix + "_" + std::to_string(legIndex);
}

std::string Ledger::nowIso()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    const std::tm* utc = std::gmtime(&t);
    std::ostringstream ss;
    if (utc)
        ss << std::put_time(utc, "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

bool Ledger::postTransaction(const std::string& transactionId,
                             const std::vector<LedgerEntry>& entries,
                             std::string& errorMessage)
{
    CriticalLock guard(lock_);

    if (entries.empty())
    {
        errorMessage = "Transaction cannot be empty.";
        return false;
    }

    // Duplicate posting guard
    if (postedTransactions_.find(transactionId) != postedTransactions_.end())
    {
        errorMessage = "Ledger transaction already posted for ID: " + transactionId;
        return false;
    }

    int64_t totalDebits = 0;
    int64_t totalCredits = 0;

    for (const auto& entry : entries)
    {
        if (entry.amount <= 0)
        {
            errorMessage = "Ledger entry amount must be strictly positive (> 0).";
            return false;
        }

        if (entry.accountId.empty())
        {
            errorMessage = "Ledger entry accountId cannot be empty.";
            return false;
        }

        if (entry.type == EntryType::DEBIT)
            totalDebits += entry.amount;
        else
            totalCredits += entry.amount;
    }

    // THE DOUBLE-ENTRY INVARIANT: Debits must equal Credits
    if (totalDebits != totalCredits)
    {
        errorMessage = "Double-entry balance mismatch: total debits (" +
                       std::to_string(totalDebits) + ") != total credits (" +
                       std::to_string(totalCredits) + ").";
        return false;
    }

    const std::string ts = nowIso();
    std::size_t leg = 0;

    // Atomically commit all legs into the journal and update balances
    for (auto entry : entries)
    {
        if (entry.id.empty())
            entry.id = generateEntryId(leg++);
        if (entry.createdAt.empty())
            entry.createdAt = ts;
        if (entry.currency.empty())
            entry.currency = "INR";

        journal_.push_back(entry);

        // Net balance: credits increase (+), debits decrease (-)
        if (entry.type == EntryType::CREDIT)
            accountNetBalances_[entry.accountId] += entry.amount;
        else
            accountNetBalances_[entry.accountId] -= entry.amount;
    }

    postedTransactions_[transactionId] = true;
    errorMessage.clear();
    return true;
}

int64_t Ledger::getNetBalance(const std::string& accountId) const
{
    CriticalLock guard(lock_);
    const auto it = accountNetBalances_.find(accountId);
    if (it == accountNetBalances_.end())
        return 0;
    return it->second;
}

std::vector<LedgerEntry> Ledger::getEntriesForAccount(const std::string& accountId) const
{
    CriticalLock guard(lock_);
    std::vector<LedgerEntry> result;
    for (const auto& entry : journal_)
    {
        if (entry.accountId == accountId)
            result.push_back(entry);
    }
    return result;
}

std::vector<LedgerEntry> Ledger::getAllEntries() const
{
    CriticalLock guard(lock_);
    return journal_;
}

std::vector<std::string> Ledger::getAccounts() const
{
    CriticalLock guard(lock_);
    std::vector<std::string> accounts;
    accounts.reserve(accountNetBalances_.size());
    for (const auto& pair : accountNetBalances_)
        accounts.push_back(pair.first);
    return accounts;
}

void Ledger::loadFrom(const std::vector<LedgerEntry>& entries)
{
    CriticalLock guard(lock_);
    journal_ = entries;
    accountNetBalances_.clear();
    postedTransactions_.clear();
    for (const auto& entry : journal_)
    {
        if (entry.type == EntryType::CREDIT)
            accountNetBalances_[entry.accountId] += entry.amount;
        else
            accountNetBalances_[entry.accountId] -= entry.amount;

        if (!entry.transactionId.empty())
            postedTransactions_[entry.transactionId] = true;
    }
}
