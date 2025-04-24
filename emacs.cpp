#include "emacs.hpp"

#include <windows.h>

#include <fstream>
#include <sstream>
#include <iostream>

// Reads info about the currently running Emacs server from the file system.
ConnectionInfo ReadEmacsConnectionInfo()
{
    ConnectionInfo connInfo;
    std::ifstream file("C:\\Users\\Simon\\AppData\\Roaming\\.emacs.d\\server\\server");
    if (!file.is_open())
    {
        std::cerr << "Error opening file\n";
        return connInfo;  // Returns an empty struct if file opening fails
    }

    std::string line;
    if (getline(file, line))
    {
        std::istringstream iss(line);
        getline(iss, connInfo.ip, ':');  // Read IP
        std::string portStr;
        getline(iss, portStr, '\n');  // Read port
        connInfo.port = std::stoi(portStr);
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

// Evaluates an Emacs Lisp expression via sending it to Emacs through the emacsclient protocol.
void InvokeEmacs(const ConnectionInfo& connInfo, const std::string& invocation)
{
    WSADATA wsaData;
    SOCKET s;
    struct sockaddr_in server;

    // Initialize Winsock
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        std::cerr << "Failed. Error Code : " << WSAGetLastError() << std::endl;
        return;
    }

    // Create socket
    if ((s = socket(AF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET)
    {
        std::cerr << "Could not create socket : " << WSAGetLastError() << std::endl;
        WSACleanup();
        return;
    }

    server.sin_addr.s_addr = inet_addr(connInfo.ip.c_str());
    server.sin_family = AF_INET;
    server.sin_port = htons(connInfo.port);

    // Connect to remote server
    if (connect(s, (struct sockaddr*)&server, sizeof(server)) < 0)
    {
        std::cerr << "Connect error.\n";
        closesocket(s);
        WSACleanup();
        return;
    }



    std::string message =
        "-auth " + connInfo.authString + " -current-frame -eval " + EmacsQuote(invocation) + "\r\n";
    std::cout << "sendimg message: " << message << std::endl;
    send(s, message.c_str(), message.length(), 0);

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
    std::cout << "got emacs port: " << info.port << std::endl;

    // ... we'll need to escape this too
    InvokeEmacs(
        info, "(with-current-buffer (window-buffer (selected-window)) (insert \"" + text + "\"))");
}

