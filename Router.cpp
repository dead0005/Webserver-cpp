#include "Router.h"

void Router::add(const std::string& method, const std::string& path, bool prefix, RouteHandler h)
{
    routes_.push_back({ method, path, prefix, std::move(h) });
}

void Router::get (const std::string& p, RouteHandler h) { add("GET",  p, false, std::move(h)); }
void Router::post(const std::string& p, RouteHandler h) { add("POST", p, false, std::move(h)); }
void Router::put (const std::string& p, RouteHandler h) { add("PUT",  p, false, std::move(h)); }
void Router::getP (const std::string& p, RouteHandler h){ add("GET",  p, true,  std::move(h)); }
void Router::postP(const std::string& p, RouteHandler h){ add("POST", p, true,  std::move(h)); }

std::string Router::dispatch(const HttpRequest& req) const
{
    // Strip query string from path before matching
    const std::string path = req.target.substr(0, req.target.find('?'));

    for (const auto& route : routes_)
    {
        if (route.method != req.method)
            continue;

        if (route.prefix)
        {
            // Prefix route: path must start with route.path
            if (path.rfind(route.path, 0) == 0)
            {
                // Everything after the prefix is the path param (e.g. the txn ID or action)
                const std::string param = path.substr(route.path.size());
                return route.handler(req, param);
            }
        }
        else
        {
            // Exact route
            if (path == route.path)
                return route.handler(req, "");
        }
    }

    return "";  // no match — caller will serve static files or return 404
}
