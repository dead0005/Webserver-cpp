#include "WebServer.h"
#include "PaymentHandler.h"
#include "PaymentStore.h"
#include "Ledger.h"

#include <cstdlib>
#include <iostream>
#include <limits>

int main(int argc, char* argv[])
{
	if (argc > 3)
	{
		std::cerr << "Usage: server.exe [bind-address] [port]\n";
		return 2;
	}

	const std::string bindAddress = argc >= 2 ? argv[1] : "127.0.0.1";
	int port = 8080;
	if (argc == 3)
	{
		char* end = nullptr;
		const long parsedPort = std::strtol(argv[2], &end, 10);
		if (end == argv[2] || *end != '\0' || parsedPort < 1 ||
			parsedPort > std::numeric_limits<unsigned short>::max())
		{
			std::cerr << "Port must be an integer from 1 to 65535.\n";
			return 2;
		}
		port = static_cast<int>(parsedPort);
	}

	// In-memory payment store and double-entry ledger.
	// Both outlive the webServer.
	PaymentStore store;
	Ledger ledger;

	WebServer webServer(bindAddress, port);

	// Wire up payment and accounting routes onto the server's router.
	registerPaymentRoutes(webServer.router(), store, ledger);

	std::cout << "PayCore starting on " << bindAddress << ":" << port << "\n";
	std::cout << "Endpoints: POST /payments  GET /payments  GET /payments/<id>\n";
	std::cout << "           POST /payments/<id>/capture|cancel|refund\n";
	std::cout << "           GET  /ledger    GET /accounts  GET /accounts/<id>\n";

	if (webServer.init() != 0)
		return 1;

	return webServer.run() == 0 ? 0 : 1;
}

