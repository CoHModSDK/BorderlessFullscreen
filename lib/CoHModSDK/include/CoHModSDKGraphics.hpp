/**
 *  CoHModSDK - Shared runtime SDK for Company of Heroes
 *  Copyright (c) 2026 Tosox
 *
 *  This project is licensed under the Creative Commons
 *  Attribution-NonCommercial-NoDerivatives 4.0 International License
 *  (CC BY-NC-ND 4.0) with additional permissions.
 *
 *  Independent mods using this project only through its public interfaces
 *  are not required to use CC BY-NC-ND 4.0.
 *
 *  See the repository root LICENSE file for the full license text and
 *  additional permissions.
 */

#pragma once

#include "CoHModSDK.hpp"

#include <d3d9.h>
#include <dxgi.h>

#include <cstdint>
#include <stdexcept>

extern "C" {
    using CoHModSDKD3D9CreateDevicePreFn  = bool(*)(IDirect3D9*, UINT*, D3DDEVTYPE*, HWND*, DWORD*, D3DPRESENT_PARAMETERS*);
    using CoHModSDKD3D9CreateDevicePostFn = void(*)(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*, HRESULT, IDirect3DDevice9*);
    using CoHModSDKDXGICreateSwapChainPreFn  = bool(*)(IDXGIFactory*, IUnknown**, DXGI_SWAP_CHAIN_DESC*);
    using CoHModSDKDXGICreateSwapChainPostFn = void(*)(IDXGIFactory*, IUnknown*, DXGI_SWAP_CHAIN_DESC*, HRESULT, IDXGISwapChain*);

    struct CoHModSDKGraphicsApiV1 {
        std::uint32_t abiVersion;
        std::uint32_t size;
        bool (*OnD3D9CreateDevice)(CoHModSDKD3D9CreateDevicePreFn pre, CoHModSDKD3D9CreateDevicePostFn post);
        bool (*OnDXGICreateSwapChain)(CoHModSDKDXGICreateSwapChainPreFn pre, CoHModSDKDXGICreateSwapChainPostFn post);
    };

    COHMODSDK_RUNTIME_API bool CoHModSDK_GetGraphicsApi(std::uint32_t abiVersion, const CoHModSDKGraphicsApiV1** outApi);
}

namespace ModSDK {
    namespace GraphicsDetail {
        inline const CoHModSDKGraphicsApiV1& GetGraphicsApi() {
            static const CoHModSDKGraphicsApiV1* api = []() -> const CoHModSDKGraphicsApiV1* {
                const CoHModSDKGraphicsApiV1* resolvedApi = nullptr;
                if (!CoHModSDK_GetGraphicsApi(COHMODSDK_ABI_VERSION, &resolvedApi) || (resolvedApi == nullptr)) {
                    throw std::runtime_error("CoHModSDK graphics API is unavailable");
                }
                return resolvedApi;
            }();
            return *api;
        }
    }

    namespace Graphics {
        inline bool OnD3D9CreateDevice(CoHModSDKD3D9CreateDevicePreFn pre, CoHModSDKD3D9CreateDevicePostFn post) {
            return GraphicsDetail::GetGraphicsApi().OnD3D9CreateDevice(pre, post);
        }

        inline bool OnDXGICreateSwapChain(CoHModSDKDXGICreateSwapChainPreFn pre, CoHModSDKDXGICreateSwapChainPostFn post) {
            return GraphicsDetail::GetGraphicsApi().OnDXGICreateSwapChain(pre, post);
        }
    }
}
