#pragma once

#include "PaymentStore.h"
#include "Ledger.h"

#include <string>

// Manages atomic state persistence and disaster recovery hydration.
// Writes to a temporary file before an atomic swap to prevent partial write corruptions.
class PersistenceManager
{
public:
    // Persist full in-memory state of store and ledger to disk atomically.
    static bool saveState(const std::string& filePath,
                          const PaymentStore& store,
                          const Ledger& ledger);

    // Hydrate store and ledger from disk on server startup.
    // Returns true if successfully loaded or if file does not exist yet (first boot).
    static bool loadState(const std::string& filePath,
                          PaymentStore& store,
                          Ledger& ledger);
};
