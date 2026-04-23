#include "CoHModSDK.hpp"
#include "CoHModSDKGraphics.hpp"

#include <Windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cstdlib>

namespace {
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
            return;
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

    bool OnBeforeD3D9CreateDevice(IDirect3D9* direct3D, UINT* adapter, D3DDEVTYPE*, HWND* window,
                                  DWORD*, D3DPRESENT_PARAMETERS* params) {
        HWND targetWindow = (window != nullptr) ? *window : nullptr;
        if ((targetWindow == nullptr) && (params != nullptr)) {
            targetWindow = params->hDeviceWindow;
        }
        ForceWindowedPresentParameters(direct3D, (adapter != nullptr) ? *adapter : 0, targetWindow, params);
        return true;
    }

    void OnAfterD3D9CreateDevice(IDirect3D9*, UINT, D3DDEVTYPE, HWND window, DWORD,
                                 D3DPRESENT_PARAMETERS* params, HRESULT result, IDirect3DDevice9*) {
        if (SUCCEEDED(result)) {
            HWND targetWindow = window;
            if ((targetWindow == nullptr) && (params != nullptr)) {
                targetWindow = params->hDeviceWindow;
            }
            ApplyBorderlessWindow(targetWindow);
        }
    }

    bool OnBeforeDXGICreateSwapChain(IDXGIFactory*, IUnknown**, DXGI_SWAP_CHAIN_DESC* description) {
        ForceWindowedSwapChainDesc(description);
        return true;
    }

    void OnAfterDXGICreateSwapChain(IDXGIFactory*, IUnknown*, DXGI_SWAP_CHAIN_DESC* description,
                                    HRESULT result, IDXGISwapChain*) {
        if (SUCCEEDED(result) && (description != nullptr)) {
            ApplyBorderlessWindow(description->OutputWindow);
        }
    }

    bool OnInitialize() {
        if (!HasLaunchParameter(L"-borderless")) {
            ModSDK::Runtime::LogWarning("Borderless Fullscreen is inactive because -borderless was not provided");
            return true;
        }

        ModSDK::Graphics::OnD3D9CreateDevice(&OnBeforeD3D9CreateDevice, &OnAfterD3D9CreateDevice);
        ModSDK::Graphics::OnDXGICreateSwapChain(&OnBeforeDXGICreateSwapChain, &OnAfterDXGICreateSwapChain);

        return true;
    }

    void OnShutdown() {}

    const CoHModSDKModuleV1 kModule = {
        .abiVersion = COHMODSDK_ABI_VERSION,
        .size = sizeof(CoHModSDKModuleV1),
        .modId = "de.tosox.borderlessfullscreen",
        .name = "Borderless Fullscreen",
        .version = "1.1.0",
        .author = "Tosox",
        .OnInitialize = &OnInitialize,
        .OnShutdown = &OnShutdown,
    };
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved) {
    DisableThreadLibraryCalls(hModule);
    return TRUE;
}

COHMODSDK_EXPORT_MODULE(kModule);
