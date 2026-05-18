#include "App.h"
#include "IRenderer.h"
#include "DXRenderer.h"
#include "DXShader.h"
#include "VideoSource.h"
#include "utils.h"
#include "NetworkManager.h"
#include "ContentManager.h"
#include <iostream>


// Initialize the static AppState member
AppState App::state;

App::App(int width, int height)
{
    ContentManager contentMgr;
    contentMgr.LoadVideoContentFromFolder(".\\Videos");
    if (contentMgr.GetVideoContents().empty())
    {
        /*std::cerr << "No .mp4 files found." << std::endl;*/
		MessageBoxA(nullptr, "No .mp4 files found in the Videos folder.", "Error", MB_ICONERROR);
    }



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

    for (const auto& videoContent : contentMgr.GetVideoContents())
    {
        VideoSource* videoSource = new VideoSource();
        if (videoSource->OpenFile(videoContent.filename, renderer->GetDevice(), renderer->GetContext()))
        {
            videoSource->fadeInDuration = videoContent.fadeInDuration;
            videoSource->fadeOutDuration = videoContent.fadeOutDuration;
            videoSource->looped = videoContent.looped;
            videoSource->positions = videoContent.positions;
            state.sources.push_back(videoSource);
        }
        else
        {
            std::cerr << "Failed to open video: " << videoContent.filename << std::endl;
            delete videoSource;
        }
    }

 //   bgVideo = new VideoSource();
	//fgVideo = new VideoSource();

 //   //bgVideo.OpenFile("Videos/13.mp4", g_Renderer.device, g_Renderer.context);
 //   bgVideo->OpenFile("Videos/toyota_positional_test_v3_max_speed_accel_bg.mp4", renderer->GetDevice(), renderer->GetContext());
 //   fgVideo->OpenFile("Videos/1.mp4", renderer->GetDevice(), renderer->GetContext());
    
    state.sources[0]->looped = true;
    state.sources[0]->Play(GetTimeStd());
    //fgVideo->looped = true;

	ShowWindow(window, SW_SHOW);
	ToggleFullscreen(window);

    state.networkMgr = new NetworkManager("127.0.0.1", 5555, this);
	state.networkMgr->Start();
}

App::~App()
{
    if (state.networkMgr)
    {
        state.networkMgr->Stop();
        delete state.networkMgr;
    }

    for (auto source : state.sources)
        delete source;

    if (renderer)
        delete renderer;

    if (videoShader)
		delete videoShader;
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
			state.sources[1]->Rewind();
			state.sources[1]->Play(GetTimeStd());
        }

		state.sources[0]->GetNextFrame(renderer->GetContext());
        if (fgActive)
        {
            if (!state.sources[1]->GetNextFrame(renderer->GetContext()))
                fgActive = false;
		}

        RECT rc; 
        GetClientRect(window, &rc);
        float w = (float)(rc.right - rc.left);
        float h = (float)(rc.bottom - rc.top);

		renderer->BeginRendering();
		renderer->DrawVideo(state.sources[0], videoShader, 1.0f, false, w, h);

        if (fgActive)
        {
            float fade = 1.0f;
            if (state.sources[1]->internalPTS < state.sources[1]->fadeInDuration)
                fade = (float)state.sources[1]->internalPTS / state.sources[1]->fadeInDuration;
            else if (state.sources[1]->duration - state.sources[1]->internalPTS < state.sources[1]->fadeOutDuration)
                fade = (float)(state.sources[1]->duration - state.sources[1]->internalPTS) / state.sources[1]->fadeOutDuration;

            renderer->DrawVideo(state.sources[1], videoShader, max(0.0f, fade), true, w, h);
        }

        renderer->EndRendering();
	}
}

VideoSource* App::GetBackgroundVideo()
{
    return state.sources.empty() ? nullptr : state.sources[0];
}

std::vector<float> App::GetPositions()
{
    return state.sources[0]->positions;
}

double App::GetLastPTS()
{
	return state.sources[0]->lastPTS;
}

int64_t App::GetBGCaptureTimeNS()
{
	return state.sources[0]->bg_capture_time_ns;
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

    if (msg == WM_KEYDOWN && wp == VK_ESCAPE)
    {
        DestroyWindow(hwnd);
        return 0;
    }
    
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
