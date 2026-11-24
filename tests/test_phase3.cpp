#include "../Payment.h"
#include "../PaymentStore.h"
#include "../Ledger.h"
#include "../ProviderSimulator.h"

#include <cassert>
#include <iostream>
#include <string>

void testProviderScenarios()
{
    std::cout << "[TEST] Payment Provider Simulator Bank Scenarios... ";
    ProviderSimulator provider;

    // 1. Clean approval
    ProviderResponse r1 = provider.authorize(50000, "INR", "");
    assert(r1.status == ProviderResultStatus::SUCCESS);
    assert(!r1.authorizationCode.empty());
    assert(r1.authorizationCode.rfind("AUTH_", 0) == 0);

    // 2. Forced decline (insufficient funds)
    ProviderResponse r2 = provider.authorize(50000, "INR", "decline");
    assert(r2.status == ProviderResultStatus::DECLINED);
    assert(r2.declineReason == "INSUFFICIENT_FUNDS");

    // 3. Forced fraud score decline
    ProviderResponse r3 = provider.authorize(50000, "INR", "fraud");
    assert(r3.status == ProviderResultStatus::DECLINED);
    assert(r3.declineReason == "SUSPECTED_FRAUD_DECLINE");

    // 4. Simulated bank timeout
    ProviderResponse r4 = provider.authorize(50000, "INR", "timeout");
    assert(r4.status == ProviderResultStatus::TIMEOUT);
    assert(r4.declineReason == "BANK_GATEWAY_TIMEOUT");

    // 5. Simulated network drop
    ProviderResponse r5 = provider.authorize(50000, "INR", "network_error");
    assert(r5.status == ProviderResultStatus::NETWORK_ERROR);
    assert(r5.declineReason == "ACQUIRER_CONNECTION_RESET");

    // 6. Exceeds bank transaction limit (> ₹10,00,000)
    ProviderResponse r6 = provider.authorize(150000000, "INR", "");
    assert(r6.status == ProviderResultStatus::DECLINED);
    assert(r6.declineReason == "EXCEEDS_SINGLE_TRANSACTION_LIMIT");

    std::cout << "PASSED\n";
}

void testPaymentWithProviderLifecycle()
{
    std::cout << "[TEST] Payment Lifecycle with Bank Integration... ";
    PaymentStore store;
    ProviderSimulator provider;

    // --- Scenario A: Approved Payment ---
    PaymentStore::CreateRequest reqA;
    reqA.idempotencyKey = "order_ok_1";
    reqA.amount = 75000;
    reqA.currency = "INR";
    reqA.description = "Approved order";

    auto resA = store.create(reqA);
    assert(resA.first == PaymentStore::CreateResult::Created);
    assert(store.transition(resA.second.id, PaymentStatus::PROCESSING));

    ProviderResponse bankA = provider.authorize(reqA.amount, reqA.currency, "");
    assert(bankA.status == ProviderResultStatus::SUCCESS);
    assert(store.transition(resA.second.id, PaymentStatus::AUTHORIZED));

    Payment pA;
    assert(store.find(resA.second.id, pA));
    assert(pA.status == PaymentStatus::AUTHORIZED);

    // --- Scenario B: Declined Payment ---
    PaymentStore::CreateRequest reqB;
    reqB.idempotencyKey = "order_declined_1";
    reqB.amount = 50000;
    reqB.currency = "INR";
    reqB.description = "Declined order";

    auto resB = store.create(reqB);
    assert(resB.first == PaymentStore::CreateResult::Created);
    assert(store.transition(resB.second.id, PaymentStatus::PROCESSING));

    ProviderResponse bankB = provider.authorize(reqB.amount, reqB.currency, "decline");
    assert(bankB.status == ProviderResultStatus::DECLINED);
    assert(store.transition(resB.second.id, PaymentStatus::FAILED));

    Payment pB;
    assert(store.find(resB.second.id, pB));
    assert(pB.status == PaymentStatus::FAILED);

    // Terminal state guard: cannot capture a FAILED payment
    assert(!store.transition(resB.second.id, PaymentStatus::CAPTURED));

    // --- Scenario C: Bank Gateway Timeout ---
    PaymentStore::CreateRequest reqC;
    reqC.idempotencyKey = "order_timeout_1";
    reqC.amount = 60000;
    reqC.currency = "INR";
    reqC.description = "Timeout order";

    auto resC = store.create(reqC);
    assert(resC.first == PaymentStore::CreateResult::Created);
    assert(store.transition(resC.second.id, PaymentStatus::PROCESSING));

    ProviderResponse bankC = provider.authorize(reqC.amount, reqC.currency, "timeout");
    assert(bankC.status == ProviderResultStatus::TIMEOUT);
    assert(store.transition(resC.second.id, PaymentStatus::FAILED));

    Payment pC;
    assert(store.find(resC.second.id, pC));
    assert(pC.status == PaymentStatus::FAILED);

    std::cout << "PASSED\n";
}

int main()
{
    std::cout << "========================================\n";
    std::cout << "   PayCore Phase 3 Invariant Verification\n";
    std::cout << "========================================\n\n";

    testProviderScenarios();
    testPaymentWithProviderLifecycle();

    std::cout << "\n========================================\n";
    std::cout << "   ALL PHASE 3 CHECKS PASSED!\n";
    std::cout << "========================================\n";
    return 0;
}
