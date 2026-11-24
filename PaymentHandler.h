#pragma once

#include "HttpRequest.h"
#include "Ledger.h"
#include "PaymentStore.h"
#include "ProviderSimulator.h"
#include "Router.h"

// Registers all payment, ledger, and banking simulation routes onto the router.
void registerPaymentRoutes(Router& router, PaymentStore& store, Ledger& ledger, ProviderSimulator& provider);
