# PayCore — C++ Payment Processing & Ledger Engine

A lightweight, concurrent payment processing, double-entry transaction, and banking simulation engine built in modern C++ on top of a custom Winsock2 `select()` HTTP server.

Designed from scratch without third-party web frameworks, demonstrating systems programming, networking, financial state consistency, idempotency, double-entry accounting guarantees, and payment provider fault-tolerance.

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
- **Thread-Safe Architecture**: Guarded with Win32 `CRITICAL_SECTION` RAII primitives.
- **Extensible Request Router**: Dynamic exact and prefix path routing with method matching and keep-alive connection support.

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
                    /              |              \
                   v               v               v
        +----------------+ +----------------+ +--------------------+
        |  PaymentStore  | |  Ledger Engine | | Provider Simulator |
        | State Machine  | |  Double-Entry  | | Bank Authorize /   |
        |  Idempotency   | |  Debit=Credit  | | Decline / Timeout  |
        +----------------+ +----------------+ +--------------------+
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

## 🏦 Payment Provider Simulator (Fault Injection)

Real-world payment gateways encounter network drops, timeouts, and cardholder declines. PayCore includes an internal simulated acquirer controllable via the `X-Mock-Action` header:

| `X-Mock-Action` Value | HTTP Status | Provider Outcome | State Transition |
|---|---|---|---|
| *(none / empty)* | `201 Created` | Clean approval (`AUTH_XXXXXX`) | `INITIATED -> PROCESSING -> AUTHORIZED` |
| `decline` / `insufficient_funds` | `402 Payment Required` | Declines transaction with `INSUFFICIENT_FUNDS` | `INITIATED -> PROCESSING -> FAILED` |
| `fraud` | `402 Payment Required` | Declines with `SUSPECTED_FRAUD_DECLINE` | `INITIATED -> PROCESSING -> FAILED` |
| `timeout` | `504 Gateway Timeout` | Simulates bank latency drop | `INITIATED -> PROCESSING -> FAILED` |
| `network_error` | `502 Bad Gateway` | Simulates dropped socket connection | `INITIATED -> PROCESSING -> FAILED` |
| `delay` | `201 Created` | Simulates 200ms bank latency before approval | `INITIATED -> PROCESSING -> AUTHORIZED` |

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

All payment and accounting endpoints require the header `X-Api-Key: <your-key>`.

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

---

## 🛠️ Build and Run

### Run the Standalone Server
```powershell
.\server.exe 127.0.0.1 8080
```

### Run Invariant & Fault-Tolerance Verification Suites
```powershell
# Phase 2 Invariants: State machine, Idempotency, Double-entry arithmetic
.\tests\test_phase2.exe

# Phase 3 Invariants: Provider simulator, Decline/Timeout/Fraud handling
.\tests\test_phase3.exe
```

---

## 🧪 Testing with curl

### 1. Test API Key Protection (Missing Key returns 401)
```powershell
curl.exe -i http://127.0.0.1:8080/payments
```

### 2. Create an Approved Payment
```powershell
curl.exe -i -X POST http://127.0.0.1:8080/payments `
  -H "X-Api-Key: pk_test_live123" `
  -H "Content-Type: application/json" `
  -H "Idempotency-Key: demo-key-101" `
  -d '{\"amount\": 50000, \"currency\": \"INR\", \"description\": \"Approved Order\"}'
```

### 3. Test Bank Decline Simulation
```powershell
curl.exe -i -X POST http://127.0.0.1:8080/payments `
  -H "X-Api-Key: pk_test_live123" `
  -H "Content-Type: application/json" `
  -H "Idempotency-Key: demo-key-102" `
  -H "X-Mock-Action: decline" `
  -d '{\"amount\": 50000, \"currency\": \"INR\", \"description\": \"Declined Order\"}'
```
*(Returns `402 Payment Required` with `decline_reason: "INSUFFICIENT_FUNDS"` and payment status marked `FAILED`).*

### 4. Test Bank Timeout Simulation
```powershell
curl.exe -i -X POST http://127.0.0.1:8080/payments `
  -H "X-Api-Key: pk_test_live123" `
  -H "Content-Type: application/json" `
  -H "Idempotency-Key: demo-key-103" `
  -H "X-Mock-Action: timeout" `
  -d '{\"amount\": 50000, \"currency\": \"INR\", \"description\": \"Timeout Order\"}'
```
*(Returns `504 Gateway Timeout` and marks payment `FAILED`).*

### 5. Capture Payment & Inspect Double-Entry Ledger
```powershell
# Capture
curl.exe -i -X POST http://127.0.0.1:8080/payments/<txn_id>/capture `
  -H "X-Api-Key: pk_test_live123"

# View balanced journal
curl.exe -i http://127.0.0.1:8080/ledger -H "X-Api-Key: pk_test_live123"

# View account balances
curl.exe -i http://127.0.0.1:8080/accounts -H "X-Api-Key: pk_test_live123"
```
