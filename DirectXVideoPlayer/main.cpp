#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d3dcompiler.h>
#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <wrl/client.h>
#include "utils.h"
#include "DXShader.h"
#include "VideoSource.h"
#include "DXRenderer.h"

// FFmpeg headers
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_d3d11va.h>
}

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "avcodec.lib")
#pragma comment(lib, "avformat.lib")
#pragma comment(lib, "avutil.lib")
#pragma comment(lib, "d3dcompiler.lib")

#ifndef __ID3D11Multithread_INTERFACE_DEFINED__
#define __ID3D11Multithread_INTERFACE_DEFINED__
MIDL_INTERFACE("9b7e4e00-342c-4106-a19f-4f2704f689f0")
ID3D11Multithread : public IUnknown
{
public:
    virtual void STDMETHODCALLTYPE Enter(void) = 0;
    virtual void STDMETHODCALLTYPE Leave(void) = 0;
    virtual BOOL STDMETHODCALLTYPE SetMultithreadProtected(BOOL bMTProtect) = 0;
    virtual BOOL STDMETHODCALLTYPE GetMultithreadProtected(void) = 0;
};
#endif

//struct Vertex { float x, y, z; float u, v; };


// --- Window & Main ---
DXRenderer g_Renderer;
bool g_Space = false;
bool isFullscreen = false;
WINDOWPLACEMENT g_WindowedPlacement = { sizeof(WINDOWPLACEMENT) };

void ToggleFullscreen(HWND hwnd) {
    isFullscreen = !isFullscreen;
    if (isFullscreen) {
        // Save windowed placement and style, then go borderless fullscreen
        GetWindowPlacement(hwnd, &g_WindowedPlacement);
        LONG style = GetWindowLong(hwnd, GWL_STYLE);
        SetWindowLong(hwnd, GWL_STYLE, style & ~WS_OVERLAPPEDWINDOW);
        MONITORINFO mi = { sizeof(mi) };
        GetMonitorInfo(MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY), &mi);
        SetWindowPos(hwnd, HWND_TOP,
            mi.rcMonitor.left, mi.rcMonitor.top,
            mi.rcMonitor.right - mi.rcMonitor.left,
            mi.rcMonitor.bottom - mi.rcMonitor.top,
            SWP_NOOWNERZORDER | SWP_FRAMECHANGED);

        while (ShowCursor(FALSE) >= 0);

    } else {
        // Restore windowed style and placement
        LONG style = GetWindowLong(hwnd, GWL_STYLE);
        SetWindowLong(hwnd, GWL_STYLE, style | WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(hwnd, &g_WindowedPlacement);
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);

        while (ShowCursor(TRUE) < 0);
    }
}

LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_DESTROY) PostQuitMessage(0);
    if (m == WM_KEYDOWN && w == VK_SPACE) g_Space = true;
    if (m == WM_KEYDOWN && w == 'F')
		ToggleFullscreen(h);

    if (m == WM_SIZE && g_Renderer.swapChain) g_Renderer.Resize(0, 0);
    return DefWindowProc(h, m, w, l);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
//int main() 
{
    WNDCLASS wc = { 0 }; wc.lpfnWndProc = WndProc; wc.lpszClassName = L"VP"; wc.hInstance = GetModuleHandle(NULL);
    RegisterClass(&wc);
    HWND hwnd = CreateWindow(L"VP", L"OOP Video Player", WS_OVERLAPPEDWINDOW, 100, 100, 1280, 720, 0, 0, wc.hInstance, 0);

    if (!g_Renderer.Initialize(hwnd)) return -1;
    DXShader videoShader;
    /*videoShader.Init(g_Renderer.device, shaderSource);*/
	videoShader.LoadFromFile(g_Renderer.device, L"shaders.hlsl");

    VideoSource bgVideo, fgVideo;
    // Update these paths to your local files
    //bgVideo.OpenFile("Videos/13.mp4", g_Renderer.device, g_Renderer.context);
    bgVideo.OpenFile("Videos/toyota_positional_test_v3_max_speed_accel_bg.mp4", g_Renderer.device, g_Renderer.context);
    fgVideo.OpenFile("Videos/1.mp4", g_Renderer.device, g_Renderer.context);
    
	bgVideo.looped = true;
	bgVideo.Play(GetTimeStd());

    //fgVideo.looped = true;

    bool fgActive = false;
    double fgPts = 0;
    ShowWindow(hwnd, SW_SHOW);
    ToggleFullscreen(hwnd);

    MSG msg = { 0 };
    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) { DispatchMessage(&msg); continue; }

        if (g_Space) 
        { 
            g_Space = false; 
            fgActive = true; 
			fgVideo.Rewind();
			fgVideo.Play(GetTimeStd());
        }

        bgVideo.GetNextFrame(g_Renderer.context);

        if (fgActive) 
        { 
            if (!fgVideo.GetNextFrame(g_Renderer.context)) 
                fgActive = false;
        }

        RECT rc; GetClientRect(hwnd, &rc);
        float w = (float)(rc.right - rc.left);
        float h = (float)(rc.bottom - rc.top);

        g_Renderer.BeginFrame();
        g_Renderer.DrawVideo(bgVideo, videoShader, 1.0f, false, w, h);
        
        if (fgActive) 
        {
            float fade = 1.0f;

            if (fgVideo.internalPTS < fgVideo.fadeInDuration)
            {
                fade = (float)fgVideo.internalPTS / fgVideo.fadeInDuration;
            }
            else if (fgVideo.duration - fgVideo.internalPTS < fgVideo.fadeOutDuration)
            {
                fade = (float)(fgVideo.duration - fgVideo.internalPTS) / fgVideo.fadeOutDuration;
            }

            g_Renderer.DrawVideo(fgVideo, videoShader, max(0.0f, fade), true, w, h);
        }
        g_Renderer.EndFrame();
    }
    return 0;
}