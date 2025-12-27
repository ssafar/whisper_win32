// for sprintf etc.
#define _CRT_SECURE_NO_WARNINGS

#include <windows.h>

#include "emacs.hpp"
#include "text_injection.hpp"
#include "json.hpp"
#include "resource.h"
#include "settings.hpp"
#include "utils.hpp"

#include <commctrl.h>

#include <curl/curl.h>
#include <dsound.h>
#include <lame/lame.h>
#include <process.h>

#include <iostream>
#include <optional>

#define BUFFER_SIZE 44100 * 2 * 1220  // a lot of seconds of 44100 Hz, 16-bit mono audio

constexpr int WM_REQUEST_DONE = WM_USER + 1;
constexpr int WM_TRAYICON = WM_USER + 2;


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

struct Mp3Segment
{
    std::vector<char> data;
    double audio_duration_seconds = 0.0;
};

struct Mp3SegmentRing
{
    static constexpr int NUM_ELTS = 5;

    std::array<Mp3Segment, NUM_ELTS> segments;

    volatile int last_written = -1;

    HANDLE newEntryEvent; // Producers signal this, consumers ack

    Mp3SegmentRing()
    {
        newEntryEvent = CreateEvent(
            NULL, // security
            TRUE, // manual reset
            FALSE, // initial state
            NULL); // no name
    };
};


LPDIRECTSOUND8 lpds;
LPDIRECTSOUNDCAPTURE8 lpdsCapture;
LPDIRECTSOUNDCAPTUREBUFFER lpdsCaptureBuffer = NULL;
bool isRecording = false;
DWORD bufferSize = BUFFER_SIZE;

Mp3SegmentRing mp3_segments;

// Signaled when we're done with everything
HANDLE terminationEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

lame_t lame;
HWND hwndDialog;

// For the COM API
constexpr int PROCESSING_TIME_TIMEOUT_MS = 15000;

// This is the JSON
std::string the_results;

// This is the actual return value.
std::optional<std::string> returned_text;

// Stats for the last request
double last_audio_duration_seconds = 0.0;
double last_request_time_seconds = 0.0;

constexpr int HKID_START_OR_STOP = 1;

BOOL InitDirectSound(HWND hWnd);
void StartRecording();
void StopRecording();

size_t CurlWriteToStringCallback(void* contents, size_t size, size_t nmemb, std::string* s)
{
    size_t newLength = size * nmemb;
    s->append((char*)contents, newLength);
    return newLength;
}

// Runs on a background thread; sends the mp3 file to Whisper, and waits for the results.
void SendToWhisper(const Mp3Segment& segment)
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
    curl_mime_data(part, segment.data.data(), segment.data.size());
    curl_mime_type(part, "application/octet-stream");

    // For some reason, otherwise we would think that it's a string
    curl_mime_filename(part, "output.mp3");

    curl_mimepart* part2 = curl_mime_addpart(mime);
    curl_mime_name(part2, "model");
    curl_mime_data(part2, "whisper-1", CURL_ZERO_TERMINATED);

    // Only add prompt if it's not empty
    const char* prompt = GetPromptText();
    if (prompt && prompt[0] != '\0') {
        curl_mimepart* part3 = curl_mime_addpart(mime);
        curl_mime_name(part3, "prompt");
        curl_mime_data(part3, prompt, CURL_ZERO_TERMINATED);
    }

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

    // Perform the file upload with timing
    ULONGLONG start_time = GetTickCount64();
    CURLcode res = curl_easy_perform(curl);
    ULONGLONG end_time = GetTickCount64();
    last_request_time_seconds = (end_time - start_time) / 1000.0;

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
        InjectTextToTarget(returned_text.value());
    }
    else
    {
        returned_text = std::nullopt;
    }

    // Set stats from segment (so they're coherent with this request)
    last_audio_duration_seconds = segment.audio_duration_seconds;

    PostMessage(hwndDialog, WM_REQUEST_DONE, 0, 0);
}

unsigned int __stdcall SendToWhisperWorker(void* hwnd)
{
    int last_consumed = -1;

    // complete yolo, no locking, we just fill the ring and hope we don't wrap over
    HANDLE handles[] { terminationEvent, mp3_segments.newEntryEvent };

    do {
        DWORD result = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
        if (result == WAIT_OBJECT_0) // termination
        {
            break;
        }

        if (result == WAIT_OBJECT_0 + 1)
        {
            ResetEvent(mp3_segments.newEntryEvent);

            // Catch up with all newly written ones
            while (mp3_segments.last_written > last_consumed)
            {
                const Mp3Segment& segment = mp3_segments.segments[(++last_consumed) % Mp3SegmentRing::NUM_ELTS];
                SendToWhisper(segment);
            }
        }
    } while (true);

    _endthreadex(0);
    return 0;
}

void SendToWhisperAsync()
{
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

    // Update stats label
    wchar_t stats_buffer[128];
    double ratio = (last_audio_duration_seconds > 0)
        ? last_request_time_seconds / last_audio_duration_seconds
        : 0.0;
    swprintf(stats_buffer, 128, L"%.1fs audio \u2192 %.1fs processing (%.2fx realtime)",
             last_audio_duration_seconds, last_request_time_seconds, ratio);
    SetWindowText(GetDlgItem(hwndDialog, IDC_STATS), stats_buffer);
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
        SetWindowText(GetDlgItem(hwnd, IDC_RECORD), L"Stop [F8]");
    }
    else
    {
        StopRecording();
        SetWindowText(GetDlgItem(hwnd, IDC_RECORD), L"Record [F8]");
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

        // Calculate audio duration: bytes / (sample_rate * channels * bytes_per_sample)
        // 44100 Hz, mono, 16-bit = 88200 bytes per second
        double audio_duration = SUCCEEDED(hr) ? dwCapturePosition / 88200.0 : 0.0;

        if (SUCCEEDED(hr))
        {
            wchar_t buffer[64];
            wsprintf(buffer, L"%lu bytes recorded", dwCapturePosition);
            SetWindowText(GetDlgItem(hwndDialog, IDC_MESSAGES), buffer);
        }
        else
        {
            SetWindowText(GetDlgItem(hwndDialog, IDC_MESSAGES), L"there were issues.");
        }

        // Reserve a new segment
        int segment_id = mp3_segments.last_written + 1;
        Mp3Segment& segment = mp3_segments.segments[segment_id % Mp3SegmentRing::NUM_ELTS];

        // Fill it
        segment.data = std::move(EncodeToMP3(lpdsCaptureBuffer, BUFFER_SIZE));
        segment.audio_duration_seconds = audio_duration;

        // Release it
        mp3_segments.last_written += 1;
        SetEvent(mp3_segments.newEntryEvent);

        lpdsCaptureBuffer->Release();
        lpdsCaptureBuffer = NULL;
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_WIN95_CLASSES;
    InitCommonControlsEx(&icex);

    LoadSettingsFromRegistry();

    // A global hotkey so that we can use it even if we are in the background.
    RegisterHotKey(NULL, HKID_START_OR_STOP, MOD_NOREPEAT, VK_F8);

    // We need an explicit message pump to be able to process the global hotkey messages.
    HWND hwndDialog = CreateDialog(hInstance, MAKEINTRESOURCE(IDD_RECORDER), NULL, DialogProc);

    // FIXME(ssafar): are we leaking the thread handle here?
    _beginthreadex(NULL, 0, &SendToWhisperWorker, hwndDialog, 0, NULL);

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

    UnregisterHotKey(NULL, HKID_START_OR_STOP);

    // Stop all the threads
    SetEvent(terminationEvent);

    NOTIFYICONDATA nid = {0};
    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = hwndDialog;
    nid.uID = ID_TRAYICON;
    Shell_NotifyIcon(NIM_DELETE, &nid);

    return 0;
}
