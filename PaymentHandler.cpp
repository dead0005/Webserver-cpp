#include "PaymentHandler.h"
#include "Ledger.h"

#include <cctype>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>

// ============================================================
//  Small JSON helpers — no external library needed.
//  These only handle the simple, flat JSON we produce/consume.
// ============================================================

// Extract a string value from flat JSON. e.g. jsonStr(body, "currency", val) -> true
// Supports both quoted "key":"val" and unquoted key:val (handles PowerShell curl quirks).
static bool jsonStr(const std::string& json, const std::string& key, std::string& out)
{
    std::string needle = "\"" + key + "\"";
    std::size_t keyPos = json.find(needle);
    std::size_t needleLen = needle.size();
    if (keyPos == std::string::npos)
    {
        needle = key;
        keyPos = json.find(needle);
        needleLen = needle.size();
    }
    if (keyPos == std::string::npos) return false;

    const std::size_t colon = json.find(':', keyPos + needleLen);
    if (colon == std::string::npos) return false;

    std::size_t valStart = colon + 1;
    while (valStart < json.size() && (json[valStart] == ' ' || json[valStart] == '\t'))
        ++valStart;

    if (valStart >= json.size()) return false;

    if (json[valStart] == '"')
    {
        ++valStart; // skip opening quote
        const std::size_t valEnd = json.find('"', valStart);
        if (valEnd == std::string::npos) return false;
        out = json.substr(valStart, valEnd - valStart);
        return true;
    }
    else
    {
        std::size_t valEnd = valStart;
        while (valEnd < json.size() && json[valEnd] != ',' && json[valEnd] != '}' &&
               json[valEnd] != '\r' && json[valEnd] != '\n' && json[valEnd] != ' ' && json[valEnd] != '\t')
            ++valEnd;
        if (valEnd == valStart) return false;
        out = json.substr(valStart, valEnd - valStart);
        return true;
    }
}

// Extract an integer value from flat JSON. e.g. jsonInt(body, "amount", val) -> 10000
// Supports both quoted "key":100 and unquoted key:100.
static bool jsonInt(const std::string& json, const std::string& key, int64_t& out)
{
    std::string needle = "\"" + key + "\"";
    std::size_t keyPos = json.find(needle);
    std::size_t needleLen = needle.size();
    if (keyPos == std::string::npos)
    {
        needle = key;
        keyPos = json.find(needle);
        needleLen = needle.size();
    }
    if (keyPos == std::string::npos) return false;

    const std::size_t colon = json.find(':', keyPos + needleLen);
    if (colon == std::string::npos) return false;

    std::size_t valStart = colon + 1;
    while (valStart < json.size() && (json[valStart] == ' ' || json[valStart] == '\t'))
        ++valStart;

    std::size_t numEnd = valStart;
    if (numEnd < json.size() && json[numEnd] == '-') ++numEnd;
    while (numEnd < json.size() && std::isdigit(static_cast<unsigned char>(json[numEnd])))
        ++numEnd;

    if (numEnd == valStart) return false;

    try {
        out = static_cast<int64_t>(std::stoll(json.substr(valStart, numEnd - valStart)));
        return true;
    } catch (...) {
        return false;
    }
}

// Escape a string for safe embedding in a JSON value (handles quotes and backslashes).
static std::string jsonEscape(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (const char c : s) {
        if      (c == '"')  out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else                out += c;
    }
    return out;
}

// Serialize a Payment to a JSON object string.
static std::string paymentToJson(const Payment& p)
{
    std::ostringstream j;
    j << "{\n";
    j << "  \"id\": \""              << jsonEscape(p.id)             << "\",\n";
    j << "  \"idempotency_key\": \"" << jsonEscape(p.idempotencyKey) << "\",\n";
    j << "  \"amount\": "            << p.amount                     << ",\n";
    j << "  \"currency\": \""        << jsonEscape(p.currency)       << "\",\n";
    j << "  \"description\": \""     << jsonEscape(p.description)    << "\",\n";
    j << "  \"status\": \""          << statusToString(p.status)     << "\",\n";
    j << "  \"created_at\": \""      << jsonEscape(p.createdAt)      << "\",\n";
    j << "  \"updated_at\": \""      << jsonEscape(p.updatedAt)      << "\",\n";
    j << "  \"history\": [";
    for (std::size_t i = 0; i < p.history.size(); ++i) {
        if (i > 0) j << ", ";
        j << "\"" << jsonEscape(p.history[i]) << "\"";
    }
    j << "]\n}";
    return j.str();
}

// Serialize a LedgerEntry to a JSON object string.
static std::string ledgerEntryToJson(const LedgerEntry& e)
{
    std::ostringstream j;
    j << "{\n";
    j << "  \"id\": \""             << jsonEscape(e.id)            << "\",\n";
    j << "  \"transaction_id\": \"" << jsonEscape(e.transactionId) << "\",\n";
    j << "  \"account_id\": \""     << jsonEscape(e.accountId)     << "\",\n";
    j << "  \"type\": \""           << entryTypeToString(e.type)   << "\",\n";
    j << "  \"amount\": "           << e.amount                    << ",\n";
    j << "  \"currency\": \""       << jsonEscape(e.currency)      << "\",\n";
    j << "  \"description\": \""    << jsonEscape(e.description)   << "\",\n";
    j << "  \"created_at\": \""     << jsonEscape(e.createdAt)     << "\"\n";
    j << "}";
    return j.str();
}

// Build an HTTP date header value (e.g. Mon, 24 Nov 2026 12:00:00 GMT).
static std::string httpDate()
{
    const std::time_t now = std::time(nullptr);
    const std::tm* utc = std::gmtime(&now);
    if (!utc) return "";
    std::ostringstream ss;
    ss << std::put_time(utc, "%a, %d %b %Y %H:%M:%S GMT");
    return ss.str();
}

// Build a full HTTP response with a JSON body.
// keepAlive controls whether we include "Connection: keep-alive" or "Connection: close".
static std::string makeJsonResponse(int code, const std::string& reason,
                                    const std::string& body, bool keepAlive)
{
    std::ostringstream r;
    r << "HTTP/1.1 " << code << " " << reason << "\r\n";
    r << "Date: " << httpDate() << "\r\n";
    r << "Server: PayCore/0.1\r\n";
    r << "Content-Type: application/json\r\n";
    r << "Content-Length: " << body.size() << "\r\n";
    r << "Connection: " << (keepAlive ? "keep-alive" : "close") << "\r\n";
    r << "X-Content-Type-Options: nosniff\r\n";
    r << "Cache-Control: no-store\r\n";
    r << "\r\n";
    r << body;
    return r.str();
}

// Build an error response with a plain-text JSON error message.
static std::string makeErrorResponse(int code, const std::string& reason,
                                     const std::string& msg, bool keepAlive)
{
    // Return error as a JSON object so clients always get JSON from /payments routes.
    const std::string body = "{\"error\": \"" + jsonEscape(msg) + "\"}";
    return makeJsonResponse(code, reason, body, keepAlive);
}

// ============================================================
//  Route handlers
//  Each function matches one HTTP endpoint.
// ============================================================

// POST /payments
// Required headers: Idempotency-Key
// Required body fields: amount (integer paise), currency (string), description (string)
// Auto-advances: INITIATED -> PROCESSING -> AUTHORIZED (simulated instant auth)
static std::string handleCreatePayment(const HttpRequest& req, const std::string& /*unused*/,
                                       PaymentStore& store)
{
    // Idempotency key is mandatory — without it we can't do safe retries.
    const auto ikey = req.headers.find("idempotency-key");
    if (ikey == req.headers.end() || ikey->second.empty())
        return makeErrorResponse(400, "Bad Request", "Missing Idempotency-Key header.", req.keepAlive);

    int64_t amount = 0;
    if (!jsonInt(req.body, "amount", amount) || amount <= 0)
        return makeErrorResponse(400, "Bad Request", "amount must be a positive integer (minor units).", req.keepAlive);

    std::string currency;
    if (!jsonStr(req.body, "currency", currency) || currency.empty())
        return makeErrorResponse(400, "Bad Request", "currency is required (e.g. \"INR\").", req.keepAlive);

    std::string description;
    jsonStr(req.body, "description", description);

    PaymentStore::CreateRequest cr;
    cr.idempotencyKey = ikey->second;
    cr.amount         = amount;
    cr.currency       = currency;
    cr.description    = description;

    const std::pair<PaymentStore::CreateResult, Payment> res = store.create(cr);
    const PaymentStore::CreateResult result = res.first;
    const Payment payment = res.second;

    if (result == PaymentStore::CreateResult::KeyConflict)
        return makeErrorResponse(409, "Conflict",
            "Idempotency-Key already used with different payment details.", req.keepAlive);

    // For duplicate keys with same payload, return 200 (already processed).
    if (result == PaymentStore::CreateResult::Duplicate)
        return makeJsonResponse(200, "OK", paymentToJson(payment), req.keepAlive);

    // --- Simulated instant authorization ---
    // Real systems would call an external provider here and await a callback.
    // We advance the state machine synchronously to demonstrate the full lifecycle.
    store.transition(payment.id, PaymentStatus::PROCESSING);
    store.transition(payment.id, PaymentStatus::AUTHORIZED);

    // Fetch the final state after transitions.
    Payment updated;
    const std::string body = store.find(payment.id, updated)
                           ? paymentToJson(updated)
                           : paymentToJson(payment);

    return makeJsonResponse(201, "Created", body, req.keepAlive);
}

// GET /payments
// Returns all payments as a JSON array.
static std::string handleListPayments(const HttpRequest& req, const std::string& /*unused*/,
                                      PaymentStore& store)
{
    const auto payments = store.all();
    std::ostringstream j;
    j << "[\n";
    for (std::size_t i = 0; i < payments.size(); ++i) {
        if (i > 0) j << ",\n";
        j << paymentToJson(payments[i]);
    }
    j << "\n]";
    return makeJsonResponse(200, "OK", j.str(), req.keepAlive);
}

// GET /payments/<id>
// pathParam is the transaction ID extracted by the router (e.g. "txn_123_abc456")
static std::string handleGetPayment(const HttpRequest& req, const std::string& pathParam,
                                    PaymentStore& store)
{
    if (pathParam.empty())
        return makeErrorResponse(400, "Bad Request", "Missing payment ID in path.", req.keepAlive);

    Payment payment;
    if (!store.find(pathParam, payment))
        return makeErrorResponse(404, "Not Found", "Payment not found.", req.keepAlive);

    return makeJsonResponse(200, "OK", paymentToJson(payment), req.keepAlive);
}

// POST /payments/<id>/capture  or  POST /payments/<id>/cancel  or  POST /payments/<id>/refund
// pathParam is everything after "/payments/" — e.g. "txn_123_abc456/capture"
static std::string handlePaymentAction(const HttpRequest& req, const std::string& pathParam,
                                       PaymentStore& store, Ledger& ledger)
{
    // Split pathParam into  id  /  action
    const std::size_t slash = pathParam.find('/');
    if (slash == std::string::npos)
        return makeErrorResponse(404, "Not Found", "Unknown route.", req.keepAlive);

    const std::string id     = pathParam.substr(0, slash);
    const std::string action = pathParam.substr(slash + 1);

    if (id.empty())
        return makeErrorResponse(400, "Bad Request", "Missing payment ID.", req.keepAlive);

    PaymentStatus target;
    if      (action == "capture") target = PaymentStatus::CAPTURED;
    else if (action == "cancel")  target = PaymentStatus::CANCELLED;
    else if (action == "refund")  target = PaymentStatus::REFUNDED;
    else
        return makeErrorResponse(404, "Not Found",
            "Unknown action '" + action + "'. Use capture, cancel, or refund.", req.keepAlive);

    if (!store.transition(id, target))
    {
        // Could be: payment not found, or state transition not allowed.
        Payment payment;
        if (!store.find(id, payment))
            return makeErrorResponse(404, "Not Found", "Payment not found.", req.keepAlive);

        return makeErrorResponse(422, "Unprocessable Entity",
            "Cannot " + action + " a payment in " + statusToString(payment.status) + " state.",
            req.keepAlive);
    }

    Payment updated;
    if (!store.find(id, updated))
        return makeErrorResponse(500, "Internal Server Error", "State updated but payment lost.", req.keepAlive);

    // --- Double-Entry Ledger Posting ---
    // When a payment is captured or refunded, we atomically post multi-legged ledger entries.
    // Invariant: sum(Debits) == sum(Credits)
    if (action == "capture")
    {
        // Platform fee: 2% take-rate (200 bps)
        const int64_t fee = (updated.amount * 2) / 100;
        const int64_t merchantSettlement = updated.amount - fee;

        std::vector<LedgerEntry> entries;
        entries.push_back({ "", id, "escrow:customer",    EntryType::DEBIT,  updated.amount,     updated.currency, "Release customer escrow hold", "" });
        entries.push_back({ "", id, "merchant:settlement", EntryType::CREDIT, merchantSettlement, updated.currency, "Merchant settlement credit",  "" });
        if (fee > 0)
            entries.push_back({ "", id, "platform:fee",        EntryType::CREDIT, fee,                updated.currency, "2% platform processing fee",  "" });

        std::string ledgerErr;
        ledger.postTransaction("cap_" + id, entries, ledgerErr);
    }
    else if (action == "refund")
    {
        // Full reversal of capture: return customer funds, reverse merchant & fee credits
        const int64_t fee = (updated.amount * 2) / 100;
        const int64_t merchantSettlement = updated.amount - fee;

        std::vector<LedgerEntry> entries;
        entries.push_back({ "", id, "escrow:customer",    EntryType::CREDIT, updated.amount,     updated.currency, "Customer refund credit",       "" });
        entries.push_back({ "", id, "merchant:settlement", EntryType::DEBIT,  merchantSettlement, updated.currency, "Reversal of merchant settlement", "" });
        if (fee > 0)
            entries.push_back({ "", id, "platform:fee",        EntryType::DEBIT,  fee,                updated.currency, "Reversal of platform fee",       "" });

        std::string ledgerErr;
        ledger.postTransaction("ref_" + id, entries, ledgerErr);
    }

    return makeJsonResponse(200, "OK", paymentToJson(updated), req.keepAlive);
}

// GET /ledger
// Returns complete immutable double-entry journal entries.
static std::string handleListLedger(const HttpRequest& req, Ledger& ledger)
{
    const auto entries = ledger.getAllEntries();
    std::ostringstream j;
    j << "[\n";
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (i > 0) j << ",\n";
        j << ledgerEntryToJson(entries[i]);
    }
    j << "\n]";
    return makeJsonResponse(200, "OK", j.str(), req.keepAlive);
}

// GET /accounts
// Returns all active ledger accounts and their current net balances.
static std::string handleListAccounts(const HttpRequest& req, Ledger& ledger)
{
    const auto accounts = ledger.getAccounts();
    std::ostringstream j;
    j << "[\n";
    for (std::size_t i = 0; i < accounts.size(); ++i) {
        if (i > 0) j << ",\n";
        const int64_t bal = ledger.getNetBalance(accounts[i]);
        j << "{\n";
        j << "  \"account_id\": \"" << jsonEscape(accounts[i]) << "\",\n";
        j << "  \"net_balance\": "   << bal                     << ",\n";
        j << "  \"currency\": \"INR\"\n";
        j << "}";
    }
    j << "\n]";
    return makeJsonResponse(200, "OK", j.str(), req.keepAlive);
}

// GET /accounts/<id>
// Returns balance and transaction entry history for a specific account.
static std::string handleGetAccount(const HttpRequest& req, const std::string& pathParam, Ledger& ledger)
{
    if (pathParam.empty())
        return makeErrorResponse(400, "Bad Request", "Missing account ID in path.", req.keepAlive);

    const int64_t bal = ledger.getNetBalance(pathParam);
    const auto entries = ledger.getEntriesForAccount(pathParam);

    std::ostringstream j;
    j << "{\n";
    j << "  \"account_id\": \"" << jsonEscape(pathParam) << "\",\n";
    j << "  \"net_balance\": "  << bal                   << ",\n";
    j << "  \"currency\": \"INR\",\n";
    j << "  \"entries\": [\n";
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (i > 0) j << ",\n";
        j << ledgerEntryToJson(entries[i]);
    }
    j << "\n  ]\n}";
    return makeJsonResponse(200, "OK", j.str(), req.keepAlive);
}

// ============================================================
//  Route registration — called once from main()
// ============================================================

void registerPaymentRoutes(Router& router, PaymentStore& store, Ledger& ledger)
{
    // POST /payments  — create a new payment
    router.post("/payments", [&store](const HttpRequest& req, const std::string& param) {
        return handleCreatePayment(req, param, store);
    });

    // GET /payments  — list all payments
    router.get("/payments", [&store](const HttpRequest& req, const std::string& param) {
        return handleListPayments(req, param, store);
    });

    // GET /payments/<id>  — get a specific payment
    router.getP("/payments/", [&store](const HttpRequest& req, const std::string& param) {
        return handleGetPayment(req, param, store);
    });

    // POST /payments/<id>/capture|cancel|refund  — lifecycle actions + ledger posting
    router.postP("/payments/", [&store, &ledger](const HttpRequest& req, const std::string& param) {
        return handlePaymentAction(req, param, store, ledger);
    });

    // --- Double-Entry Ledger & Account Routes ---

    // GET /ledger  — view full immutable audit journal
    router.get("/ledger", [&ledger](const HttpRequest& req, const std::string& /*param*/) {
        return handleListLedger(req, ledger);
    });

    // GET /accounts  — list all account balances
    router.get("/accounts", [&ledger](const HttpRequest& req, const std::string& /*param*/) {
        return handleListAccounts(req, ledger);
    });

    // GET /accounts/<id>  — view specific account balance and entry history
    router.getP("/accounts/", [&ledger](const HttpRequest& req, const std::string& param) {
        return handleGetAccount(req, param, ledger);
    });
}
