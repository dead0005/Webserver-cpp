#pragma once

#include <string>
#include <unordered_map>

// A parsed HTTP request. Populated by WebServer after reading a full request off the wire.
struct HttpRequest
{
    std::string method;    // GET, POST, PUT, DELETE ...
    std::string target;    // raw URL path, e.g.  /payments/txn_123
    std::string version;   // HTTP/1.0 or HTTP/1.1
    std::string body;

    // All header names are lowercased for easy lookup.
    // e.g.  req.headers["content-type"],  req.headers["idempotency-key"]
    std::unordered_map<std::string, std::string> headers;

    // Derived: true when the client asked for keep-alive AND we're on HTTP/1.1
    bool keepAlive = false;
};
