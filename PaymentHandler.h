#pragma once

#include "HttpRequest.h"
#include "Ledger.h"
#include "PaymentStore.h"
#include "Router.h"

// Registers all payment and double-entry ledger routes onto the router.
// Both store and ledger are shared state and must outlive the router and handlers.
void registerPaymentRoutes(Router& router, PaymentStore& store, Ledger& ledger);
