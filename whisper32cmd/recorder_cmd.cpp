#include <Windows.h>

#include "../recorder_h.h"  // This would be your generated header file from the IDL

#include <iostream>
#include <string>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "rpcrt4.lib")

EXTERN_C const CLSID CLSID_Recorder;

int main(int argc, char* argv[])
{
    // Check if the command line argument is provided
    if (argc != 2)
    {
        std::cerr << "Usage: " << argv[0] << " [start|stop]" << std::endl;
        return 1;
    }

    std::string command = argv[1];

    // Initialize COM
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hr))
    {
        std::cerr << "COM initialization failed: " << std::hex << hr << std::endl;
        return 1;
    }

    // Create an instance of the Recorder
    IRecorder* pRecorder = NULL;
    hr = CoCreateInstance(CLSID_Recorder,
                          NULL,
                          CLSCTX_ALL,
                          /*__uuidof(IRecorder)*/ IID_IRecorder,
                          (void**)&pRecorder);
    if (FAILED(hr))
    {
        std::cerr << "Failed to create instance of Recorder: " << std::hex << hr << std::endl;
        CoUninitialize();
        return 1;
    }

    if (command == "start")
    {
        // Start recording
        hr = pRecorder->StartRecording();
        if (FAILED(hr))
        {
            std::cerr << "Failed to start recording: " << std::hex << hr << std::endl;
        }
        else
        {
            std::cout << "Recording started." << std::endl;
        }
    }
    else if (command == "stop")
    {
        // Stop recording and get the result
        BSTR result;
        hr = pRecorder->StopRecording(&result);
        if (FAILED(hr))
        {
            std::cerr << "Failed to stop recording: " << std::hex << hr << std::endl;
        }
        else
        {
            // Convert BSTR to a standard string (assuming Unicode)
            wchar_t* wsResult = (wchar_t*)result;
            std::wcout << L"Recording stopped. Result: " << wsResult << std::endl;
            // Free the BSTR
            SysFreeString(result);
        }
    }
    else
    {
        std::cerr << "Invalid command. Use 'start' or 'stop'." << std::endl;
        pRecorder->Release();
        CoUninitialize();
        return 1;
    }

    // Release the IRecorder interface
    if (pRecorder)
    {
        pRecorder->Release();
    }

    // Uninitialize COM
    CoUninitialize();

    return 0;
}
