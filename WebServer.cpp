#include "WebServer.h"
#include "HttpRequest.h"   // shared request struct used by Router and handlers

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <utility>
#include <windows.h>   // Win32 file I/O — avoids <filesystem> (not in GCC 6.3)

// ============================================================
//  Anonymous namespace — private helpers used only in this file
// ============================================================
namespace
{
	constexpr std::size_t MAX_HEADER_SIZE        = 16 * 1024;
	constexpr std::size_t MAX_REQUEST_BODY_SIZE  = 1024 * 1024;
	constexpr std::uintmax_t MAX_STATIC_FILE_SIZE = 8 * 1024 * 1024;

	enum class ParseResult
	{
		Incomplete,
		Complete,
		BadRequest,
		HeaderTooLarge,
		PayloadTooLarge,
		UnsupportedTransferEncoding,
		ExpectationFailed
	};

	// Internal request struct — lives only inside WebServer.cpp.
	// The public HttpRequest (from HttpRequest.h) is built separately for the Router.
	struct InternalRequest
	{
		std::string method;
		std::string target;
		std::string version;
		std::string body;
	};

	std::string toLowerAscii(std::string value)
	{
		for (char& character : value)
		{
			const unsigned char byte = static_cast<unsigned char>(character);
			character = static_cast<char>(std::tolower(byte));
		}
		return value;
	}

	std::string trim(std::string value)
	{
		const std::size_t first = value.find_first_not_of(" \t");
		if (first == std::string::npos)
			return {};

		const std::size_t last = value.find_last_not_of(" \t");
		return value.substr(first, last - first + 1);
	}

	bool isTokenCharacter(unsigned char character)
	{
		if (std::isalnum(character) != 0)
			return true;

		switch (character)
		{
		case '!': case '#': case '$': case '%': case '&': case '\'':
		case '*': case '+': case '-': case '.': case '^': case '_':
		case '`': case '|': case '~':
			return true;
		default:
			return false;
		}
	}

	bool parseUnsignedDecimal(const std::string& value, std::size_t& parsed)
	{
		if (value.empty())
			return false;

		parsed = 0;
		for (const char rawCharacter : value)
		{
			const unsigned char character = static_cast<unsigned char>(rawCharacter);
			if (character < '0' || character > '9')
				return false;

			const std::size_t digit = static_cast<std::size_t>(character - '0');
			if (parsed > (std::numeric_limits<std::size_t>::max() - digit) / 10)
				return false;

			parsed = parsed * 10 + digit;
		}
		return true;
	}

	ParseResult parseRequest(const std::string& data, InternalRequest& request)
	{
		const std::size_t headerEnd = data.find("\r\n\r\n");
		if (headerEnd == std::string::npos)
			return data.size() > MAX_HEADER_SIZE ? ParseResult::HeaderTooLarge : ParseResult::Incomplete;

		if (headerEnd + 4 > MAX_HEADER_SIZE)
			return ParseResult::HeaderTooLarge;

		const std::size_t firstLineEnd = data.find("\r\n");
		if (firstLineEnd == std::string::npos || firstLineEnd > headerEnd)
			return ParseResult::BadRequest;

		std::istringstream requestLine(data.substr(0, firstLineEnd));
		std::string extra;
		if (!(requestLine >> request.method >> request.target >> request.version) ||
			(requestLine >> extra))
		{
			return ParseResult::BadRequest;
		}

		if (request.method.empty() ||
			!std::all_of(request.method.begin(), request.method.end(),
				[](unsigned char value) { return isTokenCharacter(value); }) ||
			request.target.empty() || request.target.front() != '/' ||
			request.target.find('#') != std::string::npos)
		{
			return ParseResult::BadRequest;
		}

		if (request.version != "HTTP/1.0" && request.version != "HTTP/1.1")
			return ParseResult::BadRequest;

		bool hasHost              = false;
		bool hasContentLength     = false;
		bool hasTransferEncoding  = false;
		bool hasExpectation       = false;
		std::size_t contentLength = 0;

		std::size_t lineStart = firstLineEnd + 2;
		while (lineStart < headerEnd)
		{
			const std::size_t lineEnd = data.find("\r\n", lineStart);
			if (lineEnd == std::string::npos || lineEnd > headerEnd || lineEnd == lineStart)
				return ParseResult::BadRequest;

			const std::string line = data.substr(lineStart, lineEnd - lineStart);
			const std::size_t colon = line.find(':');
			if (colon == std::string::npos || colon == 0)
				return ParseResult::BadRequest;

			const std::string name = line.substr(0, colon);
			if (!std::all_of(name.begin(), name.end(),
				[](unsigned char value) { return isTokenCharacter(value); }))
			{
				return ParseResult::BadRequest;
			}

			const std::string value = trim(line.substr(colon + 1));
			for (const char rawCharacter : value)
			{
				const unsigned char character = static_cast<unsigned char>(rawCharacter);
				if ((character < 0x20 && character != '\t') || character == 0x7f)
					return ParseResult::BadRequest;
			}

			const std::string lowerName = toLowerAscii(name);
			if (lowerName == "host")
			{
				if (hasHost || value.empty())
					return ParseResult::BadRequest;
				hasHost = true;
			}
			else if (lowerName == "content-length")
			{
				if (hasContentLength || !parseUnsignedDecimal(value, contentLength))
					return ParseResult::BadRequest;
				hasContentLength = true;
			}
			else if (lowerName == "transfer-encoding")
			{
				hasTransferEncoding = true;
			}
			else if (lowerName == "expect")
			{
				hasExpectation = true;
			}

			lineStart = lineEnd + 2;
		}

		if (request.version == "HTTP/1.1" && !hasHost)
			return ParseResult::BadRequest;
		if (hasExpectation)
			return ParseResult::ExpectationFailed;
		if (hasTransferEncoding)
			return ParseResult::UnsupportedTransferEncoding;
		if (contentLength > MAX_REQUEST_BODY_SIZE)
			return ParseResult::PayloadTooLarge;

		const std::size_t bodyStart = headerEnd + 4;
		if (contentLength > std::numeric_limits<std::size_t>::max() - bodyStart)
			return ParseResult::PayloadTooLarge;
		if (data.size() < bodyStart + contentLength)
			return ParseResult::Incomplete;

		request.body = data.substr(bodyStart, contentLength);
		return ParseResult::Complete;
	}

	// Decode a percent-encoded URL path.  Rejects path traversal (.. segments).
	bool decodePath(const std::string& target, std::string& decodedPath)
	{
		const std::size_t queryStart = target.find('?');
		const std::string encodedPath = target.substr(0, queryStart);
		decodedPath.clear();

		for (std::size_t index = 0; index < encodedPath.size(); ++index)
		{
			unsigned char value = static_cast<unsigned char>(encodedPath[index]);
			if (value == '%')
			{
				if (index + 2 >= encodedPath.size())
					return false;

				auto hexValue = [](unsigned char digit) -> int
				{
					if (digit >= '0' && digit <= '9') return digit - '0';
					if (digit >= 'a' && digit <= 'f') return digit - 'a' + 10;
					if (digit >= 'A' && digit <= 'F') return digit - 'A' + 10;
					return -1;
				};

				const int high = hexValue(static_cast<unsigned char>(encodedPath[index + 1]));
				const int low  = hexValue(static_cast<unsigned char>(encodedPath[index + 2]));
				if (high < 0 || low < 0)
					return false;

				value = static_cast<unsigned char>((high << 4) | low);
				index += 2;
			}

			if (value == 0 || value < 0x20 || value == 0x7f ||
				value == '\\' || value == ':')
			{
				return false;
			}

			decodedPath.push_back(static_cast<char>(value));
		}

		// Reject . and .. path segments
		std::size_t segmentStart = 1;
		while (segmentStart <= decodedPath.size())
		{
			const std::size_t separator = decodedPath.find('/', segmentStart);
			const std::string segment = decodedPath.substr(
				segmentStart, separator == std::string::npos
					? std::string::npos : separator - segmentStart);

			if (segment == "." || segment == "..")
				return false;
			if (separator == std::string::npos)
				break;
			segmentStart = separator + 1;
		}

		return true;
	}

	std::string mimeTypeFor(const std::string& path)
	{
		// Find the last dot in the path
		const std::size_t dot = path.rfind('.');
		if (dot == std::string::npos) return "application/octet-stream";

		const std::string ext = toLowerAscii(path.substr(dot));
		if (ext == ".html" || ext == ".htm") return "text/html; charset=utf-8";
		if (ext == ".css")  return "text/css; charset=utf-8";
		if (ext == ".js")   return "text/javascript; charset=utf-8";
		if (ext == ".json") return "application/json";
		if (ext == ".txt")  return "text/plain; charset=utf-8";
		if (ext == ".svg")  return "image/svg+xml";
		if (ext == ".png")  return "image/png";
		if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
		if (ext == ".gif")  return "image/gif";
		if (ext == ".ico")  return "image/x-icon";
		return "application/octet-stream";
	}

	std::string httpDate()
	{
		const std::time_t now = std::time(nullptr);
		const std::tm* utc = std::gmtime(&now);
		if (!utc)
			return {};
		std::ostringstream formatted;
		formatted << std::put_time(utc, "%a, %d %b %Y %H:%M:%S GMT");
		return formatted.str();
	}

	std::string makeResponse(int statusCode, const std::string& reason,
		const std::string& contentType, const std::string& body,
		bool headOnly = false, const std::string& extraHeaders = {})
	{
		std::ostringstream response;
		response << "HTTP/1.1 " << statusCode << ' ' << reason << "\r\n";
		response << "Date: " << httpDate() << "\r\n";
		response << "Server: PayCore/0.1\r\n";
		if (statusCode != 204)
		{
			response << "Content-Type: " << contentType << "\r\n";
			response << "Content-Length: " << body.size() << "\r\n";
		}
		response << "Connection: close\r\n";
		response << "X-Content-Type-Options: nosniff\r\n";
		response << "Cache-Control: no-store\r\n";
		response << extraHeaders;
		response << "\r\n";
		if (!headOnly)
			response << body;
		return response.str();
	}

	std::string errorResponse(int statusCode, const std::string& reason,
		const std::string& message, const std::string& extraHeaders = {})
	{
		return makeResponse(statusCode, reason, "text/plain; charset=utf-8",
			message + "\n", false, extraHeaders);
	}

	void suppressResponseBody(std::string& response)
	{
		const std::size_t headerEnd = response.find("\r\n\r\n");
		if (headerEnd != std::string::npos)
			response.resize(headerEnd + 4);
	}

	// ----------------------------------------------------------------
	//  Static file serving using Win32 APIs (no <filesystem> needed)
	// ----------------------------------------------------------------

	// Canonicalize a path: resolve the full absolute path using Win32.
	// Returns "" on failure.
	std::string canonicalize(const std::string& path)
	{
		char buf[MAX_PATH];
		if (GetFullPathNameA(path.c_str(), MAX_PATH, buf, nullptr) == 0)
			return "";
		return buf;
	}

	// Check that 'child' is strictly inside 'parent' (prevents path traversal).
	bool isInsideDir(const std::string& parent, const std::string& child)
	{
		if (child.size() <= parent.size()) return false;
		if (child.substr(0, parent.size()) != parent) return false;
		// Next char after parent must be a separator
		const char sep = child[parent.size()];
		return sep == '\\' || sep == '/';
	}

	bool isRegularFile(const std::string& path)
	{
		const DWORD attr = GetFileAttributesA(path.c_str());
		return attr != INVALID_FILE_ATTRIBUTES &&
		       !(attr & FILE_ATTRIBUTE_DIRECTORY);
	}

	// Read entire file into a string.  Returns false if file is too large or unreadable.
	bool readFile(const std::string& path, std::string& contents)
	{
		HANDLE hFile = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
		                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (hFile == INVALID_HANDLE_VALUE) return false;

		LARGE_INTEGER size;
		if (!GetFileSizeEx(hFile, &size) ||
		    static_cast<std::uintmax_t>(size.QuadPart) > MAX_STATIC_FILE_SIZE)
		{
			CloseHandle(hFile);
			return false;
		}

		contents.resize(static_cast<std::size_t>(size.QuadPart));
		DWORD bytesRead = 0;
		const BOOL ok = ReadFile(hFile, &contents[0],
		                         static_cast<DWORD>(size.QuadPart), &bytesRead, nullptr);
		CloseHandle(hFile);
		return ok && bytesRead == static_cast<DWORD>(size.QuadPart);
	}

} // namespace

// ============================================================
//  WebServer implementation
// ============================================================

WebServer::WebServer(std::string ipAddress, int port)
	: TcpListener(std::move(ipAddress), port)
{
}

void WebServer::onClientDisconnected(SOCKET clientSocket)
{
	m_requestBuffers.erase(clientSocket);
}

void WebServer::onMessageReceived(SOCKET clientSocket, const char* message, int length)
{
	if (length <= 0)
	{
		closeClient(clientSocket);
		return;
	}

	std::string& bufferedRequest = m_requestBuffers[clientSocket];
	if (bufferedRequest.size() > MAX_HEADER_SIZE + MAX_REQUEST_BODY_SIZE -
		static_cast<std::size_t>(length))
	{
		sendToClient(clientSocket,
			errorResponse(413, "Content Too Large", "Request is too large."));
		closeClient(clientSocket);
		return;
	}

	bufferedRequest.append(message, static_cast<std::size_t>(length));

	InternalRequest request;
	const ParseResult parseResult = parseRequest(bufferedRequest, request);
	if (parseResult == ParseResult::Incomplete)
		return;

	if (parseResult != ParseResult::Complete)
	{
		int statusCode = 400;
		std::string reason = "Bad Request";
		std::string messageText = "Malformed HTTP request.";
		if (parseResult == ParseResult::HeaderTooLarge)
		{
			statusCode   = 431;
			reason       = "Request Header Fields Too Large";
			messageText  = "Request headers are too large.";
		}
		else if (parseResult == ParseResult::PayloadTooLarge)
		{
			statusCode   = 413;
			reason       = "Content Too Large";
			messageText  = "Request body is too large.";
		}
		else if (parseResult == ParseResult::UnsupportedTransferEncoding)
		{
			statusCode   = 501;
			reason       = "Not Implemented";
			messageText  = "Transfer-Encoding is not supported.";
		}
		else if (parseResult == ParseResult::ExpectationFailed)
		{
			statusCode   = 417;
			reason       = "Expectation Failed";
			messageText  = "Expect headers are not supported.";
		}

		sendToClient(clientSocket, errorResponse(statusCode, reason, messageText));
		closeClient(clientSocket);
		return;
	}

	std::string path;
	if (!decodePath(request.target, path))
	{
		sendToClient(clientSocket,
			errorResponse(400, "Bad Request", "Invalid request path."));
		closeClient(clientSocket);
		return;
	}

	// ----------------------------------------------------------------
	// Build the public HttpRequest (HttpRequest.h) for the Router.
	// We re-scan the validated header block to populate the headers map.
	// ----------------------------------------------------------------
	HttpRequest publicReq;
	publicReq.method  = request.method;
	publicReq.target  = request.target;
	publicReq.version = request.version;
	publicReq.body    = request.body;

	{
		const std::size_t headerEnd    = bufferedRequest.find("\r\n\r\n");
		const std::size_t firstLineEnd = bufferedRequest.find("\r\n");
		if (firstLineEnd != std::string::npos && headerEnd != std::string::npos)
		{
			std::size_t lineStart = firstLineEnd + 2;
			while (lineStart < headerEnd)
			{
				const std::size_t lineEnd = bufferedRequest.find("\r\n", lineStart);
				if (lineEnd == std::string::npos || lineEnd > headerEnd) break;

				const std::string line = bufferedRequest.substr(lineStart, lineEnd - lineStart);
				const std::size_t colon = line.find(':');
				if (colon != std::string::npos && colon > 0)
				{
					std::string hname = toLowerAscii(line.substr(0, colon));
					std::string hval  = trim(line.substr(colon + 1));
					publicReq.headers[hname] = hval;
				}
				lineStart = lineEnd + 2;
			}
		}

		// Determine keep-alive:
		// Keep-alive is enabled when the client explicitly requests "Connection: keep-alive".
		// Otherwise we close the connection after the response for compatibility with simple clients.
		const auto connIt = publicReq.headers.find("connection");
		const std::string connVal = connIt != publicReq.headers.end()
		                          ? toLowerAscii(connIt->second) : "";

		publicReq.keepAlive = (connVal == "keep-alive");
	}

	// ----------------------------------------------------------------
	// Router — handles /payments/* and any other registered routes.
	// Falls through to static-file / legacy API handling if no match.
	// ----------------------------------------------------------------
	{
		std::string routerResponse = m_router.dispatch(publicReq);
		if (!routerResponse.empty())
		{
			sendToClient(clientSocket, routerResponse);
			if (publicReq.keepAlive)
				bufferedRequest.clear();   // keep connection open for next request
			else
				closeClient(clientSocket);
			return;
		}
	}

	// ----------------------------------------------------------------
	// Legacy API and static file serving (unchanged from original)
	// ----------------------------------------------------------------
	std::string response;
	if (path == "/api/echo")
	{
		if (request.method == "OPTIONS")
		{
			response = makeResponse(204, "No Content", "text/plain", {},
				false, "Allow: POST, OPTIONS\r\n");
		}
		else if (request.method == "POST")
		{
			response = makeResponse(200, "OK", "text/plain; charset=utf-8",
				request.body);
		}
		else
		{
			response = errorResponse(405, "Method Not Allowed",
				"Method is not supported for this resource.",
				"Allow: POST, OPTIONS\r\n");
		}
	}
	else if (path == "/api/status")
	{
		if (request.method == "OPTIONS")
		{
			response = makeResponse(204, "No Content", "text/plain", {},
				false, "Allow: GET, HEAD, OPTIONS\r\n");
		}
		else if (request.method == "GET" || request.method == "HEAD")
		{
			const std::string body =
				"{\"status\":\"running\",\"server\":\"PayCore C++ server\"}";
			response = makeResponse(200, "OK", "application/json", body,
				request.method == "HEAD");
		}
		else
		{
			response = errorResponse(405, "Method Not Allowed",
				"Method is not supported for this resource.",
				"Allow: GET, HEAD, OPTIONS\r\n");
		}
	}
	else if (request.method == "OPTIONS")
	{
		response = makeResponse(204, "No Content", "text/plain", {},
			false, "Allow: GET, HEAD, OPTIONS\r\n");
	}
	else if (request.method != "GET" && request.method != "HEAD")
	{
		response = errorResponse(405, "Method Not Allowed",
			"Method is not supported for this resource.",
			"Allow: GET, HEAD, OPTIONS\r\n");
	}
	else
	{
		// Static file serving using Win32 file APIs
		if (path == "/")
			path = "/index.html";
		else if (!path.empty() && path.back() == '/')
			path += "index.html";

		// Get the canonical path of the wwwroot directory
		const std::string root = canonicalize("wwwroot");
		if (root.empty())
		{
			response = errorResponse(500, "Internal Server Error",
				"Static file directory is unavailable.");
		}
		else
		{
			// Build candidate path: wwwroot + decoded path (with backslashes on Windows)
			std::string relPath = path.substr(1); // strip leading /
			std::replace(relPath.begin(), relPath.end(), '/', '\\');
			const std::string candidate = canonicalize(root + "\\" + relPath);

			if (candidate.empty())
			{
				response = errorResponse(404, "Not Found", "The requested resource was not found.");
			}
			else if (!isInsideDir(root, candidate))
			{
				response = errorResponse(403, "Forbidden", "The requested path is not allowed.");
			}
			else if (!isRegularFile(candidate))
			{
				response = errorResponse(404, "Not Found", "The requested resource was not found.");
			}
			else
			{
				std::string body;
				if (!readFile(candidate, body))
				{
					response = errorResponse(500, "Internal Server Error",
						"Static file could not be read.");
				}
				else
				{
					response = makeResponse(200, "OK", mimeTypeFor(candidate),
						body, request.method == "HEAD");
				}
			}
		}
	}

	if (request.method == "HEAD")
		suppressResponseBody(response);

	if (!sendToClient(clientSocket, response))
		std::cerr << "Response delivery failed; closing client connection.\n";
	closeClient(clientSocket);
}
