#pragma once

#include "PaymentStore.h"
#include "Ledger.h"

#include <string>
#include <vector>

struct Discrepancy
{
    std::string type;         // "MISSING_LEDGER_POSTING", "AMOUNT_MISMATCH", "ORPHAN_ENTRY"
    std::string referenceId;  // payment ID or ledger ID
    std::string description;
};

struct ReconciliationReport
{
    std::string              status; // "HEALTHY" or "DISCREPANCIES_FOUND"
    std::size_t              totalPaymentsAudited;
    std::size_t              totalLedgerEntriesAudited;
    std::vector<Discrepancy> discrepancies;
    std::string              auditedAt;

    std::string toJson() const;
};

class ReconciliationEngine
{
public:
    static ReconciliationReport runAudit(const PaymentStore& store, const Ledger& ledger);
};
