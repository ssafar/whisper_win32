#include "recorder_h.h"

// for sprintf etc.
#define _CRT_SECURE_NO_WARNINGS

#include <windows.h>

#include "json.hpp"
#include "resource.h"
#include "settings.hpp"
#include "utils.hpp"

#include <commctrl.h>
#include <comutil.h>
#include <curl/curl.h>
#include <dsound.h>
#include <lame/lame.h>
#include <process.h>

#include <codecvt>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>

#define BUFFER_SIZE 44100 * 2 * 120  // 120 seconds of 44100 Hz, 16-bit mono audio

constexpr int WM_REQUEST_DONE = WM_USER + 1;
constexpr int WM_TRAYICON = WM_USER + 2;

EXTERN_C const CLSID CLSID_Recorder;
#pragma comment(lib, "dsound.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "libmp3lame.lib")
#pragma comment(lib, "ComCtl32.lib")
#pragma comment(lib, "version.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "comsuppw.lib")
#pragma comment(lib, "rpcrt4.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "OleAut32.lib")

#if _WIN64
#pragma comment(lib, "libcurl-x64")
#else
#pragma comment(lib, "libcurl")
#endif

struct ConnectionInfo
{
    std::string ip;
    int port{};
    std::string authString;
};

LPDIRECTSOUND8 lpds;
LPDIRECTSOUNDCAPTURE8 lpdsCapture;
LPDIRECTSOUNDCAPTUREBUFFER lpdsCaptureBuffer = NULL;
bool isRecording = false;
DWORD bufferSize = BUFFER_SIZE;

std::optional<std::vector<char>> mp3_data{};

lame_t lame;
HWND hwndDialog;

// For the COM API
constexpr int PROCESSING_TIME_TIMEOUT_MS = 15000;

// Was this started from the UI? (reset by completion, set possibly by the COM
// interface.)
bool stopped_by_com = false;

// This is the JSON
std::string the_results;

// This is the actual return value.
std::optional<std::string> returned_text;
HANDLE hResultsReadyEvent;

constexpr int HKID_START_OR_STOP = 1;

ConnectionInfo ReadEmacsConnectionInfo();
void InvokeEmacs(const ConnectionInfo& connInfo, const std::string& invocation);

BOOL InitDirectSound(HWND hWnd);
void StartRecording();
void StopRecording();

size_t CurlWriteToStringCallback(void* contents, size_t size, size_t nmemb, std::string* s)
{
    size_t newLength = size * nmemb;
    s->append((char*)contents, newLength);
    return newLength;
}

void InjectTextToEmacs(const std::string& text)
{
    ConnectionInfo info = ReadEmacsConnectionInfo();
    std::cout << "got emacs port: " << info.port << std::endl;

    // ... we'll need to escape this too
    InvokeEmacs(
        info, "(with-current-buffer (window-buffer (selected-window)) (insert \"" + text + "\"))");
}

void InjectTextViaClipboard(const std::string& text, HWND hWnd)
{
    // Convert the UTF-8 string to a wide string
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), NULL, 0);
    std::wstring wide_text(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), &wide_text[0], size_needed);

    // Open the clipboard
    if (!OpenClipboard(NULL))
    {
        return;  // If we can't open the clipboard, exit the function
    }

    // Empty the clipboard
    EmptyClipboard();

    // Allocate global memory for the text
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, wide_text.size() * 2 + 2);
    if (!hMem)
    {
        CloseClipboard();
        return;
    }

    // Copy the text to the global memory
    memcpy(GlobalLock(hMem), wide_text.c_str(), wide_text.size() * 2 + 2);
    GlobalUnlock(hMem);

    // Set the clipboard data
    SetClipboardData(CF_UNICODETEXT, hMem);

    // Close the clipboard
    CloseClipboard();

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
    else
    {
        InjectTextViaClipboard(text, hwndForeground);
    }
}

// Runs on a background thread; sends the mp3 file to Whisper, and waits for the results.
void SendToWhisper()
{
    CURL* curl = curl_easy_init();

    // Set the URL for the request
    if (GetAPIType() == API_OPENAI)
    {
        curl_easy_setopt(curl, CURLOPT_URL, "https://api.openai.com/v1/audio/transcriptions");
    }
    else
    {
        curl_easy_setopt(curl, CURLOPT_URL, GetCustomEndpoint());
    }

    // Create a MIME handle for a multipart/form-data POST
    curl_mime* mime = curl_mime_init(curl);

    // Add the file part
    curl_mimepart* part = curl_mime_addpart(mime);
    curl_mime_name(part, "file");
    curl_mime_data(part, mp3_data.value().data(), mp3_data.value().size());
    curl_mime_type(part, "application/octet-stream");

    // For some reason, otherwise we would think that it's a string
    curl_mime_filename(part, "output.mp3");

    curl_mimepart* part2 = curl_mime_addpart(mime);
    curl_mime_name(part2, "model");
    curl_mime_data(part2, "whisper-1", CURL_ZERO_TERMINATED);

    // Add the headers
    struct curl_slist* headerlist = NULL;
    headerlist = curl_slist_append(headerlist, "Expect:");
    headerlist = curl_slist_append(headerlist, "Content-Type: multipart/form-data");

    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_OPTIONS, CURLSSLOPT_NATIVE_CA);
    curl_version_info_data* data = curl_version_info(CURLVERSION_NOW);
    printf("libcurl is using %s for SSL/TLS.\n", data->ssl_version);

    if (GetAPIType() == API_OPENAI)
    {
        char auth_header[300];
        snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", GetOpenAIToken());
        headerlist = curl_slist_append(headerlist, auth_header);
    }

    // Set the custom headers
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerlist);

    // Set the mime post data
    curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);

    std::string response;

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteToStringCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    // Perform the file upload
    CURLcode res = curl_easy_perform(curl);

    // Check for errors
    if (res != CURLE_OK)
    {
        fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
    }
    else
    {
        printf("Upload completed successfully\n");
    }

    std::cout << "Got response: " << response << std::endl;

    // Clean up the MIME part
    curl_mime_free(mime);
    curl_slist_free_all(headerlist);

    // Always cleanup
    curl_easy_cleanup(curl);

    the_results = response;

    nlohmann::json results_obj = nlohmann::json::parse(the_results);

    if (results_obj.contains("text"))
    {
        returned_text = results_obj["text"];
        if (stopped_by_com)
        {
            SetEvent(hResultsReadyEvent);
        }
        else
        {
            InjectTextToTarget(returned_text.value());
        }
    }
    else
    {
        returned_text = std::nullopt;
    }

    stopped_by_com = false;
    PostMessage(hwndDialog, WM_REQUEST_DONE, 0, 0);
}

unsigned int __stdcall SendToWhisperWorker(void* hwnd)
{
    SendToWhisper();
    _endthreadex(0);
    return 0;
}

void SendToWhisperAsync()
{
    // FIXME(ssafar): are we leaking the thread handle here?
    _beginthreadex(NULL, 0, &SendToWhisperWorker, hwndDialog, 0, NULL);
}

// Takes the contents of the captured buffer and encodes the results into an mp3 file on disk.
std::vector<char> EncodeToMP3(IDirectSoundCaptureBuffer* lpdsCaptureBuffer, DWORD bufferSize)
{
    lame_t lame = lame_init();
    lame_set_in_samplerate(lame, 44100);
    lame_set_num_channels(lame, 1);  // Explicitly set to mono
    lame_set_mode(lame, MONO);
    lame_set_VBR(lame, vbr_default);
    if (lame_init_params(lame) < 0)
    {
        std::cerr << "Failed to initialize LAME encoder." << std::endl;
        return {};
    }

    BYTE* pcmBuffer;  // = new BYTE[bufferSize];
    const int MP3_BUFFER_SIZE = static_cast<int>(1.25 * bufferSize + 7200);

    std::vector<char> mp3Buffer;
    mp3Buffer.resize(MP3_BUFFER_SIZE);

    DWORD readBytes = 0;
    lpdsCaptureBuffer->GetCurrentPosition(nullptr, &readBytes);
    lpdsCaptureBuffer->Lock(0, readBytes, (void**)&pcmBuffer, &bufferSize, nullptr, 0, 0);

    int mp3Bytes = lame_encode_buffer(lame,
                                      (short int*)pcmBuffer,
                                      nullptr,
                                      readBytes / 2,
                                      (unsigned char*)mp3Buffer.data(),
                                      mp3Buffer.size());
    lpdsCaptureBuffer->Unlock(pcmBuffer, bufferSize, nullptr, 0);

    // Finish the encoding
    mp3Bytes += lame_encode_flush(
        lame, (unsigned char*)mp3Buffer.data() + mp3Bytes, MP3_BUFFER_SIZE - mp3Bytes);
    mp3Buffer.resize(mp3Bytes);  // Resize buffer to the actual MP3 size

    lame_close(lame);

    return mp3Buffer;
}

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

void ProcessResultsJson()
{
    nlohmann::json results_obj = nlohmann::json::parse(the_results);
    if (results_obj.is_object() && results_obj.contains("text"))
    {
        std::string text = results_obj["text"];
        SetWindowText(GetDlgItem(hwndDialog, IDC_MESSAGES), to_wstring(text).c_str());
    }
    else
    {
        // Just paste the entire thing
        SetWindowText(GetDlgItem(hwndDialog, IDC_MESSAGES), to_wstring(std::string{ the_results }).c_str());
    }
}

// Mostly for reading from the Emacs eval text box.
std::wstring GetWindowString(HWND hwnd)
{
    std::wstring str;
    int length = GetWindowTextLength(hwnd);
    str.resize(GetWindowTextLength(hwnd), ' ');

    GetWindowText(hwnd, str.data(), length + 1);

    return str;
}

// Handle the textbox for Emacs eval.
void DoEmacsEval(HWND hwnd)
{
    ConnectionInfo info = ReadEmacsConnectionInfo();
    std::wstring emacs_command = GetWindowString(GetDlgItem(hwnd, IDC_EMACS_COMMAND_EDIT));
    InvokeEmacs(info, to_string(emacs_command));
}

void OnRecordOrStop(HWND hwnd)
{
    // Use the same global hotkey for some test text injection
    if (SendMessage(GetDlgItem(hwnd, IDC_USE_TEST_TEXT), BM_GETCHECK, 0, 0))
    {
        SetTimer(hwnd, /* timer id */ 1, /* timeout */ 1000, nullptr);
        return;
    }

    if (!isRecording)
    {
        StartRecording();
        SetWindowText(GetDlgItem(hwnd, IDC_RECORD), L"Stop");
    }
    else
    {
        StopRecording();
        SetWindowText(GetDlgItem(hwnd, IDC_RECORD), L"Record");
        SendToWhisperAsync();
    }
    isRecording = !isRecording;
}

void SetupIconsAndTray(HWND hwnd)
{
    HICON hIcon =
        LoadIcon((HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), MAKEINTRESOURCE(IDI_RECORDER));

    SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
    SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);

    NOTIFYICONDATA nid = {0};
    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = hwnd;
    nid.uID = ID_TRAYICON;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = hIcon;
    Shell_NotifyIcon(NIM_ADD, &nid);
}

INT_PTR CALLBACK DialogProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_INITDIALOG:
        SetupIconsAndTray(hwnd);

        LoadSettingsFromRegistry();
        if (!InitDirectSound(hwnd))
        {
            MessageBox(
                hwnd, L"Error initializing DirectSound. The program will now exit.", L"Error", MB_OK);
            return FALSE;
        }
        hwndDialog = hwnd;
        return TRUE;
    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDC_RECORD:
            OnRecordOrStop(hwnd);
            break;
        case IDC_SEND_TO_WHISPER:
            SendToWhisperAsync();
            break;
        case IDC_EMACS_EVAL:
            DoEmacsEval(hwnd);
            break;
        case IDCANCEL:
            if (isRecording)
            {
                StopRecording();
            }
            DestroyWindow(hwnd);
            std::cout << "ending dialog" << std::endl;
            PostQuitMessage(0);
            break;
        case IDC_SETTINGS:
            ShowSettingsDialog(hwnd);
            break;
        }
        break;
    case WM_REQUEST_DONE:
        ProcessResultsJson();
        break;
    case WM_TRAYICON:
        if (lParam == WM_LBUTTONDOWN)
        {
            ShowWindow(hwnd, IsWindowVisible(hwnd) ? SW_HIDE : SW_SHOW);
        }
        if (lParam == WM_RBUTTONUP)
        {
            POINT pt;
            GetCursorPos(&pt);
            HMENU hMenu = CreatePopupMenu();
            AppendMenu(hMenu, MF_STRING, IDCANCEL, L"Exit");
            SetForegroundWindow(hwnd);
            TrackPopupMenu(hMenu, TPM_RIGHTALIGN, pt.x, pt.y, 0, hwnd, NULL);
            DestroyMenu(hMenu);
        }
        break;

    case WM_TIMER:
        InjectTextToTarget("Test text!");
        KillTimer(hwnd, wParam);
        break;
    case WM_CLOSE:
        std::cout << "got wm_close" << std::endl;
        SendMessage(hwndDialog, WM_COMMAND, IDCANCEL, 0);
        break;
        // default:
        //     return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

BOOL InitDirectSound(HWND hWnd)
{
    // Initialize DirectSound for recording
    if (FAILED(DirectSoundCaptureCreate8(NULL, &lpdsCapture, NULL)))
    {
        return FALSE;
    }

    return TRUE;
}

void StartRecording()
{
    DSCBUFFERDESC dsbdesc;
    WAVEFORMATEX wfx;

    // Set up wave format structure.
    memset(&wfx, 0, sizeof(WAVEFORMATEX));
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = 1;
    wfx.nSamplesPerSec = 44100;
    wfx.wBitsPerSample = 16;
    wfx.nBlockAlign = wfx.nChannels * (wfx.wBitsPerSample / 8);
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;

    // Set up DSBUFFERDESC structure.
    memset(&dsbdesc, 0, sizeof(DSCBUFFERDESC));
    dsbdesc.dwSize = sizeof(DSCBUFFERDESC);
    dsbdesc.dwBufferBytes = bufferSize;
    dsbdesc.lpwfxFormat = &wfx;

    // Create capture buffer.
    if (FAILED(lpdsCapture->CreateCaptureBuffer(&dsbdesc, &lpdsCaptureBuffer, NULL)))
    {
        SetWindowText(GetDlgItem(hwndDialog, IDC_MESSAGES), L"failed to create capture buffer");

        return;
    }

    if (lpdsCaptureBuffer)
    {
        lpdsCaptureBuffer->Start(DSCBSTART_LOOPING);
    }
}

void StopRecording()
{
    if (lpdsCaptureBuffer)
    {
        lpdsCaptureBuffer->Stop();

        DWORD dwCapturePosition, dwReadPosition;
        HRESULT hr = lpdsCaptureBuffer->GetCurrentPosition(&dwCapturePosition, &dwReadPosition);

        if (SUCCEEDED(hr))
        {
            // Now dwCapturePosition and dwReadPosition hold the current positions
            // You can use these values as needed in your application

            wchar_t buffer[64];
            wsprintf(buffer, L"%lu bytes recorded", dwCapturePosition);
            SetWindowText(GetDlgItem(hwndDialog, IDC_MESSAGES), buffer);
        }
        else
        {
            // Handle error
            SetWindowText(GetDlgItem(hwndDialog, IDC_MESSAGES), L"there were issues.");
        }

        mp3_data = std::move(EncodeToMP3(lpdsCaptureBuffer, BUFFER_SIZE));

        lpdsCaptureBuffer->Release();
        lpdsCaptureBuffer = NULL;
    }
}

// Assuming IRecorder and related interfaces are properly defined.

class Recorder : public IRecorder
{
    long m_refCount;

public:
    Recorder()
      : m_refCount(1)
    {
    }

    // IUnknown methods
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override
    {
        if (riid == IID_IUnknown || riid == IID_IRecorder || riid == IID_IDispatch)
        {
            *ppvObject = static_cast<IDispatch*>(this);
            AddRef();
            return S_OK;
        }
        else
        {
            *ppvObject = nullptr;
            return E_NOINTERFACE;
        }
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&m_refCount); }

    ULONG STDMETHODCALLTYPE Release() override
    {
        ULONG ulRefCount = InterlockedDecrement(&m_refCount);
        if (ulRefCount == 0)
        {
            delete this;
        }
        return ulRefCount;
    }

    // IDispatch methods
    HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT* pctinfo) override
    {
        *pctinfo = 0;  // Assuming no type info is provided
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT iTInfo, LCID lcid, ITypeInfo** ppTInfo) override
    {
        *ppTInfo = nullptr;  // No type info available
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE GetIDsOfNames(
        REFIID riid, LPOLESTR* rgszNames, UINT cNames, LCID lcid, DISPID* rgDispId) override
    {
        // Typically, you would use a map or some other structure to manage names and DISPID values
        // For simplicity, assume a simple implementation where "StartRecording" has DISPID 1 and
        // "StopRecording" has DISPID 2
        HRESULT hr = S_OK;
        for (UINT i = 0; i < cNames; i++)
        {
            if (_wcsicmp(rgszNames[i], L"StartRecording") == 0)
            {
                rgDispId[i] = 1;
            }
            else if (_wcsicmp(rgszNames[i], L"StopRecording") == 0)
            {
                rgDispId[i] = 2;
            }
            else
            {
                rgDispId[i] = DISPID_UNKNOWN;
                hr = DISP_E_UNKNOWNNAME;
            }
        }
        return hr;
    }

    HRESULT STDMETHODCALLTYPE Invoke(DISPID dispIdMember,
                                     REFIID riid,
                                     LCID lcid,
                                     WORD wFlags,
                                     DISPPARAMS* pDispParams,
                                     VARIANT* pVarResult,
                                     EXCEPINFO* pExcepInfo,
                                     UINT* puArgErr) override
    {
        switch (dispIdMember)
        {
        case 1:  // StartRecording
            if (wFlags & DISPATCH_METHOD)
            {
                return StartRecording();
            }
            break;
        case 2:  // StopRecording
            if (wFlags & DISPATCH_METHOD)
            {
                BSTR resultString = NULL;
                HRESULT hr = StopRecording(&resultString);
                if (FAILED(hr)) return hr;

                // Ensure pVarResult is not NULL
                if (pVarResult != NULL)
                {
                    VariantClear(pVarResult);  // Clear the VARIANT and free previous data if any
                    pVarResult->vt = VT_BSTR;  // Set type to BSTR
                    pVarResult->bstrVal = resultString;  // Assign the BSTR to the VARIANT
                }
                else
                {
                    // Optional: Free resultString if pVarResult is NULL to avoid memory leaks
                    SysFreeString(resultString);
                }
                return S_OK;
            }
            break;
        default:
            return DISP_E_MEMBERNOTFOUND;
        }
        return S_OK;
    }

    // IRecorder methods
    HRESULT STDMETHODCALLTYPE StartRecording() override
    {
        std::cout << "start called!" << std::endl;
        if (isRecording)
        {
            return S_OK;
        }

        SendMessage(hwndDialog, WM_COMMAND, IDC_RECORD, 0);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE StopRecording(BSTR* pResult) override
    {
        if (!isRecording)
        {
            return S_OK;
        }

        if (pResult == nullptr)
        {
            return E_POINTER;
        }

        stopped_by_com = true;

        SendMessage(hwndDialog, WM_COMMAND, IDC_RECORD, 0);

        // DWORD dwWaitResult = WaitForSingleObject(hResultsReadyEvent, PROCESSING_TIME_TIMEOUT_MS);
        DWORD dwWaitResult;
        HRESULT hr =
            CoWaitForMultipleHandles(COWAIT_DISPATCH_CALLS | COWAIT_DISPATCH_WINDOW_MESSAGES,
                                     PROCESSING_TIME_TIMEOUT_MS,
                                     1,
                                     &hResultsReadyEvent,
                                     &dwWaitResult);

        if (FAILED(hr))
        {
            return hr;
        }
        if (dwWaitResult != WAIT_OBJECT_0)
        {
            return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
        }

        ResetEvent(hResultsReadyEvent);

        if (!returned_text.has_value())
        {
            // Could be somewhat more specific?
            return E_FAIL;
        }

        // Convert std::string to BSTR
        _bstr_t bstrResult(returned_text.value().c_str());

        // Assign the BSTR to the output parameter, with proper AddRef
        *pResult = bstrResult.copy();

        if (*pResult == nullptr)
        {
            return E_OUTOFMEMORY;
        }

        return S_OK;
    }
};

// ... aand a class factory
class RecorderFactory : public IClassFactory
{
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override
    {
        if (riid == IID_IUnknown || riid == IID_IClassFactory)
        {
            *ppvObject = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        else
        {
            *ppvObject = nullptr;
            return E_NOINTERFACE;
        }
    }

    ULONG STDMETHODCALLTYPE AddRef() override
    {
        // Return a constant because this is a singleton
        return 2;
    }

    ULONG STDMETHODCALLTYPE Release() override
    {
        // Return a constant because this is a singleton
        return 1;
    }

    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* pUnkOuter,
                                             REFIID riid,
                                             void** ppvObject) override
    {
        wchar_t guidString[39];
        StringFromGUID2(riid, guidString, 39);

        std::wcout << "createInstance!!! " << guidString << std::endl;

        if (pUnkOuter != nullptr)
        {
            return CLASS_E_NOAGGREGATION;
        }

        Recorder* pRecorder = new Recorder();
        HRESULT hr = pRecorder->QueryInterface(riid, ppvObject);
        pRecorder->Release();  // Release initial reference
        return hr;
    }

    HRESULT STDMETHODCALLTYPE LockServer(BOOL fLock) override
    {
        // Manage lock count
        return S_OK;
    }
};

HRESULT RegisterRecorderTypeLib(HMODULE hModule) {
    wchar_t szPath[MAX_PATH];
    if (!GetModuleFileName(hModule, szPath, MAX_PATH)) {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    ITypeLib* pTypeLib = nullptr;
    HRESULT hr = LoadTypeLibEx(szPath, REGKIND_NONE, &pTypeLib);
    if (SUCCEEDED(hr)) {
        hr = RegisterTypeLibForUser(pTypeLib, szPath, nullptr);
        pTypeLib->Release();
    }
    return hr;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_WIN95_CLASSES;
    InitCommonControlsEx(&icex);
    RegisterRecorderTypeLib(hInstance);

    LoadSettingsFromRegistry();

    // A global hotkey so that we can use it even if we are in the background.
    RegisterHotKey(NULL, HKID_START_OR_STOP, MOD_NOREPEAT, VK_F8);

    hResultsReadyEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

    if (hResultsReadyEvent == NULL)
    {
        std::cerr << "CreateEvent failed (" << GetLastError() << ")" << std::endl;
        return 1;
    }

    // We need an explicit message pump to be able to process the global hotkey messages.
    HWND hwndDialog = CreateDialog(hInstance, MAKEINTRESOURCE(IDD_RECORDER), NULL, DialogProc);
    std::unique_ptr<RecorderFactory> recorder_factory{std::make_unique<RecorderFactory>()};

    DWORD dwRegister;
    HRESULT hr = CoRegisterClassObject(CLSID_Recorder,
                                       recorder_factory.get(),
                                       CLSCTX_LOCAL_SERVER,
                                       REGCLS_MULTIPLEUSE,
                                       &dwRegister);
    if (FAILED(hr))
    {
        // Function to retrieve the error message associated with the HRESULT
        LPVOID lpMsgBuf;
        DWORD dw = FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                     FORMAT_MESSAGE_IGNORE_INSERTS,
                                 NULL,
                                 hr,
                                 MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                 (LPTSTR)&lpMsgBuf,
                                 0,
                                 NULL);

        std::wcerr << L"Operation failed with error: " << static_cast<LPTSTR>(lpMsgBuf)
                   << std::endl;

        // Free the buffer allocated by FormatMessage
        LocalFree(lpMsgBuf);
    }
    else
    {
        std::cout << "Operation succeeded." << std::endl;
    }

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0))
    {
        if (msg.message == WM_HOTKEY && msg.wParam == HKID_START_OR_STOP)
        {
            SendMessage(hwndDialog, WM_COMMAND, IDC_RECORD, 0);
        }

        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (lpdsCapture)
    {
        lpdsCapture->Release();
    }

    CoRevokeClassObject(dwRegister);
    UnregisterHotKey(NULL, HKID_START_OR_STOP);
    CloseHandle(hResultsReadyEvent);

    NOTIFYICONDATA nid = {0};
    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = hwndDialog;
    nid.uID = ID_TRAYICON;
    Shell_NotifyIcon(NIM_DELETE, &nid);

    return 0;
}
