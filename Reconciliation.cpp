#include "Reconciliation.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace
{
    std::string nowIso()
    {
        const auto now = std::chrono::system_clock::now();
        const std::time_t t = std::chrono::system_clock::to_time_t(now);
        const std::tm* utc = std::gmtime(&t);
        std::ostringstream ss;
        if (utc)
            ss << std::put_time(utc, "%Y-%m-%dT%H:%M:%SZ");
        return ss.str();
    }

    std::string jsonEscape(const std::string& s)
    {
        std::string out;
        out.reserve(s.size());
        for (char c : s) {
            if      (c == '"')  out += "\\\"";
            else if (c == '\\') out += "\\\\";
            else if (c == '\n') out += "\\n";
            else if (c == '\r') out += "\\r";
            else                out += c;
        }
        return out;
    }
}

std::string ReconciliationReport::toJson() const
{
    std::ostringstream j;
    j << "{\n";
    j << "  \"status\": \""                       << status                    << "\",\n";
    j << "  \"total_payments_audited\": "         << totalPaymentsAudited      << ",\n";
    j << "  \"total_ledger_entries_audited\": "   << totalLedgerEntriesAudited << ",\n";
    j << "  \"audited_at\": \""                   << jsonEscape(auditedAt)     << "\",\n";
    j << "  \"discrepancies\": [\n";
    for (std::size_t i = 0; i < discrepancies.size(); ++i)
    {
        const auto& d = discrepancies[i];
        j << "    {\n";
        j << "      \"type\": \""         << jsonEscape(d.type)        << "\",\n";
        j << "      \"reference_id\": \"" << jsonEscape(d.referenceId) << "\",\n";
        j << "      \"description\": \""  << jsonEscape(d.description) << "\"\n";
        j << "    }" << (i + 1 < discrepancies.size() ? "," : "") << "\n";
    }
    j << "  ]\n";
    j << "}";
    return j.str();
}

ReconciliationReport ReconciliationEngine::runAudit(const PaymentStore& store, const Ledger& ledger)
{
    ReconciliationReport report{};
    report.auditedAt = nowIso();

    const auto payments = store.all();
    const auto entries  = ledger.getAllEntries();

    report.totalPaymentsAudited       = payments.size();
    report.totalLedgerEntriesAudited = entries.size();

    // Map payments by ID
    std::unordered_map<std::string, Payment> paymentMap;
    for (const auto& p : payments)
        paymentMap[p.id] = p;

    // Group ledger entries by transactionId
    std::unordered_map<std::string, std::vector<LedgerEntry>> entriesByTxn;
    for (const auto& e : entries)
    {
        entriesByTxn[e.transactionId].push_back(e);

        // Check for orphan ledger entries referencing nonexistent payments
        if (paymentMap.find(e.transactionId) == paymentMap.end())
        {
            Discrepancy d;
            d.type        = "ORPHAN_LEDGER_ENTRY";
            d.referenceId = e.id;
            d.description = "Ledger entry references unknown transactionId: " + e.transactionId;
            report.discrepancies.push_back(d);
        }
    }

    // Audit each payment against ledger
    for (const auto& p : payments)
    {
        if (p.status == PaymentStatus::CAPTURED || p.status == PaymentStatus::SETTLED)
        {
            const auto it = entriesByTxn.find(p.id);
            if (it == entriesByTxn.end())
            {
                Discrepancy d;
                d.type        = "MISSING_LEDGER_POSTING";
                d.referenceId = p.id;
                d.description = "Payment is marked CAPTURED but has no entries in double-entry ledger.";
                report.discrepancies.push_back(d);
            }
            else
            {
                // Verify credit sum equals payment amount
                int64_t creditSum = 0;
                for (const auto& leg : it->second)
                {
                    if (leg.type == EntryType::CREDIT)
                        creditSum += leg.amount;
                }

                if (creditSum != p.amount)
                {
                    Discrepancy d;
                    d.type        = "AMOUNT_MISMATCH";
                    d.referenceId = p.id;
                    d.description = "Ledger credits (" + std::to_string(creditSum) +
                                    ") do not equal payment amount (" + std::to_string(p.amount) + ").";
                    report.discrepancies.push_back(d);
                }
            }
        }
    }

    report.status = report.discrepancies.empty() ? "HEALTHY" : "DISCREPANCIES_FOUND";
    return report;
}
