#include "../Payment.h"
#include "../PaymentStore.h"
#include "../Ledger.h"
#include "../PersistenceManager.h"
#include "../Reconciliation.h"

#include <cassert>
#include <cstdio>
#include <iostream>
#include <string>

void testPersistenceAndHydration()
{
    std::cout << "[TEST] PersistenceManager Atomic Save & Disaster Recovery Hydration... ";
    const std::string testFile = "tests/test_state_temp.json";

    PaymentStore store1;
    Ledger ledger1;

    // 1. Create a payment in store1
    PaymentStore::CreateRequest req;
    req.idempotencyKey = "persist_key_1";
    req.amount = 80000; // 800.00 INR
    req.currency = "INR";
    req.description = "Persistent Payment";

    auto createRes = store1.create(req);
    assert(createRes.first == PaymentStore::CreateResult::Created);
    const std::string txnId = createRes.second.id;

    assert(store1.transition(txnId, PaymentStatus::PROCESSING));
    assert(store1.transition(txnId, PaymentStatus::AUTHORIZED));
    assert(store1.transition(txnId, PaymentStatus::CAPTURED));

    // 2. Post matching double-entry ledger entries in ledger1
    std::vector<LedgerEntry> entries;
    entries.push_back({ "", txnId, "escrow:customer",    EntryType::DEBIT,  80000, "INR", "Escrow hold release", "" });
    entries.push_back({ "", txnId, "merchant:settlement", EntryType::CREDIT, 78400, "INR", "Merchant credit 98%", "" });
    entries.push_back({ "", txnId, "platform:fee",        EntryType::CREDIT,  1600, "INR", "Platform fee 2%",     "" });

    std::string err;
    assert(ledger1.postTransaction("cap_" + txnId, entries, err));

    // 3. Save snapshot to disk
    assert(PersistenceManager::saveState(testFile, store1, ledger1));

    // 4. Simulate complete server death & reboot: hydrate into brand-new empty objects
    PaymentStore store2;
    Ledger ledger2;

    assert(PersistenceManager::loadState(testFile, store2, ledger2));

    // 5. Verify hydrated store2 has exact payment data
    Payment p;
    assert(store2.find(txnId, p));
    assert(p.amount == 80000);
    assert(p.status == PaymentStatus::CAPTURED);
    assert(p.idempotencyKey == "persist_key_1");
    assert(p.history.size() == 4);

    // Verify idempotency works on rebooted store2 (Duplicate key returns cached payment)
    auto dupRes = store2.create(req);
    assert(dupRes.first == PaymentStore::CreateResult::Duplicate);
    assert(dupRes.second.id == txnId);

    // 6. Verify hydrated ledger2 has exact balances and entries
    assert(ledger2.getNetBalance("escrow:customer") == -80000);
    assert(ledger2.getNetBalance("merchant:settlement") == 78400);
    assert(ledger2.getNetBalance("platform:fee") == 1600);
    assert(ledger2.getAllEntries().size() == 3);

    std::remove(testFile.c_str());
    std::cout << "PASSED\n";
}

void testReconciliationEngine()
{
    std::cout << "[TEST] Financial Reconciliation Engine (Audit & Anomaly Detection)... ";
    PaymentStore store;
    Ledger ledger;

    const std::string txnId = "txn_audit_100";

    // 1. Create a captured payment and matching balanced ledger entries
    PaymentStore::CreateRequest req;
    req.idempotencyKey = "audit_key_1";
    req.amount = 50000;
    req.currency = "INR";
    req.description = "Healthy Payment";

    auto res = store.create(req);
    store.transition(res.second.id, PaymentStatus::PROCESSING);
    store.transition(res.second.id, PaymentStatus::AUTHORIZED);
    store.transition(res.second.id, PaymentStatus::CAPTURED);

    std::vector<LedgerEntry> legs;
    legs.push_back({ "leg_1", res.second.id, "escrow:customer",    EntryType::DEBIT,  50000, "INR", "Escrow",   "" });
    legs.push_back({ "leg_2", res.second.id, "merchant:settlement", EntryType::CREDIT, 49000, "INR", "Merchant", "" });
    legs.push_back({ "leg_3", res.second.id, "platform:fee",        EntryType::CREDIT,  1000, "INR", "Fee",      "" });

    std::string err;
    assert(ledger.postTransaction("cap_" + res.second.id, legs, err));

    // Audit 1: Should be 100% HEALTHY
    ReconciliationReport r1 = ReconciliationEngine::runAudit(store, ledger);
    assert(r1.status == "HEALTHY");
    assert(r1.discrepancies.empty());
    assert(r1.totalPaymentsAudited == 1);
    assert(r1.totalLedgerEntriesAudited == 3);

    // 2. Inject an anomaly: Create a CAPTURED payment WITHOUT any ledger entries (e.g. database dropped second query)
    PaymentStore::CreateRequest orphanReq;
    orphanReq.idempotencyKey = "orphan_key";
    orphanReq.amount = 30000;
    orphanReq.currency = "INR";
    orphanReq.description = "Orphan Payment";

    auto orphanRes = store.create(orphanReq);
    store.transition(orphanRes.second.id, PaymentStatus::PROCESSING);
    store.transition(orphanRes.second.id, PaymentStatus::AUTHORIZED);
    store.transition(orphanRes.second.id, PaymentStatus::CAPTURED);

    // Audit 2: Must catch MISSING_LEDGER_POSTING
    ReconciliationReport r2 = ReconciliationEngine::runAudit(store, ledger);
    assert(r2.status == "DISCREPANCIES_FOUND");
    assert(r2.discrepancies.size() == 1);
    assert(r2.discrepancies[0].type == "MISSING_LEDGER_POSTING");
    assert(r2.discrepancies[0].referenceId == orphanRes.second.id);
    std::cout << "\n       (Successfully flagged discrepancy: \"" << r2.discrepancies[0].description << "\")... ";

    // 3. Inject an orphan ledger entry referencing nonexistent payment
    std::vector<LedgerEntry> orphanLegs;
    orphanLegs.push_back({ "ghost_1", "ghost_payment_999", "escrow:customer",    EntryType::DEBIT,  1000, "INR", "Ghost", "" });
    orphanLegs.push_back({ "ghost_2", "ghost_payment_999", "merchant:settlement", EntryType::CREDIT, 1000, "INR", "Ghost", "" });
    assert(ledger.postTransaction("cap_ghost", orphanLegs, err));

    // Audit 3: Must catch ORPHAN_LEDGER_ENTRY
    ReconciliationReport r3 = ReconciliationEngine::runAudit(store, ledger);
    assert(r3.status == "DISCREPANCIES_FOUND");
    bool foundOrphan = false;
    for (const auto& d : r3.discrepancies) {
        if (d.type == "ORPHAN_LEDGER_ENTRY")
            foundOrphan = true;
    }
    assert(foundOrphan);

    std::cout << "PASSED\n";
}

int main()
{
    std::cout << "========================================\n";
    std::cout << "   PayCore Phase 4 Invariant Verification\n";
    std::cout << "========================================\n\n";

    testPersistenceAndHydration();
    testReconciliationEngine();

    std::cout << "\n========================================\n";
    std::cout << "   ALL PHASE 4 CHECKS PASSED!\n";
    std::cout << "========================================\n";
    return 0;
}
