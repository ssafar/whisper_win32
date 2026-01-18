#include "text_injection.hpp"


#include <windows.h>
#include <iostream>

#include "emacs.hpp"

bool SetClipboardText(const std::string& text)
{
    // Convert the UTF-8 string to a wide string
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), NULL, 0);
    std::wstring wide_text(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), &wide_text[0], size_needed);

    // Open the clipboard
    if (!OpenClipboard(NULL))
    {
        return false;  // If we can't open the clipboard, exit the function
    }

    // Empty the clipboard
    EmptyClipboard();

    // Allocate global memory for the text
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, wide_text.size() * 2 + 2);
    if (!hMem)
    {
        CloseClipboard();
        return false;
    }

    // Copy the text to the global memory
    memcpy(GlobalLock(hMem), wide_text.c_str(), wide_text.size() * 2 + 2);
    GlobalUnlock(hMem);

    // Set the clipboard data
    SetClipboardData(CF_UNICODETEXT, hMem);

    // Close the clipboard
    CloseClipboard();

    return true;
}

void InjectTextViaClipboard(const std::string& text, HWND hWnd)
{
    if (!SetClipboardText(text))
    {
        return;
    }

    // Bring the target window to the foreground
    SetForegroundWindow(hWnd);

    // Simulate Ctrl+V key press
    INPUT inputs[4] = {};
    ZeroMemory(inputs, sizeof(inputs));

    // Set up a generic keyboard event structure
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = VK_CONTROL;
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = 'V';
    inputs[2].type = INPUT_KEYBOARD;
    inputs[2].ki.wVk = 'V';
    inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
    inputs[3].type = INPUT_KEYBOARD;
    inputs[3].ki.wVk = VK_CONTROL;
    inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;

    // Send the input to the system
    SendInput(4, inputs, sizeof(INPUT));
}

void InjectTextViaConsoleRightClick(const std::string& text, HWND hWnd)
{
    if (!SetClipboardText(text))
    {
        return;
    }

    SetForegroundWindow(hWnd);

    RECT rc;
    if (!GetClientRect(hWnd, &rc))
    {
        return;
    }

    const int x = (rc.left + rc.right) / 2;
    const int y = (rc.top + rc.bottom) / 2;
    const LPARAM click_pos = MAKELPARAM(x, y);

    PostMessage(hWnd, WM_RBUTTONDOWN, MK_RBUTTON, click_pos);
    PostMessage(hWnd, WM_RBUTTONUP, 0, click_pos);
}

void InjectTextToTarget(const std::string& text)
{
    HWND hwndForeground = GetForegroundWindow();
    if (hwndForeground == NULL)
    {
        std::cout << "couldn't get foreground window" << std::endl;
        return;
    }

    wchar_t className[256];
    if (GetClassName(hwndForeground, className, sizeof(className) / sizeof(wchar_t)) > 0)
    {
        std::wcout << "got class name: " << className << std::endl;
    }
    else
    {
        std::wcout << "oops couldn't get class name" << std::endl;
    }

    std::wstring classNameString{className};
    if (classNameString == L"Emacs")
    {
        InjectTextToEmacs(text);
    }
    else if (classNameString == L"ConsoleWindowClass")
    {
        InjectTextViaConsoleRightClick(text, hwndForeground);
    }
    else
    {
        InjectTextViaClipboard(text, hwndForeground);
    }
}
