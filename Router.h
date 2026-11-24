#pragma once

#include "HttpRequest.h"

#include <functional>
#include <string>
#include <vector>

// A handler receives the full request and the extracted path parameter (empty if none).
// It must return a complete HTTP response string (headers + body).
using RouteHandler = std::function<std::string(const HttpRequest&, const std::string& pathParam)>;

struct Route
{
    std::string method;      // "GET", "POST", etc.
    std::string path;        // e.g. "/payments" or "/payments/"
    bool        prefix;      // if true: match anything that starts with path
    RouteHandler handler;
};

class Router
{
public:
    // Register exact routes
    void get (const std::string& path, RouteHandler h);
    void post(const std::string& path, RouteHandler h);
    void put (const std::string& path, RouteHandler h);

    // Register prefix routes (path param is whatever comes after path)
    void getP (const std::string& path, RouteHandler h);  // GET  /path/:param
    void postP(const std::string& path, RouteHandler h);  // POST /path/:param/action

    // Returns "" if nothing matched (caller falls through to static-file serving)
    std::string dispatch(const HttpRequest& req) const;

private:
    std::vector<Route> routes_;
    void add(const std::string& method, const std::string& path, bool prefix, RouteHandler h);
};
