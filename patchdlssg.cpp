// compile option: 
//  cl /LD /DDEBUG patchDLSSG.cpp /link /DEF:winmm.def winmm.lib /OUT:patchdlssg.dll [debug]
//  cl /LD patchDLSSG.cpp /link /DEF:winmm.def winmm.lib /OUT:patchdlssg.dll [release]

#include <windows.h>
#include <Psapi.h>
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include "MinHook.h"
#include <cstring>
#include <vector>

void Log(const char* fmt, ...)
{
#ifndef DEBUG
    return;
#endif
    FILE* f = nullptr;

    fopen_s(&f, "D:\\patchdlssg.log", "a");

    if (!f)
        return;

    SYSTEMTIME st{};
    GetLocalTime(&st);

    fprintf(
        f,
        "[%02d:%02d:%02d.%03d] ",
        st.wHour,
        st.wMinute,
        st.wSecond,
        st.wMilliseconds
    );

    va_list args;
    va_start(args, fmt);
    vfprintf(f, fmt, args);
    va_end(args);

    fprintf(f, "\n");

    fflush(f);
    fclose(f);
}

namespace sl
{
    struct StructType
    {
        uint32_t data1;
        uint16_t data2;
        uint16_t data3;
        uint8_t  data4[8];
    };

    enum class Result : uint32_t
    {
        eOk = 0
    };

    enum class DLSSGMode : uint32_t
    {
        eOff,
        eOn,
        eAuto,
        eDynamic,
        eCount
    };

    enum class DLSSGFlags : uint32_t
    {
        eShowOnlyInterpolatedFrame = 1 << 0,
        eDynamicResolutionEnabled = 1 << 1,
        eRequestVRAMEstimate = 1 << 2,
        eRetainResourcesWhenOff = 1 << 3,
        eEnableFullscreenMenuDetection = 1 << 4,
    };

    struct ViewportHandle
    {
        uint64_t handle;
    };

    //using PFunOnAPIErrorCallback = void(const APIError& lastError);

    struct DLSSGOptions
    {
        void* base_structure;
        StructType structType;
        size_t structVersion;
        DLSSGMode mode;
        uint32_t numFramesToGenerate;
        DLSSGFlags flags{};
        uint32_t dynamicResWidth{};
        uint32_t dynamicResHeight{};
        uint32_t numBackBuffers{};
        uint32_t mvecDepthWidth{};
        uint32_t mvecDepthHeight{};
        uint32_t colorWidth{};
        uint32_t colorHeight{};
        uint32_t colorBufferFormat{};
        uint32_t mvecBufferFormat{};
        uint32_t depthBufferFormat{};
        uint32_t hudLessBufferFormat{};
        uint32_t uiBufferFormat{};
        //PFunOnAPIErrorCallback* onErrorCallback{};
    };
}

static const char targetstr[] = "r.Streamline.DLSSG.Enable 0";

// make DLSSG still working udner menu or other temporary scenes, it's optional
bool patchDLSSGv1() {
    Log("[patchDLSSGv1] start");
    HMODULE hModule = GetModuleHandleW(L"NGR-Win64-Shipping.exe");
    if (!hModule) {
        Log("[patchDLSSGv1] NGR-Win64-Shipping.exe module not found");
        return false;
    }

    MODULEINFO minfo{};
    if (!GetModuleInformation(GetCurrentProcess(), hModule, &minfo, sizeof(minfo))) {
        Log("[patchDLSSGv1] GetModuleInformation failed");
        return false;
    }

    uint8_t* base = reinterpret_cast<uint8_t*>(minfo.lpBaseOfDll);
    size_t size = minfo.SizeOfImage;
    Log("[patchDLSSGv1] module base: %d, module size: %d", base, size);

    const size_t targetlen = sizeof(targetstr) - 1;
    char* found = nullptr;

    for (size_t i = 0; i <= size - targetlen; ++i) {
        if (memcmp(base + i, targetstr, targetlen) == 0) {
            found = reinterpret_cast<char*>(base + i);
            break;
        }
    }

    if (!found) {
        Log("[patchDLSSGv1] target not found");
        return false;
    }

    Log("[patchDLSSGv1] Found target string at %p", found);
    Log("[patchDLSSGv1] Current string is '%s'", found);

    char* valoff = strrchr(found, '0');
    if (!valoff) {
        Log("[patchDLSSGv1] couldn't locate final '0'");
        return false;
    }

    DWORD oldProtect{};
    if (!VirtualProtect(valoff, 1, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        Log("[patchDLSSGv1] VirtualProtect failed");
        return false;
    }
    *valoff = '1';
    VirtualProtect(valoff, 1, oldProtect, &oldProtect);
    Log("[patchDLSSGv1] Patched string = '%s'", found);
    return true;
}



namespace sl
{
    using Feature = uint32_t;

    constexpr Feature kFeatureDLSS = 0;
    constexpr Feature kFeatureReflex = 3;
    constexpr Feature kFeaturePCL = 4;
    constexpr Feature kFeatureDLSS_G = 1000;
}

using PFN_slIsFeatureLoaded =
sl::Result(*)(sl::Feature feature, bool& enabled);

void checkStreamline() {
    HMODULE hSL = GetModuleHandleW(L"sl.interposer.dll");
    if (!hSL) {
        Log("sl.interposer.dll not loaded");
        return;
    }

    auto fnSLIsFeatureLoaded = reinterpret_cast<PFN_slIsFeatureLoaded>(GetProcAddress(hSL, "slIsFeatureLoaded"));

    if (!fnSLIsFeatureLoaded) {
        Log("slIsFeatureLoaded not found");
        return;
    }

    bool enabled = false;
    sl::Result res = fnSLIsFeatureLoaded(sl::kFeatureDLSS_G, enabled);
    Log("slFeatureLoaded:[DLSSG] res=%d enabled=%d", res, enabled);

    enabled = false;
    res = fnSLIsFeatureLoaded(sl::kFeatureReflex, enabled);
    Log("slFeatureLoaded:[Reflex] res=%d enabled=%d", res, enabled);

    enabled = false;
    res = fnSLIsFeatureLoaded(sl::kFeaturePCL, enabled);
    Log("slFeatureLoaded:[PCL] res=%d enabled=%d", res, enabled);

    enabled = false;
    res = fnSLIsFeatureLoaded(sl::kFeatureDLSS, enabled);
    Log("slFeatureLoaded:[DLSS] res=%d enabled=%d", res, enabled);
}


HMODULE FindsldlssgModule() {
    HANDLE hProcess = GetCurrentProcess();

    // set timeout for 60s
    for (DWORD retry = 0; retry < 600; retry++) {
        HMODULE mods[1024];
        DWORD needed = 0;

        if (!EnumProcessModulesEx(hProcess, mods, sizeof(mods), &needed, LIST_MODULES_ALL)) {
            Log("[FindsldlssgModule] EnumProcessModules failed");
            Sleep(100);
            continue;
        }

        for (DWORD i = 0; i < needed / sizeof(HMODULE); i++) {
            wchar_t path[32768];
            if (K32GetModuleFileNameExW(hProcess, mods[i], path, _countof(path))) {
                // The process would load following modules, we want to pick the module with its path including "sl_dlss_g".
                //  C:\ProgramData\NVIDIA\NGX\models\sl_reflex_0\versions\133888\files\1B0_E658703.dll   
                //  C:\ProgramData\NVIDIA\NGX\models\sl_dlss_g_0\versions\133888\files\1B0_E658703.dll   
                //  C:\ProgramData\NVIDIA\NGX\models\sl_common_0\versions\133888\files\1B0_E658703.dll
                //  C:\ProgramData\NVIDIA\NGX\models\sl_pcl_0\versions\133888\files\1B0_E658703.dll   
                if (wcsstr(path, L"sl_dlss_g")) {
                    Log("[FindsldlssgModule] sl.dlssg.dll found, module path=%ls", path);
                    return mods[i];
                }
            }
        }

        // sl.dlssg.dll not loaded yet, wait for 100ms
        Sleep(100);
    }

    Log("[FindsldlssgModule] sl.dlssg.dll not loaded after 60s");
    return nullptr;
}



// patch SLDLSSGModeFromCvar
bool patchDLSSG() {
    Log("[patchDLSSG] start");
    HMODULE hModule;
    while (true) {
        hModule = GetModuleHandleW(L"NGR-Win64-Shipping.exe");
        if (hModule != nullptr) {
            Log("[patchDLSSG] Found shipping module: %p", hModule);
            break;
        }
        Sleep(50);
    }

    MODULEINFO minfo{};
    if (!GetModuleInformation(GetCurrentProcess(), hModule, &minfo, sizeof(minfo))) {
        Log("[patchDLSSG] GetModuleInformation failed");
        return false;
    }

    uint8_t* base = reinterpret_cast<uint8_t*>(minfo.lpBaseOfDll);
    size_t msize = static_cast<size_t>(minfo.SizeOfImage);
    Log("[patchDLSSG] module base: %p, module size: %zu", base, msize);

    // 1.take all matched addresses,two results expected
    static const uint8_t pattern[] =
    {
        0x48, 0x83, 0xEC, 0x38,
        0x33, 0xD2,
        0x48, 0x8D, 0x0D
    };
    static const uint8_t patched[] =
    {
        0xB8, 0x01, 0x00, 0x00, 0x00,
        0xC3,
    };

    std::vector<uint8_t*> matched;
    for (size_t i = 0; i < msize - sizeof(pattern); ++i) {
        if (memcmp(base+i, pattern, sizeof(pattern)) == 0) {
            matched.push_back(base + i);
        }
    }

    Log("[patchDLSSG] got %zu matched results", matched.size());
    for (uint8_t* addr : matched) {
        Log("[patchDLSSG] matched address: %p", addr);
    }

    // first function is expected to be SLDLSSGModeFromCvar
    bool done = false;
    uint8_t* addr = matched[0];
    DWORD oldProtect{};
    if (VirtualProtect(addr, sizeof(patched), PAGE_EXECUTE_READWRITE, &oldProtect)) {
        memcpy(addr, patched, sizeof(patched));
        //FlushInstructionCache(GetCurrentProcess(), addr, sizeof(patched));
        VirtualProtect(addr, sizeof(patched), oldProtect, &oldProtect);
        Log("[patchDLSSG] patched addr:%p", addr);
        done = true;
    }
    else {
        Log("[patchDLSSG] VirtualProtect failed at %p", addr);
        done = false;
    }

    Log("[patchDLSSG] end");
    return done;
}

DWORD WINAPI ThreadProc(LPVOID)
{
    Log("ThreadProc begin");

    patchDLSSG();
    patchDLSSGv1();

    while (false) {
        Log("-------------------------StreamlineWatcher Begin--------------------------");
        checkStreamline();
        Sleep(10000);
        Log("-------------------------StreamlineWatcher End--------------------------");
    }

    return 0;
}

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hinst);

        CreateThread(
            NULL,
            0,
            ThreadProc,
            NULL,
            0,
            NULL
        );
    }
    return TRUE;
}
