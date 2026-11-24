#include "TcpListener.h"

#include <algorithm>
#include <iostream>
#include <limits>
#include <utility>
#include <vector>

namespace
{
	volatile LONG shutdownRequested = 0;

	BOOL WINAPI handleConsoleControl(DWORD controlType)
	{
		if (controlType == CTRL_C_EVENT ||
			controlType == CTRL_BREAK_EVENT ||
			controlType == CTRL_CLOSE_EVENT ||
			controlType == CTRL_SHUTDOWN_EVENT)
		{
			InterlockedExchange(&shutdownRequested, 1);
			return TRUE;
		}

		return FALSE;
	}
}

TcpListener::TcpListener(std::string ipAddress, int port)
	: m_ipAddress(std::move(ipAddress)),
	  m_port(port),
	  m_socket(INVALID_SOCKET),
	  m_winsockInitialized(false)
{
	FD_ZERO(&m_master);
}

TcpListener::~TcpListener()
{
	closeAllSockets();
	cleanupWinsock();
}

int TcpListener::init()
{
	if (m_socket != INVALID_SOCKET)
	{
		std::cerr << "init() has already succeeded for this listener.\n";
		return WSAEALREADY;
	}

	WSADATA wsData{};
	const int startupResult = WSAStartup(MAKEWORD(2, 2), &wsData);
	if (startupResult != 0)
	{
		std::cerr << "WSAStartup failed with error " << startupResult << ".\n";
		return startupResult;
	}
	m_winsockInitialized = true;

	m_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (m_socket == INVALID_SOCKET)
	{
		const int error = WSAGetLastError();
		std::cerr << "socket() failed with error " << error << ".\n";
		cleanupWinsock();
		return error;
	}

	BOOL reuseAddress = TRUE;
	if (setsockopt(m_socket, SOL_SOCKET, SO_REUSEADDR,
		reinterpret_cast<const char*>(&reuseAddress), sizeof(reuseAddress)) == SOCKET_ERROR)
	{
		const int error = WSAGetLastError();
		std::cerr << "setsockopt(SO_REUSEADDR) failed with error " << error << ".\n";
		closeAllSockets();
		cleanupWinsock();
		return error;
	}

	sockaddr_in address{};
	address.sin_family = AF_INET;
	std::string addressText = m_ipAddress;
	int addressLength = static_cast<int>(sizeof(address));
	if (addressText.empty())
	{
		std::cerr << "Invalid IPv4 address: " << m_ipAddress << ".\n";
		closeAllSockets();
		cleanupWinsock();
		return WSAEINVAL;
	}

	if (WSAStringToAddressA(&addressText[0], AF_INET, nullptr,
		reinterpret_cast<sockaddr*>(&address), &addressLength) == SOCKET_ERROR)
	{
		const int error = WSAGetLastError();
		std::cerr << "Invalid IPv4 address: " << m_ipAddress
			<< " (error " << error << ").\n";
		closeAllSockets();
		cleanupWinsock();
		return error;
	}
	address.sin_family = AF_INET;
	address.sin_port = htons(static_cast<u_short>(m_port));

	if (bind(m_socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR)
	{
		const int error = WSAGetLastError();
		std::cerr << "bind() failed with error " << error << ".\n";
		closeAllSockets();
		cleanupWinsock();
		return error;
	}

	if (listen(m_socket, SOMAXCONN) == SOCKET_ERROR)
	{
		const int error = WSAGetLastError();
		std::cerr << "listen() failed with error " << error << ".\n";
		closeAllSockets();
		cleanupWinsock();
		return error;
	}

	FD_ZERO(&m_master);
	FD_SET(m_socket, &m_master);

	std::cout << "Listening on http://" << m_ipAddress << ':' << m_port << '\n'
		<< "Press Ctrl+C to stop.\n";
	return 0;
}

int TcpListener::run()
{
	if (m_socket == INVALID_SOCKET)
	{
		std::cerr << "Cannot run before init() succeeds.\n";
		return WSAEINVAL;
	}

	InterlockedExchange(&shutdownRequested, 0);
	if (!SetConsoleCtrlHandler(handleConsoleControl, TRUE))
	{
		const DWORD error = GetLastError();
		std::cerr << "Could not install the console shutdown handler (error "
			<< error << ").\n";
		return static_cast<int>(error);
	}

	int result = 0;
	const auto idleTimeout = std::chrono::seconds(30);

	while (InterlockedCompareExchange(&shutdownRequested, 0, 0) == 0)
	{
		fd_set readable = m_master;
		timeval timeout{};
		timeout.tv_sec = 1;
		const int readyCount = select(0, &readable, nullptr, nullptr, &timeout);
		if (readyCount == SOCKET_ERROR)
		{
			if (InterlockedCompareExchange(&shutdownRequested, 0, 0) != 0)
				break;

			result = WSAGetLastError();
			std::cerr << "select() failed with error " << result << ".\n";
			break;
		}

		if (readyCount > 0)
		{
			for (int index = 0; index < readyCount; ++index)
			{
				const SOCKET readySocket = readable.fd_array[index];
				if (readySocket == m_socket)
				{
					const SOCKET client = accept(m_socket, nullptr, nullptr);
					if (client == INVALID_SOCKET)
					{
						const int error = WSAGetLastError();
						std::cerr << "accept() failed with error " << error << ".\n";
						continue;
					}

					if (m_master.fd_count >= FD_SETSIZE)
					{
						std::cerr << "Connection limit reached; rejecting client.\n";
						closesocket(client);
						continue;
					}

					const DWORD sendTimeoutMs = 5000;
					if (setsockopt(client, SOL_SOCKET, SO_SNDTIMEO,
						reinterpret_cast<const char*>(&sendTimeoutMs),
						sizeof(sendTimeoutMs)) == SOCKET_ERROR)
					{
						std::cerr << "Could not set client send timeout; closing client.\n";
						closesocket(client);
						continue;
					}

					FD_SET(client, &m_master);
					m_lastActivity[client] = std::chrono::steady_clock::now();
					onClientConnected(client);
				}
				else
				{
					char buffer[4096];
					const int bytesReceived = recv(
						readySocket, buffer, static_cast<int>(sizeof(buffer)), 0);

					if (bytesReceived <= 0)
					{
						if (bytesReceived == SOCKET_ERROR)
						{
							std::cerr << "recv() failed with error "
								<< WSAGetLastError() << ".\n";
						}
						closeClient(readySocket);
						continue;
					}

					m_lastActivity[readySocket] = std::chrono::steady_clock::now();
					onMessageReceived(readySocket, buffer, bytesReceived);
				}
			}
		}

		const auto now = std::chrono::steady_clock::now();
		std::vector<SOCKET> expiredClients;
		for (const auto& client : m_lastActivity)
		{
			if (now - client.second > idleTimeout)
				expiredClients.push_back(client.first);
		}

		for (const SOCKET client : expiredClients)
		{
			std::cerr << "Closing idle client connection.\n";
			closeClient(client);
		}
	}

	SetConsoleCtrlHandler(handleConsoleControl, FALSE);
	closeAllSockets();
	cleanupWinsock();
	return result;
}

bool TcpListener::sendToClient(SOCKET clientSocket, const std::string& message)
{
	std::size_t totalSent = 0;
	while (totalSent < message.size())
	{
		const std::size_t remaining = message.size() - totalSent;
		const int chunkSize = static_cast<int>(
			(std::min)(remaining, static_cast<std::size_t>(std::numeric_limits<int>::max())));
		const int sent = send(
			clientSocket, message.data() + totalSent, chunkSize, 0);

		if (sent == SOCKET_ERROR || sent == 0)
		{
			std::cerr << "send() failed with error "
				<< (sent == SOCKET_ERROR ? WSAGetLastError() : 0) << ".\n";
			return false;
		}

		totalSent += static_cast<std::size_t>(sent);
	}

	return true;
}

void TcpListener::closeClient(SOCKET clientSocket)
{
	if (FD_ISSET(clientSocket, &m_master))
	{
		onClientDisconnected(clientSocket);
		FD_CLR(clientSocket, &m_master);
	}

	m_lastActivity.erase(clientSocket);
	closesocket(clientSocket);
}

void TcpListener::closeAllSockets()
{
	while (m_master.fd_count > 0)
	{
		const SOCKET socketToClose = m_master.fd_array[0];
		if (socketToClose != m_socket)
			onClientDisconnected(socketToClose);

		FD_CLR(socketToClose, &m_master);
		closesocket(socketToClose);
		m_lastActivity.erase(socketToClose);
	}

	m_socket = INVALID_SOCKET;
}

void TcpListener::cleanupWinsock()
{
	if (m_winsockInitialized)
	{
		WSACleanup();
		m_winsockInitialized = false;
	}
}

void TcpListener::onClientConnected(SOCKET)
{
}

void TcpListener::onClientDisconnected(SOCKET)
{
}

void TcpListener::onMessageReceived(SOCKET, const char*, int)
{
}
