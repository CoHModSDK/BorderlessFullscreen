#include "CoHModSDK.hpp"

#include <Windows.h>
#include <d3d9.h>
#include <dxgi.h>
#include <shellapi.h>

#include <algorithm>
#include <cstdlib>

namespace {
    using Direct3DCreate9Fn = IDirect3D9*(WINAPI*)(UINT);
    using D3D9CreateDeviceFn = HRESULT(__stdcall*)(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*, IDirect3DDevice9**);
    using CreateDXGIFactoryFn = HRESULT(WINAPI*)(REFIID, void**);
    using DXGIFactoryCreateSwapChainFn = HRESULT(__stdcall*)(IDXGIFactory*, IUnknown*, DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**);

    Direct3DCreate9Fn oFnDirect3DCreate9 = nullptr;
    D3D9CreateDeviceFn oFnD3D9CreateDevice = nullptr;

    CreateDXGIFactoryFn oFnCreateDXGIFactory = nullptr;
    DXGIFactoryCreateSwapChainFn oFnDxgiCreateSwapChain = nullptr;

    bool HasLaunchParameter(const wchar_t* parameter) {
        if (parameter == nullptr) {
            return false;
        }

        int argc = 0;
        LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (arguments == nullptr) {
            return false;
        }

        bool found = false;
        for (int index = 1; index < argc; ++index) {
            if (CompareStringOrdinal(arguments[index], -1, parameter, -1, TRUE) == CSTR_EQUAL) {
                found = true;
                break;
            }
        }

        LocalFree(arguments);
        return found;
    }

    template <typename T>
    T ResolveExport(HMODULE module, const char* exportName) {
        if ((module == nullptr) || (exportName == nullptr)) {
            return nullptr;
        }

        return reinterpret_cast<T>(GetProcAddress(module, exportName));
    }

    template <typename T>
    bool PatchVTableEntry(void* instance, std::size_t slotIndex, T detour, T* originalFunction) {
        if (instance == nullptr) {
            return false;
        }

        auto*** const asVTable = reinterpret_cast<void***>(instance);
        if ((*asVTable == nullptr) || ((*asVTable)[slotIndex] == nullptr)) {
            return false;
        }

        if (*originalFunction == nullptr) {
            *originalFunction = reinterpret_cast<T>((*asVTable)[slotIndex]);
        }

        if ((*asVTable)[slotIndex] == reinterpret_cast<void*>(detour)) {
            return true;
        }

        ModSDK::Memory::PatchMemory(&(*asVTable)[slotIndex], &detour, sizeof(detour));
        return true;
    }

    struct WindowSearchContext {
        DWORD processId = 0;
        HWND window = nullptr;
    };

    BOOL CALLBACK EnumProcessWindows(HWND window, LPARAM parameter) {
        auto* const context = reinterpret_cast<WindowSearchContext*>(parameter);
        if (context == nullptr) {
            return FALSE;
        }

        DWORD processId = 0;
        GetWindowThreadProcessId(window, &processId);
        if (processId != context->processId) {
            return TRUE;
        }

        if (GetWindow(window, GW_OWNER) != nullptr) {
            return TRUE;
        }

        context->window = window;
        return FALSE;
    }

    HWND FindProcessTopLevelWindow() {
        WindowSearchContext context = {
            .processId = GetCurrentProcessId(),
            .window = nullptr,
        };

        EnumWindows(&EnumProcessWindows, reinterpret_cast<LPARAM>(&context));
        return context.window;
    }

    bool GetDesktopRect(HWND window, RECT& rect) {
        HMONITOR monitor = nullptr;
        if ((window != nullptr) && IsWindow(window)) {
            monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
        } else {
            HWND fallbackWindow = FindProcessTopLevelWindow();
            if (fallbackWindow != nullptr) {
                monitor = MonitorFromWindow(fallbackWindow, MONITOR_DEFAULTTONEAREST);
            }
        }

        if (monitor != nullptr) {
            MONITORINFO monitorInfo = {};
            monitorInfo.cbSize = sizeof(monitorInfo);
            if (GetMonitorInfoW(monitor, &monitorInfo)) {
                rect = monitorInfo.rcMonitor;
                return true;
            }
        }

        SetRect(&rect, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
        return (rect.right > rect.left) && (rect.bottom > rect.top);
    }

    bool RectMatches(const RECT& left, const RECT& right, LONG tolerance) {
        return (std::abs(left.left - right.left) <= tolerance) &&
               (std::abs(left.top - right.top) <= tolerance) &&
               (std::abs(left.right - right.right) <= tolerance) &&
               (std::abs(left.bottom - right.bottom) <= tolerance);
    }

    void ApplyBorderlessWindow(HWND window) {
        if ((window == nullptr) || !IsWindow(window)) {
            window = FindProcessTopLevelWindow();
            if ((window == nullptr) || !IsWindow(window)) {
                return;
            }
        }

        RECT desktopRect = {};
        if (!GetDesktopRect(window, desktopRect)) {
            return;
        }

        const LONG currentStyle = GetWindowLongA(window, GWL_STYLE);
        const LONG currentExStyle = GetWindowLongA(window, GWL_EXSTYLE);

        LONG desiredStyle = currentStyle;
        desiredStyle &= ~(WS_CAPTION | WS_THICKFRAME | WS_BORDER | WS_DLGFRAME | WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_SYSMENU);
        desiredStyle |= WS_POPUP | WS_VISIBLE;

        LONG desiredExStyle = currentExStyle;
        desiredExStyle &= ~(WS_EX_CLIENTEDGE | WS_EX_DLGMODALFRAME | WS_EX_STATICEDGE | WS_EX_WINDOWEDGE);

        RECT currentRect = {};
        GetWindowRect(window, &currentRect);
        const bool needsMoveOrResize = !RectMatches(currentRect, desktopRect, 1);

        if ((desiredStyle == currentStyle) && (desiredExStyle == currentExStyle) && !needsMoveOrResize) {
            return;
        }

        if (desiredStyle != currentStyle) {
            SetWindowLongA(window, GWL_STYLE, desiredStyle);
        }

        if (desiredExStyle != currentExStyle) {
            SetWindowLongA(window, GWL_EXSTYLE, desiredExStyle);
        }

        SetWindowPos(
            window,
            nullptr,
            desktopRect.left,
            desktopRect.top,
            desktopRect.right - desktopRect.left,
            desktopRect.bottom - desktopRect.top,
            SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER | SWP_SHOWWINDOW
        );
        ShowWindow(window, SW_SHOW);
    }

    void ForceWindowedPresentParameters(IDirect3D9* direct3D, UINT adapter, HWND window, D3DPRESENT_PARAMETERS* params) {
        if (params == nullptr) {
            return;
        }

        RECT desktopRect = {};
        GetDesktopRect(window != nullptr ? window : params->hDeviceWindow, desktopRect);

        params->Windowed = TRUE;
        params->FullScreen_RefreshRateInHz = 0u;
        params->BackBufferWidth = std::max<LONG>(desktopRect.right - desktopRect.left, 1);
        params->BackBufferHeight = std::max<LONG>(desktopRect.bottom - desktopRect.top, 1);

        HWND deviceWindow = window != nullptr ? window : params->hDeviceWindow;
        if ((deviceWindow != nullptr) && IsWindow(deviceWindow)) {
            params->hDeviceWindow = deviceWindow;
        }

        if (direct3D != nullptr) {
            D3DDISPLAYMODE displayMode = {};
            if (SUCCEEDED(direct3D->GetAdapterDisplayMode(adapter, &displayMode))) {
                params->BackBufferFormat = displayMode.Format;
            }
        }
    }

    void ForceWindowedModeDesc(HWND window, DXGI_MODE_DESC& description) {
        RECT desktopRect = {};
        GetDesktopRect(window, desktopRect);

        description.Width = static_cast<UINT>(std::max<LONG>(desktopRect.right - desktopRect.left, 1));
        description.Height = static_cast<UINT>(std::max<LONG>(desktopRect.bottom - desktopRect.top, 1));
        description.RefreshRate.Numerator = 0u;
        description.RefreshRate.Denominator = 0u;
        description.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
        description.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
    }

    void ForceWindowedSwapChainDesc(DXGI_SWAP_CHAIN_DESC* description) {
        if (description == nullptr) {
            return;
        }

        description->Windowed = TRUE;
        description->Flags &= ~DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
        ForceWindowedModeDesc(description->OutputWindow, description->BufferDesc);
    }

    HRESULT __stdcall HookedD3D9CreateDevice(IDirect3D9* _this, UINT adapter, D3DDEVTYPE deviceType, HWND window,
        DWORD flags, D3DPRESENT_PARAMETERS* params, IDirect3DDevice9** device) {
        HWND targetWindow = window;
        if ((targetWindow == nullptr) && (params != nullptr)) {
            targetWindow = params->hDeviceWindow;
        }

        ForceWindowedPresentParameters(_this, adapter, targetWindow, params);

        const HRESULT result = oFnD3D9CreateDevice(_this, adapter, deviceType, window, flags, params, device);
        if (SUCCEEDED(result)) {
            ApplyBorderlessWindow(targetWindow);
        }

        return result;
    }

    IDirect3D9* WINAPI HookedDirect3DCreate9(UINT version) {
        IDirect3D9* direct3D = oFnDirect3DCreate9(version);
        if (direct3D != nullptr) {
            PatchVTableEntry(direct3D, 16u, &HookedD3D9CreateDevice, &oFnD3D9CreateDevice);
        }

        return direct3D;
    }

    HRESULT __stdcall HookedDXGIFactoryCreateSwapChain(IDXGIFactory* _this, IUnknown* device,
        DXGI_SWAP_CHAIN_DESC* description, IDXGISwapChain** swapChain) {
        DXGI_SWAP_CHAIN_DESC adjustedDescription = {};
        DXGI_SWAP_CHAIN_DESC* descriptionToUse = description;

        if (description != nullptr) {
            adjustedDescription = *description;
            ForceWindowedSwapChainDesc(&adjustedDescription);
            descriptionToUse = &adjustedDescription;
        }

        const HRESULT result = oFnDxgiCreateSwapChain(_this, device, descriptionToUse, swapChain);
        if (SUCCEEDED(result)) {
            ApplyBorderlessWindow(descriptionToUse != nullptr ? descriptionToUse->OutputWindow : nullptr);
        }

        return result;
    }

    HRESULT WINAPI HookedCreateDXGIFactory(REFIID riid, void** factory) {
        const HRESULT result = oFnCreateDXGIFactory(riid, factory);
        if (SUCCEEDED(result) && (factory != nullptr) && (*factory != nullptr)) {
            PatchVTableEntry(reinterpret_cast<IDXGIFactory*>(*factory), 10u, &HookedDXGIFactoryCreateSwapChain, &oFnDxgiCreateSwapChain);
        }

        return result;
    }

    bool InstallD3D9ExportHook() {
        HMODULE hModule = LoadLibraryA("d3d9.dll");
        if (hModule == nullptr) {
			ModSDK::Dialogs::ShowError("Failed to load d3d9.dll");
            return false;
        }

        const auto tDirect3DCreate9 = ResolveExport<void*>(hModule, "Direct3DCreate9");
        if (tDirect3DCreate9 == nullptr) {
			ModSDK::Dialogs::ShowError("Failed to find Direct3DCreate9 export");
            return false;
        }

        if (!ModSDK::Hooks::CreateHook(
                tDirect3DCreate9,
                reinterpret_cast<void*>(&HookedDirect3DCreate9),
                reinterpret_cast<void**>(&oFnDirect3DCreate9))) {
			ModSDK::Dialogs::ShowError("Failed to create Direct3DCreate9 export hook");
            return false;
        }

        if (!ModSDK::Hooks::EnableHook(tDirect3DCreate9)) {
			ModSDK::Dialogs::ShowError("Failed to enable Direct3DCreate9 export hook");
            return false;
        }

        return true;
    }

    bool InstallDxgiExportHook() {
        HMODULE hModule = LoadLibraryA("dxgi.dll");
        if (hModule == nullptr) {
			ModSDK::Dialogs::ShowError("Failed to load dxgi.dll");
            return false;
        }

        const auto tCreateDxgiFactory = ResolveExport<void*>(hModule, "CreateDXGIFactory");
        if (tCreateDxgiFactory == nullptr) {
			ModSDK::Dialogs::ShowError("Failed to find CreateDXGIFactory export");
            return false;
        }

        if (!ModSDK::Hooks::CreateHook(
                tCreateDxgiFactory,
                reinterpret_cast<void*>(&HookedCreateDXGIFactory),
                reinterpret_cast<void**>(&oFnCreateDXGIFactory))) {
			ModSDK::Dialogs::ShowError("Failed to create CreateDXGIFactory export hook");
            return false;
        }

        if (!ModSDK::Hooks::EnableHook(tCreateDxgiFactory)) {
			ModSDK::Dialogs::ShowError("Failed to enable CreateDXGIFactory export hook");
            return false;
        }

        return true;
    }

    bool OnInitialize() {
        if (!HasLaunchParameter(L"-borderless")) {
            ModSDK::Runtime::Log(CoHModSDKLogLevel_Warning, "Borderless Fullscreen is inactive because -borderless was not provided");
            return true;
        }

        if (!InstallD3D9ExportHook()) {
            return false;
        }

        if (!InstallDxgiExportHook()) {
            return false;
        }

        return true;
    }

    bool OnModsLoaded() {
        return true;
    }

    void OnShutdown() {}

    const CoHModSDKModuleV1 kModule = {
        .abiVersion = COHMODSDK_ABI_VERSION,
        .size = sizeof(CoHModSDKModuleV1),
        .modId = "de.tosox.borderlessfullscreen",
        .name = "Borderless Fullscreen",
        .version = "1.0.0",
        .author = "Tosox",
        .OnInitialize = &OnInitialize,
        .OnModsLoaded = &OnModsLoaded,
        .OnShutdown = &OnShutdown,
    };
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved) {
    DisableThreadLibraryCalls(hModule);
    return TRUE;
}

COHMODSDK_EXPORT_MODULE(kModule);
