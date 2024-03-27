#pragma once
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <sstream>


#define HANDLE_RETURN(err) LogIfFailed(err, __FILE__, __LINE__)
//Only include one instance of error logs per translation unit
inline std::stringstream Win32ErrorLog;
inline std::stringstream DirectErrorLog;
inline std::stringstream HLSLErrorLog;

inline std::string HrToString(HRESULT hr)
{
    char s_str[64] = {};
    sprintf_s(s_str, "HRESULT of 0x%08X", static_cast<UINT>(hr));
    return std::string(s_str);
}

inline void LogIfFailed(HRESULT err, const char* file, int line) {
    if (FAILED(err)) {
        std::string message = "File: " + std::string(file) + "\n\n" + HrToString(err) + "\n\nLine: " + std::to_string(line);
        DirectErrorLog << message;
    }
}


inline void LogIfFailed(bool err, const char* file, int line)
{
    if (err)
    {
        DWORD error = GetLastError();
        LPVOID lpMsgBuf;
        FormatMessageA(
            FORMAT_MESSAGE_ALLOCATE_BUFFER |
            FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
            NULL,
            error,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            (LPSTR)&lpMsgBuf,
            0, NULL);


        std::string message = "File: " + std::string(file) + "\n\nMessage: " + std::string((char*)lpMsgBuf) + "\n\nLine: " + std::to_string(line);
        LocalFree(lpMsgBuf);

        Win32ErrorLog << message;
    }
}


inline void displayErrorMessage(std::string error) {

    MessageBoxA(NULL, error.c_str(), NULL, MB_OK);

}

inline void logDebug() {
    //DX12 info queue
    if (!Win32ErrorLog.str().empty()) {
        displayErrorMessage("Win32 Errors:\n\n" + Win32ErrorLog.str());
    }
    if (!DirectErrorLog.str().empty()) {
        displayErrorMessage("DX12 Errors:\n\n" + DirectErrorLog.str());
    }
    if (!HLSLErrorLog.str().empty()) {
        displayErrorMessage("HLSL Errors:\n\n" + HLSLErrorLog.str());
    }
}