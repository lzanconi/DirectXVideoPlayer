#include "App.h"
#include "IRenderer.h"
#include "DXRenderer.h"
#include "DXShader.h"
#include "VideoSource.h"
#include "utils.h"
#include "NetworkManager.h"
#include "ContentManager.h"
#include <iostream>
#include <algorithm>

AppState App::state;

App::App(int width, int height)
{
    contentMgr = new ContentManager(this);
    contentMgr->LoadVideoContentFromFolder(".\\Videos");
    if (contentMgr->GetVideoContents().empty())
    {
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

    for (const auto& videoContent : contentMgr->GetVideoContents())
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

    for (const auto& source : state.sources)
    {
        std::cout << "VideoSource: " << source->file_name << " Duration: " << GetDurationMinSec(static_cast<int>(source->duration)) << std::endl;
    }

    state.sources[0]->looped = true;
    state.sources[0]->Play(GetTimeStd());

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

    if (contentMgr)
		delete contentMgr;
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

            int targetFgIndex = 1;

            if (state.sources.size() > targetFgIndex)
            {
                RequestForegroundVideo(targetFgIndex);
            }
        }

        ComputeVideoFrames();

        RECT rc;
        GetClientRect(window, &rc);
        float w = (float)(rc.right - rc.left);
        float h = (float)(rc.bottom - rc.top);

        renderer->BeginRendering();
        DrawVideos(w, h);
        renderer->EndRendering();
    }
}

void App::RequestForegroundVideo(int index)
{
    // Case 1: Video engine is completely idling. Fade in normally.
    if (state.fgState == ForegroundState::Idle)
    {
        state.currentForegroundIdx = index;
        state.fgState = ForegroundState::FadingIn;
        state.isForcedFadingOut = false;
        state.sources[index]->isActive = true;
        state.sources[index]->Rewind();
        state.sources[index]->Play(GetTimeStd());
    }
    // Case 2: Already active or fading in. Force a localized transition to fade out.
    else if ((state.fgState == ForegroundState::Playing || state.fgState == ForegroundState::FadingIn) && !state.isForcedFadingOut)
    {
        state.pendingForegroundIdx = index;
        state.fgState = ForegroundState::FadingOut;

        VideoSource* activeVideo = state.sources[state.currentForegroundIdx];

        // Instead of mutating activeVideo->duration directly which breaks later triggers,
        // we capture the specific timestamp frame where the fade-out interruption was requested.
        state.isForcedFadingOut = true;
        state.forcedFadeOutStartTime = activeVideo->internalPTS;
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

void App::ComputeVideoFrames()
{
    // Compute frame for the background video
    state.sources[0]->GetNextFrame(renderer->GetContext());

    //Checks if a foreground video is currently active. 
    //If state.currentForegroundIdx is -1, it means no foreground video is selected and skips the rest of the logic
    if (state.currentForegroundIdx != -1)
    {
        //Grab a pointer to the current foreground video
        int idx = state.currentForegroundIdx;
        VideoSource* fgVideo = state.sources[idx];

		//Attempt to decode the next frame for the active foreground video. 
		//It returns true if a new frame was successfully decoded, or false if the video has reached its natural end
        bool frameDecoded = fgVideo->GetNextFrame(renderer->GetContext());

		//Compute the alpha value for the current frame based on its own internal timing and fade parameters
        float currentAlpha = fgVideo->ComputeAlpha();

		//FORCED FADE OUT LOGIC:
        //Checks if this fade-out phase was forced early by a user pressing the spacebar mid-playback
        if (state.isForcedFadingOut && state.fgState == ForegroundState::FadingOut)
        {
            //Calculates how many seconds have passed since the user interrupted the video. 
            //It subtracts the playhead time when the button was pressed from the current playhead time.
            double elapsedFadeTime = fgVideo->internalPTS - state.forcedFadeOutStartTime;

            //Check if the video has to fade out 
            if (fgVideo->fadeOutDuration > 0.0f)
            {
                // Linearly interpolate downward to absolute transparency over the fade out window length
                float forcedFactor = 1.0f - static_cast<float>(elapsedFadeTime / fgVideo->fadeOutDuration);
                currentAlpha = (std::min)(currentAlpha, forcedFactor);
            }
            else
            {
                currentAlpha = 0.0f;
            }
        }

        //Updates the actual variable used by the DirectX renderer to fade out the video texture
        fgVideo->alpha = currentAlpha;

        // 3. State Engine evaluation block
        switch (state.fgState)
        {
        case ForegroundState::FadingIn:
        {
            if (fgVideo->internalPTS >= fgVideo->fadeInDuration)
            {
                state.fgState = ForegroundState::Playing;
            }
            break;
        }
        case ForegroundState::Playing:
        {
            // Native natural file ending boundary check
            if (fgVideo->duration - fgVideo->internalPTS <= fgVideo->fadeOutDuration)
            {
                state.fgState = ForegroundState::FadingOut;
            }
            break;
        }
        case ForegroundState::FadingOut:
        {
            // Terminates cleanly when alpha hitting zero or the actual asset runs completely out of frames
            if (fgVideo->alpha <= 0.001f || !frameDecoded)
            {
                fgVideo->isActive = false;
                fgVideo->alpha = 0.0f;

                state.currentForegroundIdx = -1;
                state.fgState = ForegroundState::Idle;
                state.isForcedFadingOut = false;

                if (state.pendingForegroundIdx != -1)
                {
                    int nextIdx = state.pendingForegroundIdx;
                    state.pendingForegroundIdx = -1;

                    state.currentForegroundIdx = nextIdx;
                    state.fgState = ForegroundState::FadingIn;
                    state.sources[nextIdx]->isActive = true;
                    state.sources[nextIdx]->Rewind();
                    state.sources[nextIdx]->Play(GetTimeStd());
                }
            }
            break;
        }
        default:
            break;
        }

        if (!frameDecoded && state.fgState != ForegroundState::FadingOut)
        {
            fgVideo->isActive = false;
            state.currentForegroundIdx = -1;
            state.fgState = ForegroundState::Idle;
            state.isForcedFadingOut = false;
        }
    }
}

void App::DrawVideos(float width, float height)
{
    // Draw background
    renderer->DrawVideo(state.sources[0], videoShader, 1.0f, false, width, height);

    if (state.currentForegroundIdx != -1)
    {
        int idx = state.currentForegroundIdx;
        VideoSource* fgVideo = state.sources[idx];

        if (fgVideo->isActive)
        {
            float currentAlpha = fgVideo->alpha;
            renderer->DrawVideo(fgVideo, videoShader, (std::max)(0.0f, currentAlpha), true, width, height);
        }
    }
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
        LONG style = GetWindowLong(hwnd, GWL_STYLE);
        SetWindowLong(hwnd, GWL_STYLE, style | WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(hwnd, &windowPlacement);
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);

        while (ShowCursor(TRUE) < 0);
    }
}