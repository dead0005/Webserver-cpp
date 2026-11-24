# PayCore — C++ Payment Processing Engine

A lightweight, concurrent payment processing and transaction management engine built in C++ on top of a custom Winsock2 `select()` HTTP server.

Designed from scratch without third-party web frameworks, demonstrating systems programming, networking, financial state consistency, and idempotency guarantees.

---

## 🚀 Key Features

- **Built from Scratch**: Native Windows Sockets (`ws2_32`) using non-blocking `select()` I/O multiplexing. No Boost.Asio, no external HTTP frameworks.
- **Financial Minor-Unit Precision**: Monetary values are tracked strictly in integer minor units (paise for INR; e.g. ₹500.00 = `50000` paise). **Never floating-point arithmetic**.
- **Deterministic Payment State Machine**: Explicit, guarded transitions with an immutable status history audit trail.
- **Idempotency Engine**: Supports client-supplied `Idempotency-Key` headers.
  - Safe retries return identical cached responses (`200 OK`).
  - Key reuse with conflicting transaction payloads is rejected (`409 Conflict`).
- **Thread-Safe In-Memory Ledger & Store**: Guarded with Win32 `CRITICAL_SECTION` primitives (compatible with multi-threaded runtimes).
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
                                   |
                                   v
                +---------------------------------------+
                |             PaymentStore              |
                |  State Machine / Idempotency / Mutex  |
                +---------------------------------------+
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

Any illegal transition (e.g. attempting to cancel an already refunded or settled payment) is rejected with `HTTP 422 Unprocessable Entity`.

---

## 📡 API Endpoints

### 1. Health Status
- **Method**: `GET`
- **Path**: `/api/status`
- **Response**: `200 OK`
  ```json
  {"status":"running","server":"PayCore C++ server"}
  ```

---

### 2. Create Payment (Idempotent)
- **Method**: `POST`
- **Path**: `/payments`
- **Headers**:
  - `Content-Type: application/json`
  - `Idempotency-Key: <unique-client-key>` *(Mandatory)*
- **Request Body**:
  ```json
  {
    "amount": 50000,
    "currency": "INR",
    "description": "Invoice #1042"
  }
  ```
- **Response**: `201 Created`
  ```json
  {
    "id": "txn_1795524241_8b5e09",
    "idempotency_key": "test-key-001",
    "amount": 50000,
    "currency": "INR",
    "description": "Invoice #1042",
    "status": "AUTHORIZED",
    "created_at": "2026-11-24T12:44:01Z",
    "updated_at": "2026-11-24T12:44:01Z",
    "history": [
      "INITIATED @ 2026-11-24T12:44:01Z",
      "PROCESSING @ 2026-11-24T12:44:01Z",
      "AUTHORIZED @ 2026-11-24T12:44:01Z"
    ]
  }
  ```
- **Duplicate Key (Identical Body)**: Returns `200 OK` with the original cached record.
- **Conflicting Key (Different Body)**: Returns `409 Conflict`.

---

### 3. List All Payments
- **Method**: `GET`
- **Path**: `/payments`
- **Response**: `200 OK` (JSON Array of all transactions).

---

### 4. Get Payment by ID
- **Method**: `GET`
- **Path**: `/payments/<transaction_id>`
- **Response**: `200 OK` or `404 Not Found`.

---

### 5. Lifecycle Actions (Capture, Cancel, Refund)
- **Capture**: `POST /payments/<transaction_id>/capture`
- **Cancel**: `POST /payments/<transaction_id>/cancel`
- **Refund**: `POST /payments/<transaction_id>/refund`
- **Response**: `200 OK` with updated status and audit history, or `422 Unprocessable Entity` if illegal.

---

## 🛠️ Build and Run

### Requirements
- Windows 10 / 11
- MinGW GCC (or MSVC)
- CMake 3.16+ (optional)

### Build with MinGW (Direct GCC Command)
```powershell
g++ -std=c++14 -O2 main.cpp TcpListener.cpp WebServer.cpp Router.cpp Payment.cpp PaymentStore.cpp PaymentHandler.cpp -o server.exe -lws2_32
```

### Build with CMake
```powershell
cmake -B build
cmake --build build --config Release
```

### Start the Server
```powershell
.\server.exe 127.0.0.1 8080
```

---

## 🧪 Testing with curl

```powershell
# 1. Create a payment
curl.exe -i -X POST http://127.0.0.1:8080/payments `
  -H "Content-Type: application/json" `
  -H "Idempotency-Key: demo-key-001" `
  -d '{\"amount\": 25000, \"currency\": \"INR\", \"description\": \"Demo\"}'

# 2. Test Idempotency (repeating the exact command returns 200 OK with identical payload)
curl.exe -i -X POST http://127.0.0.1:8080/payments `
  -H "Content-Type: application/json" `
  -H "Idempotency-Key: demo-key-001" `
  -d '{\"amount\": 25000, \"currency\": \"INR\", \"description\": \"Demo\"}'

# 3. Capture the transaction
curl.exe -i -X POST http://127.0.0.1:8080/payments/<transaction_id>/capture

# 4. Refund the transaction
curl.exe -i -X POST http://127.0.0.1:8080/payments/<transaction_id>/refund
```
