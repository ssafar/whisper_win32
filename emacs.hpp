#pragma once

#include <string>

struct ConnectionInfo
{
    std::string ip;
    int port{};
    std::string authString;
};


ConnectionInfo ReadEmacsConnectionInfo();
void InvokeEmacs(const ConnectionInfo& connInfo, const std::string& invocation);

