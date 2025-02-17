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

// Global variables to hold settings
char g_OpenAIToken[256] = {0};
char g_Endpoint[256] = {0};
APIType g_APIType = API_OPENAI;

char* GetOpenAIToken() { return g_OpenAIToken; }
char* GetCustomEndpoint() { return g_Endpoint; }
APIType GetAPIType() { return g_APIType; }

// Function to load settings from the registry
void LoadSettingsFromRegistry()
{
    HKEY hKey;
    DWORD dataSize;

    // Open the registry key
    if (RegOpenKeyEx(HKEY_CURRENT_USER, REGISTRY_PATH, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        // Load OpenAI token
        dataSize = sizeof(g_OpenAIToken);
        if (RegQueryValueEx(
                hKey, REGISTRY_TOKEN_VALUE, NULL, NULL, (LPBYTE)g_OpenAIToken, &dataSize) !=
            ERROR_SUCCESS)
        {
            g_OpenAIToken[0] = '\0';
        }

        // Load API type
        DWORD apiType = API_OPENAI;
        dataSize = sizeof(apiType);
        if (RegQueryValueEx(
                hKey, REGISTRY_API_TYPE_VALUE, NULL, NULL, (LPBYTE)&apiType, &dataSize) !=
            ERROR_SUCCESS)
        {
            apiType = API_OPENAI;
        }
        g_APIType = static_cast<APIType>(apiType);

        // Load endpoint
        dataSize = sizeof(g_Endpoint);
        if (RegQueryValueEx(
                hKey, REGISTRY_ENDPOINT_VALUE, NULL, NULL, (LPBYTE)g_Endpoint, &dataSize) !=
            ERROR_SUCCESS)
        {
            g_Endpoint[0] = '\0';
        }

        RegCloseKey(hKey);
    }
    else
    {
        // If the key doesn't exist, initialize default values
        g_OpenAIToken[0] = '\0';
        g_APIType = API_OPENAI;
        g_Endpoint[0] = '\0';
    }
}

// Function to save settings to the registry
void SaveSettingsToRegistry()
{
    HKEY hKey;
    DWORD disposition;

    // Create the parent path first if needed
    if (RegCreateKeyEx(HKEY_CURRENT_USER,
                       REGISTRY_PARENT_PATH,
                       0,
                       NULL,
                       0,
                       KEY_WRITE,
                       NULL,
                       &hKey,
                       &disposition) == ERROR_SUCCESS)
    {
        RegCloseKey(hKey);
    }

    // Create or open the registry key
    if (RegCreateKeyEx(
            HKEY_CURRENT_USER, REGISTRY_PATH, 0, NULL, 0, KEY_WRITE, NULL, &hKey, &disposition) ==
        ERROR_SUCCESS)
    {
        // Save OpenAI token
        RegSetValueEx(hKey,
                      REGISTRY_TOKEN_VALUE,
                      0,
                      REG_SZ,
                      (const BYTE*)g_OpenAIToken,
                      strlen(g_OpenAIToken) + 1);

        // Save API type
        DWORD apiType = static_cast<DWORD>(g_APIType);
        RegSetValueEx(
            hKey, REGISTRY_API_TYPE_VALUE, 0, REG_DWORD, (const BYTE*)&apiType, sizeof(apiType));

        // Save endpoint
        RegSetValueEx(hKey,
                      REGISTRY_ENDPOINT_VALUE,
                      0,
                      REG_SZ,
                      (const BYTE*)g_Endpoint,
                      strlen(g_Endpoint) + 1);

        RegCloseKey(hKey);
    }
}

// Dialog procedure to handle messages
INT_PTR CALLBACK SettingsDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_INITDIALOG:
        // Load settings from the registry and set them in the dialog
        LoadSettingsFromRegistry();
        SetDlgItemText(hDlg, IDC_OPENAI_TOKEN, to_wstring(g_OpenAIToken).c_str());
        SetDlgItemText(hDlg, IDC_ENDPOINT, to_wstring(g_Endpoint).c_str());

        // Set radio button based on the saved API type
        CheckRadioButton(hDlg,
                         IDC_RADIO_OPENAI,
                         IDC_RADIO_CUSTOM,
                         g_APIType == API_OPENAI ? IDC_RADIO_OPENAI : IDC_RADIO_CUSTOM);
        return TRUE;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDOK:  // OK Button pressed
            // Get the entered OpenAI token and endpoint
            GetDlgItemTextA(hDlg, IDC_OPENAI_TOKEN, g_OpenAIToken, sizeof(g_OpenAIToken));
            GetDlgItemTextA(hDlg, IDC_ENDPOINT, g_Endpoint, sizeof(g_Endpoint));

            // Get the selected API type
            g_APIType = (IsDlgButtonChecked(hDlg, IDC_RADIO_OPENAI) == BST_CHECKED) ? API_OPENAI
                                                                                    : API_CUSTOM;

            // Save settings to the registry
            SaveSettingsToRegistry();

            EndDialog(hDlg, IDOK);
            return TRUE;

        case IDCANCEL:  // Cancel Button pressed
            EndDialog(hDlg, IDCANCEL);
            return TRUE;

        case IDAPPLY:  // Apply Button pressed
            // Get the entered OpenAI token and endpoint
            GetDlgItemTextA(hDlg, IDC_OPENAI_TOKEN, g_OpenAIToken, sizeof(g_OpenAIToken));
            GetDlgItemTextA(hDlg, IDC_ENDPOINT, g_Endpoint, sizeof(g_Endpoint));

            // Get the selected API type
            g_APIType = (IsDlgButtonChecked(hDlg, IDC_RADIO_OPENAI) == BST_CHECKED) ? API_OPENAI
                                                                                    : API_CUSTOM;

            // Save settings to the registry without closing the dialog
            SaveSettingsToRegistry();
            MessageBox(hDlg, L"Changes Applied", L"Info", MB_OK | MB_ICONINFORMATION);
            return TRUE;
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
