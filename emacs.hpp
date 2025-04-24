#pragma once

#include <string>

struct ConnectionInfo
{
    std::string ip;
    int port{};
    std::string authString;
};


void InjectTextToEmacs(const std::string& text);
ConnectionInfo ReadEmacsConnectionInfo();
void InvokeEmacs(const ConnectionInfo& connInfo, const std::string& invocation);

