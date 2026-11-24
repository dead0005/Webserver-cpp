#include "WebServer.h"
#include "PaymentHandler.h"
#include "PaymentStore.h"
#include "Ledger.h"
#include "ProviderSimulator.h"
#include "PersistenceManager.h"

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

	// Core engine components: store, ledger, and banking simulator
	PaymentStore      store;
	Ledger            ledger;
	ProviderSimulator provider;

	// Disaster recovery: hydrate previous state from disk if present
	PersistenceManager::loadState("data/paycore_state.json", store, ledger);

	WebServer webServer(bindAddress, port);

	// Wire up payment, accounting, and banking simulation routes
	registerPaymentRoutes(webServer.router(), store, ledger, provider);

	std::cout << "PayCore starting on " << bindAddress << ":" << port << "\n";
	std::cout << "Endpoints: POST /payments  GET /payments  GET /payments/<id>\n";
	std::cout << "           POST /payments/<id>/capture|cancel|refund\n";
	std::cout << "           GET  /ledger    GET /accounts  GET /accounts/<id>\n";
	std::cout << "           GET  /reconciliation\n";
	std::cout << "Auth:      Required 'X-Api-Key' header on all payment/ledger routes\n";
	std::cout << "Mock Bank: Header 'X-Mock-Action: decline|timeout|network_error|delay'\n";
	std::cout << "Storage:   Atomic snapshots persisted to data/paycore_state.json\n";

	if (webServer.init() != 0)
		return 1;

	return webServer.run() == 0 ? 0 : 1;
}

