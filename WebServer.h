#pragma once

#include "TcpListener.h"
#include "Router.h"

#include <string>
#include <unordered_map>

class WebServer : public TcpListener
{
public:
	WebServer(std::string ipAddress, int port);

	// Expose the router so main() can register routes before calling run().
	Router& router() { return m_router; }

protected:
	void onClientDisconnected(SOCKET clientSocket) override;
	void onMessageReceived(SOCKET clientSocket, const char* message, int length) override;

private:
	std::unordered_map<SOCKET, std::string> m_requestBuffers;
	Router m_router;
};
