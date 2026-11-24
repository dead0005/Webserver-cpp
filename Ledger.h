#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <windows.h>

// Every double-entry movement is either a DEBIT or a CREDIT.
// In fintech accounting:
// - Asset accounts (e.g. Escrow, Bank) increase with DEBIT, decrease with CREDIT.
// - Liability/Equity/Revenue accounts (e.g. Merchant Balance, Fees) increase with CREDIT, decrease with DEBIT.
enum class EntryType
{
    DEBIT,
    CREDIT
};

std::string entryTypeToString(EntryType type);

// An immutable ledger journal entry.
struct LedgerEntry
{
    std::string id;              // e.g. "ent_179552..._01"
    std::string transactionId;   // links back to Payment id ("txn_...")
    std::string accountId;       // e.g. "escrow:customer", "merchant:settlement", "platform:fee"
    EntryType   type;            // DEBIT or CREDIT
    int64_t     amount;          // minor units (paise), must be > 0
    std::string currency;        // "INR"
    std::string description;     // "Payment capture hold release", "Fee deduction"
    std::string createdAt;       // ISO 8601 UTC timestamp
};

// Double-Entry Ledger Engine
// Enforces the core accounting invariant: sum(DEBIT) == sum(CREDIT) for every posted journal batch.
class Ledger
{
public:
    Ledger();
    ~Ledger();

    // Prevent copies (owns CRITICAL_SECTION)
    Ledger(const Ledger&) = delete;
    Ledger& operator=(const Ledger&) = delete;

    // Post a multi-legged journal batch atomically.
    // Fails and rolls back if:
    // 1. entries is empty
    // 2. sum of debits != sum of credits
    // 3. duplicate transactionId already posted
    // 4. any entry has amount <= 0
    bool postTransaction(const std::string& transactionId,
                         const std::vector<LedgerEntry>& entries,
                         std::string& errorMessage);

    // Get current calculated balance for an account in minor units (paise).
    // Balance = sum(Credits) - sum(Debits) for settlement/revenue accounts,
    // or sum(Debits) - sum(Credits) for asset/escrow accounts.
    // For universal consistency, we return Net Balance = (Credits - Debits).
    int64_t getNetBalance(const std::string& accountId) const;

    // Get all immutable journal entries posted for a specific account.
    std::vector<LedgerEntry> getEntriesForAccount(const std::string& accountId) const;

    // Get the complete immutable audit journal (all entries).
    std::vector<LedgerEntry> getAllEntries() const;

    // Get a list of all distinct accounts currently in the ledger.
    std::vector<std::string> getAccounts() const;

private:
    mutable CRITICAL_SECTION                       lock_;
    std::vector<LedgerEntry>                       journal_;            // append-only immutable journal
    std::unordered_map<std::string, int64_t>       accountNetBalances_; // accountId -> net balance (credit - debit)
    std::unordered_map<std::string, bool>          postedTransactions_; // txnId -> true (prevents duplicate postings)

    static std::string generateEntryId(std::size_t legIndex);
    static std::string nowIso();
};
