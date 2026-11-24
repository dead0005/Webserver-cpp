# PayCore — C++ Payment Processing & Ledger Engine

A lightweight, concurrent payment processing, double-entry transaction, banking simulation, and financial reconciliation engine built in modern C++ on top of a custom Winsock2 `select()` HTTP server.

Designed from scratch without third-party web frameworks, demonstrating systems programming, low-level networking, financial state consistency, idempotency, double-entry accounting guarantees, disaster recovery, and automated reconciliation.

---

## 🚀 Key Features

- **Built from Scratch**: Native Windows Sockets (`ws2_32`) using non-blocking `select()` I/O multiplexing. No Boost.Asio, no external HTTP frameworks.
- **Financial Minor-Unit Precision**: Monetary values are tracked strictly in integer minor units (paise for INR; e.g. ₹500.00 = `50000` paise). **Never floating-point arithmetic**.
- **Deterministic Payment State Machine**: Explicit, guarded transitions with an immutable status history audit trail.
- **Idempotency Engine**: Supports client-supplied `Idempotency-Key` headers.
  - Safe retries return identical cached responses (`200 OK`).
  - Key reuse with conflicting transaction payloads is rejected (`409 Conflict`).
- **Double-Entry Ledger Engine**: Every financial movement creates balanced multi-legged debits and credits ($\sum \text{Debits} == \sum \text{Credits}$).
  - Immutable append-only journal ledger.
  - Automatic 2% platform fee split and merchant settlement crediting on capture.
  - Full atomic reversal on refund.
- **Payment Provider Simulator**: Simulates an upstream banking network with deterministic fault injection via headers (`X-Mock-Action: decline | timeout | fraud | network_error | delay`).
- **API Key Security Middleware**: Rejects unauthorized access with `401 Unauthorized` via `X-Api-Key` headers.
- **Atomic Persistence & Disaster Recovery**:
  - Full state snapshot written atomically via `.tmp` swap (`MoveFileEx` with `MOVEFILE_REPLACE_EXISTING`) to prevent file corruption during sudden server crashes.
  - Automatic startup hydration restores all payments, idempotency keys, and ledger balances on boot.
- **Automated Financial Reconciliation Engine**: Cross-references payments against double-entry ledger legs, catching missing postings, amount discrepancies, and orphan entries via `GET /reconciliation`.
- **Thread-Safe Architecture**: Guarded with Win32 `CRITICAL_SECTION` RAII primitives.

---

## 🏛️ Architecture & Components

```
                +---------------------------------------+
                |           HTTP Client (curl)          |
                +---------------------------------------+
                                   |
                                   v  (TCP / HTTP 1.1)
                +---------------------------------------+
                |        TcpListener (Winsock2)         |
                |   select() Event Loop / Socket I/O    |
                +---------------------------------------+
                                   |
                                   v  (Raw Message Bytes)
                +---------------------------------------+
                |               WebServer               |
                |   HTTP Parser & Keep-Alive Dispatch   |
                +---------------------------------------+
                                   |
                                   v  (HttpRequest struct)
                +---------------------------------------+
                |                Router                 |
                |    Prefix & Exact Method Matching     |
                +---------------------------------------+
                                   |
                                   v
                +---------------------------------------+
                |            PaymentHandler             |
                | API Key Auth, Validation, JSON SerDes |
                +---------------------------------------+
                 /            |             \           \
                v             v              v           v
        +--------------+ +-------------+ +-----------+ +----------------+
        | PaymentStore | |Ledger Engine| |  Provider | | Reconciliation |
        |State Machine | |Double-Entry | | Simulator | | Payment vs     |
        | Idempotency  | |Debit=Credit | |Mock Bank  | | Ledger Audit   |
        +--------------+ +-------------+ +-----------+ +----------------+
                \             /
                 v           v
        +-------------------------------+
        |      PersistenceManager       |
        | Atomic .tmp Snapshot to Disk  |
        |  Hydration on Server Boot     |
        +-------------------------------+
```

---

## 🔄 Payment Lifecycle State Machine

Financial transactions follow a strict, uni-directional state flow:

```
    [INITIATED] ---------> [PROCESSING] ---------> [AUTHORIZED]
                                |                       |
                                v                       +---------> [CANCELLED]
                             [FAILED]                   |
                                                        v
                                                   [CAPTURED]
                                                        |
                                        +---------------+---------------+
                                        |                               |
                                        v                               v
                                    [SETTLED]                       [REFUNDED]
```

---

## ⚖️ Double-Entry Ledger Model

Every settled financial movement records equal and opposite entries across multiple accounts.

### 1. On Payment Capture (e.g. ₹500.00 = 50,000 paise)
- **Platform Fee (2%)**: 1,000 paise
- **Merchant Settlement**: 49,000 paise
- **Journal Entries Posted**:
  - `DEBIT  escrow:customer    50,000 paise` (releases hold on customer funds)
  - `CREDIT merchant:settlement 49,000 paise` (credit payable to merchant)
  - `CREDIT platform:fee         1,000 paise` (credit earned by platform)
- **Invariant**: $\sum \text{Debits} (50,000) == \sum \text{Credits} (49,000 + 1,000)$ &mdash; **Balanced**.

### 2. On Payment Refund
- **Journal Entries Posted**:
  - `CREDIT escrow:customer    50,000 paise` (returns funds to customer)
  - `DEBIT  merchant:settlement 49,000 paise` (reverses merchant settlement)
  - `DEBIT  platform:fee         1,000 paise` (reverses platform fee)
- **Net Balances**: Sums back to zero across all accounts.

---

## 📡 API Endpoints

All payment, ledger, and reconciliation endpoints require the header `X-Api-Key: <your-key>`.

### Payment Endpoints
| Method | Endpoint | Required Headers | Description |
|---|---|---|---|
| `GET` | `/api/status` | *(None)* | Server health check (public) |
| `POST` | `/payments` | `X-Api-Key`, `Idempotency-Key` | Create payment (supports `X-Mock-Action`) |
| `GET` | `/payments` | `X-Api-Key` | List all payment transactions |
| `GET` | `/payments/<id>` | `X-Api-Key` | Get payment details and history |
| `POST` | `/payments/<id>/capture` | `X-Api-Key` | Capture payment & post to ledger |
| `POST` | `/payments/<id>/refund` | `X-Api-Key` | Refund payment & reverse ledger entries |
| `POST` | `/payments/<id>/cancel` | `X-Api-Key` | Void authorized payment |

### Ledger & Account Endpoints
| Method | Endpoint | Required Headers | Description |
|---|---|---|---|
| `GET` | `/ledger` | `X-Api-Key` | View full immutable audit journal |
| `GET` | `/accounts` | `X-Api-Key` | View all active ledger account balances |
| `GET` | `/accounts/<id>` | `X-Api-Key` | View specific account balance and entry history |

### Reconciliation Endpoint
| Method | Endpoint | Required Headers | Description |
|---|---|---|---|
| `GET` | `/reconciliation` | `X-Api-Key` | Runs automated audit between payments and ledger legs |

---

## 🛠️ Verification & Test Suites

PayCore includes dedicated, zero-dependency automated verification test suites:

```powershell
# Phase 2 Invariants: State machine, Idempotency, Double-entry arithmetic
.\tests\test_phase2.exe

# Phase 3 Invariants: Provider simulator, Decline/Timeout/Fraud handling
.\tests\test_phase3.exe

# Phase 4 Invariants: Atomic Persistence, Crash Hydration, Reconciliation
.\tests\test_phase4.exe
```

---

## 🧪 Quick Test with curl

```powershell
# 1. Start the server
.\server.exe 127.0.0.1 8080

# 2. Create a ₹500 payment
curl.exe -i -X POST http://127.0.0.1:8080/payments `
  -H "X-Api-Key: pk_test_live123" `
  -H "Content-Type: application/json" `
  -H "Idempotency-Key: recon-order-01" `
  -d '{\"amount\": 50000, \"currency\": \"INR\", \"description\": \"Audited Order\"}'

# 3. Capture the payment
curl.exe -i -X POST http://127.0.0.1:8080/payments/<txn_id>/capture `
  -H "X-Api-Key: pk_test_live123"

# 4. Run Financial Reconciliation Audit
curl.exe -i http://127.0.0.1:8080/reconciliation -H "X-Api-Key: pk_test_live123"
```
