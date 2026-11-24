# PayCore — C++ Payment Processing & Ledger Engine

A lightweight, concurrent payment processing and double-entry transaction engine built in C++ on top of a custom Winsock2 `select()` HTTP server.

Designed from scratch without third-party web frameworks, demonstrating systems programming, networking, financial state consistency, idempotency, and double-entry accounting guarantees.

---

## 🚀 Key Features

- **Built from Scratch**: Native Windows Sockets (`ws2_32`) using non-blocking `select()` I/O multiplexing. No Boost.Asio, no external HTTP frameworks.
- **Financial Minor-Unit Precision**: Monetary values are tracked strictly in integer minor units (paise for INR; e.g. ₹500.00 = `50000` paise). **Never floating-point arithmetic**.
- **Deterministic Payment State Machine**: Explicit, guarded transitions with an immutable status history audit trail.
- **Idempotency Engine**: Supports client-supplied `Idempotency-Key` headers.
  - Safe retries return identical cached responses (`200 OK`).
  - Key reuse with conflicting transaction payloads is rejected (`409 Conflict`).
- **Double-Entry Ledger Engine**: Every financial movement creates balanced multi-legged debits and credits ($\sum \text{Debits} == \sum \text{Credits}$).
  - Immutable journal ledger.
  - Automatic 2% platform fee split and merchant settlement crediting on capture.
  - Full atomic reversal on refund.
- **Thread-Safe In-Memory Architecture**: Guarded with Win32 `CRITICAL_SECTION` RAII primitives.
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
                |   Request Validation & JSON SerDes    |
                +---------------------------------------+
                       |                        |
                       v                        v
        +----------------------------+   +----------------------------+
        |        PaymentStore        |   |       Ledger Engine        |
        | State Machine / Idempotency|   | Double-Entry Journal Batch |
        +----------------------------+   +----------------------------+
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

### Valid Transitions
| Current Status | Allowed Next Statuses | Description |
|---|---|---|
| `INITIATED` | `PROCESSING` | Payment received and queued for execution |
| `PROCESSING` | `AUTHORIZED`, `FAILED` | Provider authorization response |
| `AUTHORIZED` | `CAPTURED`, `CANCELLED` | Capture funds or void the hold |
| `CAPTURED` | `SETTLED`, `REFUNDED` | Final bank settlement or customer refund |
| `SETTLED` | *(Terminal)* | Transaction completed |
| `FAILED` | *(Terminal)* | Authorization declined |
| `CANCELLED` | *(Terminal)* | Payment voided before capture |
| `REFUNDED` | *(Terminal)* | Funds returned to payer |

---

## 📡 API Endpoints

### Payment Endpoints
| Method | Endpoint | Description |
|---|---|---|
| `GET` | `/api/status` | Server health check |
| `POST` | `/payments` | Create payment (requires `Idempotency-Key` header) |
| `GET` | `/payments` | List all payment transactions |
| `GET` | `/payments/<id>` | Get specific payment details and history |
| `POST` | `/payments/<id>/capture` | Capture authorized payment & trigger ledger posting |
| `POST` | `/payments/<id>/refund` | Refund captured payment & trigger ledger reversal |
| `POST` | `/payments/<id>/cancel` | Cancel authorized payment |

### Ledger & Account Endpoints
| Method | Endpoint | Description |
|---|---|---|
| `GET` | `/ledger` | View full immutable audit journal of all debit/credit legs |
| `GET` | `/accounts` | View all active ledger accounts and their net balances |
| `GET` | `/accounts/<id>` | View specific account balance and transaction history |

---

## 🛠️ Build and Run

### MinGW GCC Direct Build
```powershell
g++ -std=c++14 -O2 main.cpp TcpListener.cpp WebServer.cpp Router.cpp Payment.cpp PaymentStore.cpp PaymentHandler.cpp Ledger.cpp -o server.exe -lws2_32
```

### Start the Server
```powershell
.\server.exe 127.0.0.1 8080
```

---

## 🧪 Quick Test with curl

```powershell
# 1. Create a payment
curl.exe -i -X POST http://127.0.0.1:8080/payments `
  -H "Content-Type: application/json" `
  -H "Idempotency-Key: demo-order-01" `
  -d '{\"amount\": 50000, \"currency\": \"INR\", \"description\": \"Order #01\"}'

# 2. Capture the payment (replace with generated ID)
curl.exe -i -X POST http://127.0.0.1:8080/payments/<txn_id>/capture

# 3. View the double-entry ledger journal
curl.exe -i http://127.0.0.1:8080/ledger

# 4. View account balances
curl.exe -i http://127.0.0.1:8080/accounts
```
