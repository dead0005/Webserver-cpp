#include "../Payment.h"
#include "../PaymentStore.h"
#include "../Ledger.h"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

void testStateMachine()
{
    std::cout << "[TEST] Payment State Machine Transitions... ";
    assert(canTransition(PaymentStatus::INITIATED, PaymentStatus::PROCESSING));
    assert(canTransition(PaymentStatus::PROCESSING, PaymentStatus::AUTHORIZED));
    assert(canTransition(PaymentStatus::PROCESSING, PaymentStatus::FAILED));
    assert(canTransition(PaymentStatus::AUTHORIZED, PaymentStatus::CAPTURED));
    assert(canTransition(PaymentStatus::AUTHORIZED, PaymentStatus::CANCELLED));
    assert(canTransition(PaymentStatus::CAPTURED, PaymentStatus::SETTLED));
    assert(canTransition(PaymentStatus::CAPTURED, PaymentStatus::REFUNDED));

    // Terminal states cannot transition to anything
    assert(!canTransition(PaymentStatus::SETTLED, PaymentStatus::REFUNDED));
    assert(!canTransition(PaymentStatus::REFUNDED, PaymentStatus::CANCELLED));
    assert(!canTransition(PaymentStatus::CANCELLED, PaymentStatus::CAPTURED));
    assert(!canTransition(PaymentStatus::FAILED, PaymentStatus::AUTHORIZED));
    std::cout << "PASSED\n";
}

void testIdempotencyAndStore()
{
    std::cout << "[TEST] Idempotency & PaymentStore... ";
    PaymentStore store;

    PaymentStore::CreateRequest req;
    req.idempotencyKey = "order_12345";
    req.amount = 50000; // 500.00 INR
    req.currency = "INR";
    req.description = "Test order";

    // 1. Initial creation
    auto res1 = store.create(req);
    assert(res1.first == PaymentStore::CreateResult::Created);
    assert(res1.second.amount == 50000);
    assert(res1.second.status == PaymentStatus::INITIATED);
    const std::string txnId = res1.second.id;

    // 2. Retry with same key & same payload -> Duplicate (200 OK)
    auto res2 = store.create(req);
    assert(res2.first == PaymentStore::CreateResult::Duplicate);
    assert(res2.second.id == txnId);
    assert(res2.second.amount == 50000);

    // 3. Retry with same key & tampered payload -> KeyConflict (409 Conflict)
    PaymentStore::CreateRequest tampered = req;
    tampered.amount = 99999;
    auto res3 = store.create(tampered);
    assert(res3.first == PaymentStore::CreateResult::KeyConflict);

    // 4. State transition
    assert(store.transition(txnId, PaymentStatus::PROCESSING));
    assert(store.transition(txnId, PaymentStatus::AUTHORIZED));
    assert(store.transition(txnId, PaymentStatus::CAPTURED));
    assert(!store.transition(txnId, PaymentStatus::CANCELLED)); // invalid from CAPTURED

    Payment p;
    assert(store.find(txnId, p));
    assert(p.status == PaymentStatus::CAPTURED);
    assert(p.history.size() == 4); // INITIATED, PROCESSING, AUTHORIZED, CAPTURED

    std::cout << "PASSED\n";
}

void testDoubleEntryLedger()
{
    std::cout << "[TEST] Double-Entry Ledger Engine Invariants... ";
    Ledger ledger;

    const std::string txnId = "txn_test_999";
    const int64_t totalAmount = 100000; // 1000.00 INR
    const int64_t fee = 2000;           // 2% fee = 20.00 INR
    const int64_t merchantShare = 98000;// 980.00 INR

    // 1. Post valid capture journal batch (Debits == Credits)
    std::vector<LedgerEntry> captureEntries;
    captureEntries.push_back({ "", txnId, "escrow:customer",    EntryType::DEBIT,  totalAmount,   "INR", "Escrow hold release", "" });
    captureEntries.push_back({ "", txnId, "merchant:settlement", EntryType::CREDIT, merchantShare, "INR", "Merchant credit",      "" });
    captureEntries.push_back({ "", txnId, "platform:fee",        EntryType::CREDIT, fee,           "INR", "Platform take rate",   "" });

    std::string err;
    bool ok = ledger.postTransaction("cap_" + txnId, captureEntries, err);
    assert(ok);
    assert(err.empty());

    // Verify balances
    // Escrow: DEBIT 100000 -> net balance = -100000
    // Merchant: CREDIT 98000 -> net balance = +98000
    // Platform Fee: CREDIT 2000 -> net balance = +2000
    assert(ledger.getNetBalance("escrow:customer") == -100000);
    assert(ledger.getNetBalance("merchant:settlement") == 98000);
    assert(ledger.getNetBalance("platform:fee") == 2000);

    // Invariant Check: Sum of all account balances in system MUST BE ZERO
    const int64_t systemBalance = ledger.getNetBalance("escrow:customer")
                                + ledger.getNetBalance("merchant:settlement")
                                + ledger.getNetBalance("platform:fee");
    assert(systemBalance == 0);

    // 2. Test Invariant Enforcement: Reject Unbalanced Entries (Debits != Credits)
    std::vector<LedgerEntry> unbalancedEntries;
    unbalancedEntries.push_back({ "", "bad_txn", "account:A", EntryType::DEBIT,  500, "INR", "Leg 1", "" });
    unbalancedEntries.push_back({ "", "bad_txn", "account:B", EntryType::CREDIT, 400, "INR", "Leg 2", "" }); // 500 != 400

    bool badOk = ledger.postTransaction("bad_txn", unbalancedEntries, err);
    assert(!badOk);
    assert(!err.empty());
    std::cout << "\n       (Successfully caught unbalanced batch: \"" << err << "\")... ";

    // 3. Test Duplicate Posting Guard
    bool dupOk = ledger.postTransaction("cap_" + txnId, captureEntries, err);
    assert(!dupOk);
    std::cout << "\n       (Successfully rejected duplicate transaction: \"" << err << "\")... ";

    // 4. Post Refund Reversal Batch
    std::vector<LedgerEntry> refundEntries;
    refundEntries.push_back({ "", txnId, "escrow:customer",    EntryType::CREDIT, totalAmount,   "INR", "Refund to customer", "" });
    refundEntries.push_back({ "", txnId, "merchant:settlement", EntryType::DEBIT,  merchantShare, "INR", "Reverse merchant settlement", "" });
    refundEntries.push_back({ "", txnId, "platform:fee",        EntryType::DEBIT,  fee,           "INR", "Reverse platform fee", "" });

    bool refOk = ledger.postTransaction("ref_" + txnId, refundEntries, err);
    assert(refOk);

    // After refund, all accounts must balance back to exactly 0
    assert(ledger.getNetBalance("escrow:customer") == 0);
    assert(ledger.getNetBalance("merchant:settlement") == 0);
    assert(ledger.getNetBalance("platform:fee") == 0);

    // Complete audit journal contains 6 entries (3 capture + 3 refund)
    assert(ledger.getAllEntries().size() == 6);

    std::cout << "PASSED\n";
}

int main()
{
    std::cout << "========================================\n";
    std::cout << "   PayCore Phase 2 Invariant Verification\n";
    std::cout << "========================================\n\n";

    testStateMachine();
    testIdempotencyAndStore();
    testDoubleEntryLedger();

    std::cout << "\n========================================\n";
    std::cout << "   ALL 15 INVARIANT CHECKS PASSED!\n";
    std::cout << "========================================\n";
    return 0;
}
