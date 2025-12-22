#include "emacs.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <format>
#include <iostream>
#include <sstream>

// Get the path to the Emacs server file
static std::string GetEmacsServerFilePath()
{
    // Try to get APPDATA environment variable for portability
    char* appdata = nullptr;
    size_t len = 0;
    if (_dupenv_s(&appdata, &len, "APPDATA") == 0 && appdata != nullptr)
    {
        std::string path = std::string(appdata) + "\\.emacs.d\\server\\server";
        free(appdata);
        return path;
    }
    // Fallback to hardcoded path
    return "C:\\Users\\Simon\\AppData\\Roaming\\.emacs.d\\server\\server";
}

// Reads info about the currently running Emacs server from the file system.
ConnectionInfo ReadEmacsConnectionInfo()
{
    ConnectionInfo connInfo;
    std::string serverPath = GetEmacsServerFilePath();
    std::ifstream file(serverPath);
    if (!file.is_open())
    {
        MessageBoxA(NULL,
            std::format("Could not open Emacs server file.\n\n"
                        "Path: {}\n\n"
                        "Make sure Emacs server is running:\n"
                        "  M-x server-start\n\n"
                        "Or add to your init.el:\n"
                        "  (server-start)",
                        serverPath).c_str(),
            "Emacs Server File Not Found",
            MB_OK | MB_ICONERROR);
        return connInfo;  // Returns an empty struct if file opening fails
    }

    std::string line;
    if (getline(file, line))
    {
        std::istringstream iss(line);
        getline(iss, connInfo.ip, ':');  // Read IP
        std::string portStr;
        getline(iss, portStr, '\n');  // Read port
        try
        {
            connInfo.port = std::stoi(portStr);
        }
        catch (const std::exception& e)
        {
            MessageBoxA(NULL,
                std::format("Failed to parse Emacs server file.\n\n"
                            "Path: {}\n"
                            "Line: {}\n"
                            "Error: {}\n\n"
                            "Expected format: IP:PORT",
                            serverPath, line, e.what()).c_str(),
                "Emacs Server File Parse Error",
                MB_OK | MB_ICONERROR);
            file.close();
            return ConnectionInfo{};
        }
    }
    if (getline(file, connInfo.authString))
    {
        // authString is read directly
    }
    file.close();
    return connInfo;
}


// The encoding rules for the emacsclient protocol
std::string EmacsQuote(const std::string& str)
{
    std::ostringstream encoded;

    if (!str.empty() && str[0] == '-')
    {
        encoded << "&";  // ... and we'll append an actual "-" eventually
    }

    for (char c : str)
    {
        switch (c)
        {
        case ' ':
            encoded << "&_";
            break;
        case '\n':
            encoded << "&n";
            break;
        case '&':
            encoded << "&&";
            break;
        default:
            encoded << c;
            break;
        }
    }

    return encoded.str();
}

// Helper to show connection error dialog
static void ShowConnectionError(const std::string& title, const std::string& message)
{
    MessageBoxA(NULL, message.c_str(), title.c_str(), MB_OK | MB_ICONERROR);
}

// Evaluates an Emacs Lisp expression via sending it to Emacs through the emacsclient protocol.
void InvokeEmacs(const ConnectionInfo& connInfo, const std::string& invocation)
{
    WSADATA wsaData;
    SOCKET s = INVALID_SOCKET;

    // Initialize Winsock
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        int err = WSAGetLastError();
        ShowConnectionError("Emacs Connection Failed",
            std::format("WSAStartup failed.\n\nError code: {}", err));
        return;
    }

    // Use getaddrinfo for both IPv4 and IPv6 support
    struct addrinfo hints = {};
    struct addrinfo* result = nullptr;
    hints.ai_family = AF_UNSPEC;  // Allow IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    std::string portStr = std::to_string(connInfo.port);
    int addrResult = getaddrinfo(connInfo.ip.c_str(), portStr.c_str(), &hints, &result);
    if (addrResult != 0)
    {
        ShowConnectionError("Emacs Connection Failed",
            std::format("Failed to resolve address.\n\n"
                        "Host: {}\n"
                        "Port: {}\n"
                        "Error: {} ({})",
                        connInfo.ip, connInfo.port, gai_strerrorA(addrResult), addrResult));
        WSACleanup();
        return;
    }

    // Try each address until we successfully connect
    for (struct addrinfo* ptr = result; ptr != nullptr; ptr = ptr->ai_next)
    {
        s = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
        if (s == INVALID_SOCKET)
        {
            continue;
        }

        if (connect(s, ptr->ai_addr, (int)ptr->ai_addrlen) == 0)
        {
            break;  // Connected successfully
        }

        closesocket(s);
        s = INVALID_SOCKET;
    }

    if (s == INVALID_SOCKET)
    {
        int err = WSAGetLastError();
        std::string authPreview = connInfo.authString.empty()
            ? std::string("(none)")
            : connInfo.authString.substr(0, (std::min)(size_t{8}, connInfo.authString.size()));
        ShowConnectionError("Emacs Connection Failed",
            std::format("Could not connect to Emacs server.\n\n"
                        "Host: {}\n"
                        "Port: {}\n"
                        "Auth: {}...\n\n"
                        "Error code: {}\n\n"
                        "Possible causes:\n"
                        "- Emacs server not running (M-x server-start)\n"
                        "- Firewall blocking connection\n"
                        "- Stale server file (restart Emacs)",
                        connInfo.ip, connInfo.port, authPreview, err));
        freeaddrinfo(result);
        WSACleanup();
        return;
    }

    freeaddrinfo(result);



    std::string message =
        "-auth " + connInfo.authString + " -current-frame -eval " + EmacsQuote(invocation) + "\r\n";
    std::cout << "sendimg message: " << message << std::endl;
    send(s, message.c_str(), static_cast<int>(message.length()), 0);

    // Receive a reply from the server
    char server_reply[2000];
    int recv_size;
    if ((recv_size = recv(s, server_reply, 2000, 0)) == SOCKET_ERROR)
    {
        std::cerr << "recv failed.\n";
    }

    std::cout << "Reply received: " << server_reply << std::endl;

    closesocket(s);
    WSACleanup();
}

void InjectTextToEmacs(const std::string& text)
{
    ConnectionInfo info = ReadEmacsConnectionInfo();

    // Check if we got valid connection info
    if (info.ip.empty() || info.port == 0)
    {
        // Error already shown by ReadEmacsConnectionInfo
        return;
    }

    std::cout << "got emacs port: " << info.port << std::endl;

    // ... we'll need to escape this too
    InvokeEmacs(
        info, "(with-current-buffer (window-buffer (selected-window)) (insert \"" + text + "\"))");
}

