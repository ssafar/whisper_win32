#include "settings.hpp"

#include <windows.h>

#include "resource.h"

#include "utils.hpp"

#include <iostream>
#include <string>

// Define the registry path and entry names
#define REGISTRY_PARENT_PATH L"SOFTWARE\\Simon Safar"
#define REGISTRY_PATH L"SOFTWARE\\Simon Safar\\OpenAI Keys"
#define REGISTRY_TOKEN_VALUE L"token"
#define REGISTRY_API_TYPE_VALUE L"api_type"
#define REGISTRY_ENDPOINT_VALUE L"endpoint"
#define REGISTRY_PROMPT_VALUE L"prompt"

// Global variables to hold settings
char g_OpenAIToken[256] = { 0 };
char g_Endpoint[256] = { 0 };
char g_PromptText[1024] = { 0 };
APIType g_APIType = API_OPENAI;

// Add debugging variables
DWORD g_LastRegError = 0;
BOOL g_KeyOpened = FALSE;
BOOL g_TokenLoaded = FALSE;
BOOL g_TypeLoaded = FALSE;
BOOL g_EndpointLoaded = FALSE;
BOOL g_PromptLoaded = FALSE;

char* GetOpenAIToken() { return g_OpenAIToken; }
char* GetCustomEndpoint() { return g_Endpoint; }
APIType GetAPIType() { return g_APIType; }
char* GetPromptText() { return g_PromptText; }

// Function to load settings from the registry
void LoadSettingsFromRegistry()
{
    HKEY hKey;
    DWORD dataSize;

    // Reset debugging variables
    g_LastRegError = 0;
    g_KeyOpened = FALSE;
    g_TokenLoaded = FALSE;
    g_TypeLoaded = FALSE;
    g_EndpointLoaded = FALSE;
    g_PromptLoaded = FALSE;

    // Open the registry key - store error code for debugging
    g_LastRegError = RegOpenKeyExW(HKEY_CURRENT_USER, REGISTRY_PATH, 0, KEY_READ, &hKey);
    if (g_LastRegError == ERROR_SUCCESS)
    {
        g_KeyOpened = TRUE;

        // Load OpenAI token - Unicode to ASCII conversion needed
        wchar_t wideToken[256] = { 0 };
        dataSize = sizeof(wideToken);
        g_LastRegError = RegQueryValueExW(
            hKey, REGISTRY_TOKEN_VALUE, NULL, NULL, (LPBYTE)wideToken, &dataSize);
        if (g_LastRegError == ERROR_SUCCESS)
        {
            // Convert wide string to ASCII
            WideCharToMultiByte(CP_ACP, 0, wideToken, -1, g_OpenAIToken, sizeof(g_OpenAIToken), NULL, NULL);
            g_TokenLoaded = TRUE;
        }
        else
        {
            g_OpenAIToken[0] = '\0';
        }

        // Load API type
        DWORD apiType = API_OPENAI;
        dataSize = sizeof(apiType);
        g_LastRegError = RegQueryValueExW(
            hKey, REGISTRY_API_TYPE_VALUE, NULL, NULL, (LPBYTE)&apiType, &dataSize);
        if (g_LastRegError == ERROR_SUCCESS)
        {
            g_APIType = static_cast<APIType>(apiType);
            g_TypeLoaded = TRUE;
        }

        // Load endpoint - Unicode to ASCII conversion needed
        wchar_t wideEndpoint[256] = { 0 };
        dataSize = sizeof(wideEndpoint);
        g_LastRegError = RegQueryValueExW(
            hKey, REGISTRY_ENDPOINT_VALUE, NULL, NULL, (LPBYTE)wideEndpoint, &dataSize);
        if (g_LastRegError == ERROR_SUCCESS)
        {
            // Convert wide string to ASCII
            WideCharToMultiByte(CP_ACP, 0, wideEndpoint, -1, g_Endpoint, sizeof(g_Endpoint), NULL, NULL);
            g_EndpointLoaded = TRUE;
        }
        else
        {
            g_Endpoint[0] = '\0';
        }

        // Load prompt text - Unicode to ASCII conversion needed
        wchar_t widePrompt[1024] = { 0 };
        dataSize = sizeof(widePrompt);
        g_LastRegError = RegQueryValueExW(
            hKey, REGISTRY_PROMPT_VALUE, NULL, NULL, (LPBYTE)widePrompt, &dataSize);
        if (g_LastRegError == ERROR_SUCCESS)
        {
            // Convert wide string to ASCII
            WideCharToMultiByte(CP_ACP, 0, widePrompt, -1, g_PromptText, sizeof(g_PromptText), NULL, NULL);
            g_PromptLoaded = TRUE;
        }
        else
        {
            g_PromptText[0] = '\0';
        }

        RegCloseKey(hKey);
    }
    else
    {
        // If the key doesn't exist, initialize default values
        g_OpenAIToken[0] = '\0';
        g_APIType = API_OPENAI;
        g_Endpoint[0] = '\0';
        g_PromptText[0] = '\0';
    }
}

// Function to save settings to the registry
void SaveSettingsToRegistry()
{
    HKEY hKey;
    DWORD disposition;
    DWORD lastError = 0;

    // Create the parent path first if needed
    lastError = RegCreateKeyExW(HKEY_CURRENT_USER,
                       REGISTRY_PARENT_PATH,
                       0,
                       NULL,
                       0,
                       KEY_WRITE,
                       NULL,
                       &hKey,
                       &disposition);
    if (lastError == ERROR_SUCCESS)
    {
        RegCloseKey(hKey);
    }

    // Create or open the registry key
    lastError = RegCreateKeyExW(
            HKEY_CURRENT_USER, REGISTRY_PATH, 0, NULL, 0, KEY_WRITE, NULL, &hKey, &disposition);
    if (lastError == ERROR_SUCCESS)
    {
        // Convert ASCII strings to wide strings for storage
        wchar_t wideToken[256] = {0};
        MultiByteToWideChar(CP_ACP, 0, g_OpenAIToken, -1, wideToken, 256);

        // Save OpenAI token
        lastError = RegSetValueExW(hKey,
                      REGISTRY_TOKEN_VALUE,
                      0,
                      REG_SZ,
                      (const BYTE*)wideToken,
                      (wcslen(wideToken) + 1) * sizeof(wchar_t));

        // Save API type
        DWORD apiType = static_cast<DWORD>(g_APIType);
        lastError = RegSetValueExW(
            hKey, REGISTRY_API_TYPE_VALUE, 0, REG_DWORD, (const BYTE*)&apiType, sizeof(apiType));

        // Convert endpoint string to wide
        wchar_t wideEndpoint[256] = {0};
        MultiByteToWideChar(CP_ACP, 0, g_Endpoint, -1, wideEndpoint, 256);

        // Save endpoint
        lastError = RegSetValueExW(hKey,
                      REGISTRY_ENDPOINT_VALUE,
                      0,
                      REG_SZ,
                      (const BYTE*)wideEndpoint,
                      (wcslen(wideEndpoint) + 1) * sizeof(wchar_t));

        // Convert prompt text to wide string
        wchar_t widePrompt[1024] = {0};
        MultiByteToWideChar(CP_ACP, 0, g_PromptText, -1, widePrompt, 1024);

        // Save prompt text
        lastError = RegSetValueExW(hKey,
                      REGISTRY_PROMPT_VALUE,
                      0,
                      REG_SZ,
                      (const BYTE*)widePrompt,
                      (wcslen(widePrompt) + 1) * sizeof(wchar_t));

        RegCloseKey(hKey);
    }
}

// Dialog procedure to handle messages
INT_PTR CALLBACK SettingsDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_INITDIALOG:
    {
        // Load settings from the registry and set them in the dialog
        LoadSettingsFromRegistry();

        // Use wide string versions for the dialog
        wchar_t wideToken[256] = {0};
        wchar_t wideEndpoint[256] = {0};
        wchar_t widePrompt[1024] = {0};

        // Convert from ASCII to wide strings
        MultiByteToWideChar(CP_ACP, 0, g_OpenAIToken, -1, wideToken, 256);
        MultiByteToWideChar(CP_ACP, 0, g_Endpoint, -1, wideEndpoint, 256);
        MultiByteToWideChar(CP_ACP, 0, g_PromptText, -1, widePrompt, 1024);

        SetDlgItemTextW(hDlg, IDC_OPENAI_TOKEN, wideToken);
        SetDlgItemTextW(hDlg, IDC_ENDPOINT, wideEndpoint);
        SetDlgItemTextW(hDlg, IDC_PROMPT, widePrompt);

        // Set radio button based on the saved API type
        CheckRadioButton(hDlg,
                         IDC_RADIO_OPENAI,
                         IDC_RADIO_CUSTOM,
                         g_APIType == API_OPENAI ? IDC_RADIO_OPENAI : IDC_RADIO_CUSTOM);
        return TRUE;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDOK:  // OK Button pressed
            {
                // Get the entered OpenAI token and endpoint using wide character functions
                wchar_t wideToken[256] = {0};
                wchar_t wideEndpoint[256] = {0};
                wchar_t widePrompt[1024] = {0};

                GetDlgItemTextW(hDlg, IDC_OPENAI_TOKEN, wideToken, 256);
                GetDlgItemTextW(hDlg, IDC_ENDPOINT, wideEndpoint, 256);
                GetDlgItemTextW(hDlg, IDC_PROMPT, widePrompt, 1024);

                // Convert wide to ASCII for our global variables
                WideCharToMultiByte(CP_ACP, 0, wideToken, -1, g_OpenAIToken, sizeof(g_OpenAIToken), NULL, NULL);
                WideCharToMultiByte(CP_ACP, 0, wideEndpoint, -1, g_Endpoint, sizeof(g_Endpoint), NULL, NULL);
                WideCharToMultiByte(CP_ACP, 0, widePrompt, -1, g_PromptText, sizeof(g_PromptText), NULL, NULL);

                // Get the selected API type
                g_APIType = (IsDlgButtonChecked(hDlg, IDC_RADIO_OPENAI) == BST_CHECKED) ? API_OPENAI
                                                                                        : API_CUSTOM;

                // Save settings to the registry
                SaveSettingsToRegistry();

                EndDialog(hDlg, IDOK);
                return TRUE;
            }

        case IDCANCEL:  // Cancel Button pressed
            EndDialog(hDlg, IDCANCEL);
            return TRUE;

        case IDAPPLY:  // Apply Button pressed
            {
                // Get the entered OpenAI token and endpoint using wide character functions
                wchar_t wideToken[256] = {0};
                wchar_t wideEndpoint[256] = {0};
                wchar_t widePrompt[1024] = {0};

                GetDlgItemTextW(hDlg, IDC_OPENAI_TOKEN, wideToken, 256);
                GetDlgItemTextW(hDlg, IDC_ENDPOINT, wideEndpoint, 256);
                GetDlgItemTextW(hDlg, IDC_PROMPT, widePrompt, 1024);

                // Convert wide to ASCII for our global variables
                WideCharToMultiByte(CP_ACP, 0, wideToken, -1, g_OpenAIToken, sizeof(g_OpenAIToken), NULL, NULL);
                WideCharToMultiByte(CP_ACP, 0, wideEndpoint, -1, g_Endpoint, sizeof(g_Endpoint), NULL, NULL);
                WideCharToMultiByte(CP_ACP, 0, widePrompt, -1, g_PromptText, sizeof(g_PromptText), NULL, NULL);

                // Get the selected API type
                g_APIType = (IsDlgButtonChecked(hDlg, IDC_RADIO_OPENAI) == BST_CHECKED) ? API_OPENAI
                                                                                        : API_CUSTOM;

                // Save settings to the registry without closing the dialog
                SaveSettingsToRegistry();
                MessageBoxW(hDlg, L"Changes Applied", L"Info", MB_OK | MB_ICONINFORMATION);
                return TRUE;
            }
        }
    }
    return FALSE;
}

// Function to show the settings dialog in a blocking way
void ShowSettingsDialog(HWND hwndParent)
{
    INT_PTR result = DialogBox(
        GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_SETTINGS), hwndParent, SettingsDlgProc);
    if (result == -1)
    {
        DWORD dwError = GetLastError();
        LPVOID lpMsgBuf;
        FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                          FORMAT_MESSAGE_IGNORE_INSERTS,
                      NULL,
                      dwError,
                      MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                      (LPTSTR)&lpMsgBuf,
                      0,
                      NULL);

        MessageBox(NULL, (LPCTSTR)lpMsgBuf, TEXT("Error"), MB_OK | MB_ICONERROR);

        LocalFree(lpMsgBuf);
    }
    std::cout << "res: " << result;
}
