#include "WebServer.h"
#include "PaymentHandler.h"
#include "PaymentStore.h"

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

	// The store holds all in-memory payment data.
	// It outlives the server (declared before webServer so it's destroyed last).
	PaymentStore store;

	WebServer webServer(bindAddress, port);

	// Wire up the payment API routes onto the server's router.
	registerPaymentRoutes(webServer.router(), store);

	std::cout << "PayCore starting on " << bindAddress << ":" << port << "\n";
	std::cout << "Endpoints: POST /payments  GET /payments  GET /payments/<id>\n";
	std::cout << "           POST /payments/<id>/capture|cancel|refund\n";

	if (webServer.init() != 0)
		return 1;

	return webServer.run() == 0 ? 0 : 1;
}

