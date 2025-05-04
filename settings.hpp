#pragma once

#include <windows.h>

// Enum for API type
enum APIType
{
    API_OPENAI = 0,
    API_CUSTOM = 1
};

void ShowSettingsDialog(HWND hParent);

// Sets the values below
void LoadSettingsFromRegistry();

// Return the relevant global vars
char* GetOpenAIToken();
char* GetCustomEndpoint();
APIType GetAPIType();
char* GetPromptText();
