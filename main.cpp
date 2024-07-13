#include <Windows.h>
#include <iostream>
#include <winternl.h>
#include <vector>
#include <string>
#include <codecvt>


// i ripped these structs from https://processhacker.sourceforge.io/doc/winsta_8h_source.html
typedef WCHAR WINSTATIONNAME[32 + 1];
typedef enum _WINSTATIONSTATECLASS {
    State_Active = 0,
    State_Connected,
    State_ConnectQuery,
    State_Shadow,
    State_Disconnected,
    State_Idle,
    State_Listen,
    State_Reset,
    State_Down,
    State_Init
} WINSTATIONSTATECLASS;

typedef struct _SESSIONIDW {
    union {
        ULONG SessionId;
        ULONG LogonId;
    };
    WINSTATIONNAME WinStationName;
    WINSTATIONSTATECLASS State;
} SESSIONIDW, * PSESSIONIDW;

typedef HANDLE(WINAPI* LPFN_WinStationOpenServerW)(PWSTR);
typedef BOOLEAN(WINAPI* LPFN_WinStationCloseServer)(HANDLE);
typedef BOOLEAN(WINAPI* LPFN_WinStationEnumerateW)(HANDLE, PSESSIONIDW*, PULONG);
typedef BOOLEAN(WINAPI* LPFN_WinStationQueryInformationW)(HANDLE, ULONG, WINSTATIONINFOCLASS, PVOID, ULONG, PULONG);

std::vector<std::wstring> extractWideStrings(const BYTE* byteArray, size_t size) {
    std::vector<std::wstring> strings;
    std::wstring currentString;

    for (size_t i = 0; i < size / sizeof(WCHAR); ++i) {
        if (byteArray[i * sizeof(WCHAR)] == 0 && currentString.empty()) {
            continue; // Skip null characters at the beginning
        }
        currentString += static_cast<WCHAR>(byteArray[i * sizeof(WCHAR)]);
        if (byteArray[i * sizeof(WCHAR)] == 0) {
            strings.push_back(currentString);
            currentString.clear();
        }
    }

    return strings;
}

template <typename FuncPtr>
FuncPtr LoadFunctionFromDLL(HINSTANCE hDLL, const char* functionName) {
    FuncPtr functionPtr = reinterpret_cast<FuncPtr>(GetProcAddress(hDLL, functionName));
    if (functionPtr == nullptr) {
        std::cerr << "Failed to find function: " << functionName << std::endl;
        FreeLibrary(hDLL);
        exit(1);
    }
    return functionPtr;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <serverName>\n";
        return 1;
    }

    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
    std::wstring serverNameW = converter.from_bytes(argv[1]);

    if (serverNameW.length() > 20) {
        std::cerr << "Server name exceeds maximum length.\n";
        return 1;
    }
    wchar_t* serverName = const_cast<wchar_t*>(serverNameW.c_str());
    HINSTANCE hDLL = LoadLibrary(TEXT("winsta.dll"));
    if (hDLL == NULL) {
        std::cerr << "Failed to load winsta.dll\n";
        return 1;
    }

    auto pfnWinStationOpenServerW = LoadFunctionFromDLL<LPFN_WinStationOpenServerW>(hDLL, "WinStationOpenServerW");
    auto pfnWinStationCloseServer = LoadFunctionFromDLL<LPFN_WinStationCloseServer>(hDLL, "WinStationCloseServer");
    auto pfnWinStationEnumerateW = LoadFunctionFromDLL<LPFN_WinStationEnumerateW>(hDLL, "WinStationEnumerateW");
    auto pfnWinStationQueryInformationW = LoadFunctionFromDLL<LPFN_WinStationQueryInformationW>(hDLL, "WinStationQueryInformationW");

    // Opens Server Handle - if invalid host provided, will hang (TODO: Fix that lol)
    HANDLE hServer = pfnWinStationOpenServerW(serverName);
    if (hServer == NULL) {
        std::cerr << "Failed to open server\n";
        FreeLibrary(hDLL);
        return 1;
    }
    else {
        std::wcout << L"Server Handle: " << hServer << std::endl;
    }
    // Enumerate Server for Active Sessions, store IDs in PSESSIONIDW struct
    PSESSIONIDW pSessionIds = NULL;
    ULONG count = 0;
    BOOLEAN enumResult = pfnWinStationEnumerateW(hServer, &pSessionIds, &count);

    if (enumResult) {
        printf("Number of sessions: %lu\n", count);
        for (ULONG i = 0; i < count; i++) {
            std::wstring wsName(pSessionIds[i].WinStationName);
            std::string sessionName(wsName.begin(), wsName.end());
            WINSTATIONINFORMATIONW wsInfo{};
            ULONG ReturnLen;
            std::cout << "SessionID: " << pSessionIds[i].SessionId << std::endl;
            std::cout << "State: " << pSessionIds[i].State << std::endl;
            std::cout << "SessionName: " << sessionName << std::endl;
            if (pfnWinStationQueryInformationW &&
                pfnWinStationQueryInformationW(hServer,
                    pSessionIds[i].SessionId,
                    WinStationInformation,
                    &wsInfo,
                    sizeof(wsInfo),
                    &ReturnLen) &&
                (wsInfo.LogonId != 0))
            {
                std::vector<std::wstring> reserved2Strings = extractWideStrings(wsInfo.Reserved2, sizeof(wsInfo.Reserved2));
                std::vector<std::wstring> reserved3Strings = extractWideStrings(wsInfo.Reserved3, sizeof(wsInfo.Reserved3));
                std::wstring userName;
                if (!reserved3Strings.empty()) {
                    userName = reserved3Strings.back(); // Assuming last string in Reserved3 is username
                }
                std::string userNameStr(userName.begin(), userName.end());
                std::cout << "UserName: " << userNameStr << std::endl;
            }
            else {
                wprintf(L"Failed to query session info for SessionName: %s\n", pSessionIds[i].WinStationName);
            }
        }
        LocalFree(pSessionIds);
        //printf("Free'd Session IDs from memory.\n");
    }
    else {
        printf("Failed to enumerate sessions.\n");
    }
    BOOLEAN hServerClosed = pfnWinStationCloseServer(hServer);
    if (!hServerClosed) {
        //printf("Failed to close server handle.\n");
        return 1;
    }
    //printf("Gracefully closed server handle.\n");
    BOOLEAN hDLLClosed = FreeLibrary(hDLL);
    if (!hDLLClosed) {
        //printf("Failed to close DLL handle.\n");
        return 1;
    }
    //printf("Gracefully closed DLL handle.\n");
    return 0;
}
