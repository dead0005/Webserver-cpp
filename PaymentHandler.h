#pragma once

#include "HttpRequest.h"
#include "PaymentStore.h"
#include "Router.h"

// Registers all /payments routes onto the router.
// The store is shared state; it must outlive the router and all handlers.
void registerPaymentRoutes(Router& router, PaymentStore& store);
