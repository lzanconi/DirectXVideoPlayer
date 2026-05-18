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
    contentMgr->LoadContents(".\\Videos");
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
				state.isSequenceActive = false; // Interrupt any active sequence when spacebar is pressed
				state.sources[targetFgIndex]->isSequenceLoop = false;
                RequestForegroundVideo(targetFgIndex);
            }
        }

        if (sKeyPressed)
        {
            sKeyPressed = false;

            if (!state.sequence.empty())
            {
                state.isSequenceActive = true;
                state.currentSequenceIdx = 0;
                AdvanceSequence();
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
        state.sources[index]->alpha = 0.0f;
        state.sources[index]->fadeInComplete = false;
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

void App::AdvanceSequence()
{
    if (!state.isSequenceActive || state.currentSequenceIdx < 0 || state.currentSequenceIdx >= state.sequence.size())
    {
        state.isSequenceActive = false;
        state.currentSequenceIdx = -1;
        return;
    }

    const auto& seqItem = state.sequence[state.currentSequenceIdx];

    // Search for a matching VideoSource index by checking file names
    int matchIdx = -1;
    for (size_t i = 0; i < state.sources.size(); ++i)
    {
        if (state.sources[i]->file_name == seqItem.filename)
        {
            matchIdx = static_cast<int>(i);
            break;
        }
    }

    if (matchIdx != -1)
    {
        // Override the video source rules using sequence rules configuration definitions
        state.sources[matchIdx]->fadeInDuration = seqItem.fadeInDuration;
        state.sources[matchIdx]->fadeOutDuration = seqItem.fadeOutDuration;
        state.sources[matchIdx]->looped = seqItem.looped;
		state.sources[matchIdx]->isSequenceLoop = seqItem.looped;

        // Force launch video through standard player pipeline
        RequestForegroundVideo(matchIdx);
    }
    else
    {
        // If a file from the sequence is missing in the video folder, jump to next sequence step
        state.currentSequenceIdx++;
        AdvanceSequence();
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

AppState &App::GetAppState()
{
    return state;
}

void App::ComputeVideoFrames()
{
    // Compute frame for the background video
    state.sources[0]->GetNextFrame(renderer->GetContext());

    if (state.currentForegroundIdx != -1)
    {
        int idx = state.currentForegroundIdx;
        VideoSource* fgVideo = state.sources[idx];

        // Attempt to decode the next frame
        bool frameDecoded = fgVideo->GetNextFrame(renderer->GetContext());

        // Compute alpha value
        float currentAlpha = fgVideo->ComputeAlpha();

        // FORCED FADE OUT LOGIC (Spacebar overrides)
        if (state.isForcedFadingOut && state.fgState == ForegroundState::FadingOut)
        {
            double elapsedFadeTime = fgVideo->internalPTS - state.forcedFadeOutStartTime;
            if (fgVideo->fadeOutDuration > 0.0f)
            {
                float forcedFactor = 1.0f - static_cast<float>(elapsedFadeTime / fgVideo->fadeOutDuration);
                currentAlpha = (std::min)(currentAlpha, forcedFactor);
            }
            else
            {
                currentAlpha = 0.0f;
            }
        }

        fgVideo->alpha = currentAlpha;

        // State Engine evaluation block
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
            // Only enter fade out if it's not a sequence loop
            if (!fgVideo->isSequenceLoop && (fgVideo->duration - fgVideo->internalPTS <= fgVideo->fadeOutDuration))
            {
                state.fgState = ForegroundState::FadingOut;
            }
            break;
        }
        case ForegroundState::FadingOut:
        {
            // CRITICAL FIX: If alpha hits zero OR the asset runs out of frames early, cleanly terminate!
            if (fgVideo->alpha <= 0.001f || !frameDecoded)
            {
                fgVideo->isActive = false;
                fgVideo->alpha = 0.0f;

                state.currentForegroundIdx = -1;
                state.fgState = ForegroundState::Idle;
                state.isForcedFadingOut = false;

                // --- SEQUENCING PIPELINE HOOK ---
                if (state.isSequenceActive && state.pendingForegroundIdx == -1)
                {
                    if (!fgVideo->isSequenceLoop)
                    {
                        state.currentSequenceIdx++;
                        AdvanceSequence();
                    }
                }
                else if (state.pendingForegroundIdx != -1)
                {
                    state.isSequenceActive = false;

                    int nextIdx = state.pendingForegroundIdx;
                    state.pendingForegroundIdx = -1;

                    state.currentForegroundIdx = nextIdx;
                    state.fgState = ForegroundState::FadingIn;
                    state.sources[nextIdx]->isActive = true;
                    state.sources[nextIdx]->alpha = 0.0f;
                    state.sources[nextIdx]->fadeInComplete = false;
                    state.sources[nextIdx]->Rewind();
                    state.sources[nextIdx]->Play(GetTimeStd());
                }
            }
            break;
        }
        default:
            break;
        }

        // Catch-all safety: If it dies unexpectedly outside of FadingOut.
        // Guard with idx check so this doesn't re-fire if FadingOut already cleaned up and started the next video.
        if (!frameDecoded && state.fgState != ForegroundState::FadingOut && state.currentForegroundIdx == idx)
        {
            fgVideo->isActive = false;
            fgVideo->alpha = 0.0f;
            state.currentForegroundIdx = -1;
            state.fgState = ForegroundState::Idle;
            state.isForcedFadingOut = false;

            if (state.isSequenceActive)
            {
                state.currentSequenceIdx++;
                AdvanceSequence();
            }
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

    if (msg == WM_KEYDOWN && wp == 'S')
        sKeyPressed = true;

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