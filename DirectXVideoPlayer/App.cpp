#include "App.h"
#include "IRenderer.h"
#include "DXRenderer.h"
#include "DXShader.h"
#include "VideoSource.h"
#include "utils.h"
#include "NetworkManager.h"


// Initialize the static AppState member
AppState App::state;

App::App(int width, int height)
{
    wndClass.lpfnWndProc = WndProc; 
    wndClass.lpszClassName = L"VP"; 
    wndClass.hInstance = GetModuleHandle(NULL);
    RegisterClass(&wndClass);
    window = CreateWindow(L"VP", L"OOP Video Player", WS_OVERLAPPEDWINDOW, 100, 100, width, height, 0, 0, wndClass.hInstance, this);

    DXRenderer* dxRenderer = new DXRenderer();
    dxRenderer->Initialize(window);
    renderer = dxRenderer;
    state.renderer = renderer;

    videoShader = new DXShader();
    videoShader->LoadFromFile(renderer->GetDevice(), L"shaders.hlsl");

    bgVideo = new VideoSource();
	fgVideo = new VideoSource();

    //bgVideo.OpenFile("Videos/13.mp4", g_Renderer.device, g_Renderer.context);
    bgVideo->OpenFile("Videos/toyota_positional_test_v3_max_speed_accel_bg.mp4", renderer->GetDevice(), renderer->GetContext());
    fgVideo->OpenFile("Videos/1.mp4", renderer->GetDevice(), renderer->GetContext());
    
    bgVideo->looped = true;
    bgVideo->Play(GetTimeStd());
    //fgVideo->looped = true;

	ShowWindow(window, SW_SHOW);
	ToggleFullscreen(window);

    state.networkMgr = new NetworkManager("127.0.0.1", 5555, this);
	state.networkMgr->Start();
}

App::~App()
{
    if (renderer)
        delete renderer;

    if (videoShader)
		delete videoShader;

    if (bgVideo)
        delete bgVideo;

    if (fgVideo)
        delete fgVideo;
}

void App::Run()
{
    while (msg.message != WM_QUIT)
    {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            DispatchMessage(&msg);
            continue;
        }

        if (spaceBarPressed)
        {
			spaceBarPressed = false;
            fgActive = true;
			fgVideo->Rewind();
			fgVideo->Play(GetTimeStd());
        }

		bgVideo->GetNextFrame(renderer->GetContext());

        if (fgActive)
        {
            if (!fgVideo->GetNextFrame(renderer->GetContext()))
                fgActive = false;
		}

        RECT rc; 
        GetClientRect(window, &rc);
        float w = (float)(rc.right - rc.left);
        float h = (float)(rc.bottom - rc.top);

		renderer->BeginFrame();
		renderer->DrawVideo(bgVideo, videoShader, 1.0f, false, w, h);

        if (fgActive)
        {
            float fade = 1.0f;
            if (fgVideo->internalPTS < fgVideo->fadeInDuration)
                fade = (float)fgVideo->internalPTS / fgVideo->fadeInDuration;
            else if (fgVideo->duration - fgVideo->internalPTS < fgVideo->fadeOutDuration)
                fade = (float)(fgVideo->duration - fgVideo->internalPTS) / fgVideo->fadeOutDuration;

            renderer->DrawVideo(fgVideo, videoShader, max(0.0f, fade), true, w, h);
        }

        renderer->EndFrame();
	}
}

VideoSource* App::GetBackgroundVideo()
{
	return nullptr;
}

std::vector<float> App::GetPositions()
{
	return std::vector<float>();
}

double App::GetLastPTS()
{
	return 0.0;
}

int64_t App::GetBGCaptureTimeNS()
{
	return 0;
}

LRESULT App::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    App* self = nullptr;

    if (msg == WM_NCCREATE)
    {
        CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lp);
        self = reinterpret_cast<App*>(cs->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    else
    {
        self = reinterpret_cast<App*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }

    if (self)
        return self->HandleMessage(hwnd, msg, wp, lp);

    return DefWindowProc(hwnd, msg, wp, lp);
}

LRESULT App::HandleMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_DESTROY) 
        PostQuitMessage(0);
    
    if (msg == WM_KEYDOWN && wp == VK_SPACE) 
        spaceBarPressed = true;
    
    if (msg == WM_KEYDOWN && wp == 'F')
        ToggleFullscreen(hwnd);

    if (msg == WM_SIZE && renderer->GetSwapChain()) 
        renderer->Resize(0, 0);

    return DefWindowProc(hwnd, msg, wp, lp);
}

void App::ToggleFullscreen(HWND hwnd)
{
    isFullscreen = !isFullscreen;
    if (isFullscreen) {
        // Save windowed placement and style, then go borderless fullscreen
        GetWindowPlacement(hwnd, &windowPlacement);
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

    }
    else {
        // Restore windowed style and placement
        LONG style = GetWindowLong(hwnd, GWL_STYLE);
        SetWindowLong(hwnd, GWL_STYLE, style | WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(hwnd, &windowPlacement);
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);

        while (ShowCursor(TRUE) < 0);
    }
}
