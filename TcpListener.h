#pragma once

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif

#include <winsock2.h>
#include <ws2tcpip.h>

#include <chrono>
#include <map>
#include <string>

#ifdef _MSC_VER
#pragma comment(lib, "ws2_32.lib")
#endif

class TcpListener
{
public:
	TcpListener(std::string ipAddress, int port);
	virtual ~TcpListener();

	TcpListener(const TcpListener&) = delete;
	TcpListener& operator=(const TcpListener&) = delete;

	int init();
	int run();

protected:
	virtual void onClientConnected(SOCKET clientSocket);
	virtual void onClientDisconnected(SOCKET clientSocket);
	virtual void onMessageReceived(SOCKET clientSocket, const char* message, int length);

	bool sendToClient(SOCKET clientSocket, const std::string& message);
	void closeClient(SOCKET clientSocket);

private:
	void closeAllSockets();
	void cleanupWinsock();

	std::string m_ipAddress;
	int m_port;
	SOCKET m_socket;
	fd_set m_master;
	std::map<SOCKET, std::chrono::steady_clock::time_point> m_lastActivity;
	bool m_winsockInitialized;
};
