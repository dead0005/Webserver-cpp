# C++ Web Server

A lightweight, modular HTTP web server written in C++ for Windows using **Winsock2** and non-blocking **`select()` I/O multiplexing**.

## 🚀 Features

- **No Third-Party Dependencies**: Built directly on native Windows Sockets (`ws2_32`).
- **Object-Oriented Architecture**:
  - `TcpListener`: Abstract TCP server base class handling Winsock initialization, socket lifecycle, and the `select()` event loop.
  - `WebServer`: Derived class implementing HTTP/1.1 request parsing, static file reading, and response formatting.
- **I/O Multiplexing**: Uses `select()` to serve multiple client connections concurrently without thread race conditions.
- **Static File Serving**: Serves HTML, CSS, and assets directly from the `wwwroot` directory with automatic 404 handling.

## 📁 Project Structure

```text
├── TcpListener.h       # TCP listener base class header
├── TcpListener.cpp     # Winsock2 setup & select() event loop
├── WebServer.h         # HTTP web server class header
├── WebServer.cpp       # HTTP request parsing and response delivery
├── main.cpp            # Server entry point
├── wwwroot/
│   ├── index.html      # Main landing page
│   └── foo.html        # Embedded iframe document
└── README.md
```

## 🛠️ Build and Run

### Prerequisites
- Windows OS
- MinGW / GCC, Clang, or MSVC with Winsock2 support

### Compilation

Using **MinGW g++**:
```powershell
g++ -std=c++14 -O2 main.cpp TcpListener.cpp WebServer.cpp -o server.exe -lws2_32
```

### Running

```powershell
.\server.exe
```

Open your browser and navigate to:
- `http://localhost:8080/` &mdash; Loads `wwwroot/index.html`
- `http://localhost:8080/foo.html` &mdash; Loads `wwwroot/foo.html`
- `http://localhost:8080/nonexistent.html` &mdash; Displays standard 404 response

