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
    //Load all videos from a specified folder
    contentMgr = new ContentManager(this);
    contentMgr->LoadContents(".\\Videos");
    if (contentMgr->GetVideoContents().empty())
    {
        MessageBoxA(nullptr, "No .mp4 files found in the Videos folder.", "Error", MB_ICONERROR);
    }

	//Create a window to contain the DirectX rendering output
    wndClass.lpfnWndProc = WndProc;
    wndClass.lpszClassName = L"VP";
    wndClass.hInstance = GetModuleHandle(NULL);
    RegisterClass(&wndClass);
    window = CreateWindow(L"VP", L"OOP Video Player", WS_OVERLAPPEDWINDOW, 100, 100, width, height, 0, 0, wndClass.hInstance, this);

	//Initialize DirectX renderer and link it to the created window
    DXRenderer* dxRenderer = new DXRenderer();
    dxRenderer->Initialize(window);
    renderer = dxRenderer;
    state.renderer = renderer;

	//Load the HLSL shader that will be used to render the video textures with alpha blending
    videoShader = new DXShader();
    videoShader->LoadFromFile(renderer->GetDevice(), L"shaders.hlsl");

	//For each video loaded by the ContentManager, create a corresponding VideoSource instance 
	LoadVideoSources(renderer->GetDevice(), renderer->GetContext());    
    
	//Immediately start playing the first video as the background layer, looping indefinitely
	StartBackgroundVideo(0, true);

    ShowWindow(window, SW_SHOW);
    ToggleFullscreen(window);

    //Start NetworkManager to send positions
	StartNetworkManager("127.0.0.1", 5555);
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

/*
This function handles the window messaging system, process logic state evaluations, frame updates, and rendering pipelines
*/
void App::Run()
{
	//Keep running as long as you don't receive WM_QUIT message that means the application is closing
    while (msg.message != WM_QUIT)
    {
        //Checks if there are any pending window messages like keyboard strokes, mouse movements etc.
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
			//Forwards the retrieved messages to the WndProc message handler
            DispatchMessage(&msg);

            //Forces the program to skip rendering/processing when a new message is received, 
            //prioritizing input responsiveness over drawing.
            continue;
        }

		//NORMAL FOREGROUND TRIGGER LOGIC
		//SPACEBAR key press message received (NORMAL FOREGROUND TRIGGER)
        if (spaceBarPressed)
        {
			//Reset the spaceBarPressed flag immediately to prevent multiple triggers from a single key press
            spaceBarPressed = false;

            //Trigger the playback of the next foreground video (for now it's always the second video video in the list at index 1)
			TriggerForegroundVideo(1);
        }

        if (sKeyPressed)
        {
            sKeyPressed = false;

            //Check if a sequence was loaded
            if (!state.sequence.empty())
            {
				//Enable sequence mode which will override normal foreground triggering and force the videos to play in the order defined by the sequence configuration
                state.isSequenceActive = true;
                //Resets the sequence pointer back to the first item
                state.currentSequenceIdx = 0;

                //Load and play the first video in the sequence
                AdvanceSequence();
            }
        }

        ComputeVideoFrames();

        RECT rc;
        GetClientRect(window, &rc);
        float w = (float)(rc.right - rc.left);
        float h = (float)(rc.bottom - rc.top);


        //Commands the DirectX 11 backend to clear the screen to black, re-bind the geometric vertex buffers, 
        //and prime the pipeline to receive new draw commands.
        renderer->BeginRendering();

        //Draw the videos
        DrawVideos(w, h);

        //Swap the back buffer to the screen
        renderer->EndRendering();
    }
}

void App::LoadVideoSources(ID3D11Device* device, ID3D11DeviceContext* context)
{
    for (const auto& videoContent : contentMgr->GetVideoContents())
    {
        VideoSource* videoSource = new VideoSource();
        if (videoSource->OpenFile(videoContent.filename, device, context))
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

}

void App::StartBackgroundVideo(int sourceIndex, bool shouldLoop)
{
    state.sources[sourceIndex]->looped = shouldLoop;
    state.sources[sourceIndex]->Play(GetTimeStd());
}

/*
Handles the triggering of a foreground video playback
*/
void App::RequestForegroundVideo(int index)
{
    //CASE 1: No foreground video is playing, Fade in normally
    if (state.fgState == ForegroundState::Idle)
    {
        //Set which foreground video we want to play
        state.currentForegroundIdx = index;
        //Advances the state machine's operational status from Idle to FadingIn
        state.fgState = ForegroundState::FadingIn;
        //Ensures that any manual interruptions from previous video changes are completely cleared out for this fresh playback session
        state.isForcedFadingOut = false;
        state.sources[index]->isActive = true;
        state.sources[index]->fadeInComplete = false;
        state.sources[index]->alpha = 0.0f;
        
        //Rewind the foreground video
        state.sources[index]->Rewind();
        //Start to play the foreground video
        state.sources[index]->Play(GetTimeStd());
        
		//IMPORTANT: We need to decode the first frame of the video immediately to ensure that the shader has valid texture data
        //Decode first frame immediately to avoid green flash on instant cuts
        state.sources[index]->GetNextFrame(renderer->GetContext());
        
        // If video fadeInDuration properties is 0, start at full opacity for instant cut
        if (state.sources[index]->fadeInDuration <= 0.0f)
        {
            state.sources[index]->alpha = 1.0f;
            //Bypass the fade-in process
            state.sources[index]->fadeInComplete = true;
			//Directly transition to Playing state
            state.fgState = ForegroundState::Playing;
        }
    }
    //CASE 2: If a foreground video is already actively on screen or currently in the middle of fading in, and it hasn't already 
    //been commanded to exit (!state.isForcedFadingOut), the engine processes a mid-playback interruption transition
    else if ((state.fgState == ForegroundState::Playing || state.fgState == ForegroundState::FadingIn) && !state.isForcedFadingOut)
    {
		//Stores the requested new video's index position, queuing it to load immediately after the current video finishes its fade-out sequence
        state.pendingForegroundIdx = index;
		//Advances the state machine into the FadingOut
        state.fgState = ForegroundState::FadingOut;

		//Get a pointer to the currently active foreground video that we want to fade out
        VideoSource* activeVideo = state.sources[state.currentForegroundIdx];

        // Instead of mutating activeVideo->duration directly which breaks later triggers,
        // we capture the specific timestamp frame where the fade-out interruption was requested.

        //Tells to use custom interpolation for manually fading out un uncompleted video 
        state.isForcedFadingOut = true;
        //Captures the precise playback timestamp position where the interruption occurred
        //It will be used as the baseline to calculate the custom fade out speed until opacity hits zero
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

		std::cout << "Advancing to sequence item: " << seqItem.filename << std::endl;
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

void App::StartNetworkManager(const std::string& serverIP, int serverPort)
{
    state.networkMgr = new NetworkManager(serverIP, serverPort, this);
    state.networkMgr->Start();
}

void App::TriggerForegroundVideo(int targetFgIndex)
{
    //Ensures that the target index is within bounds of the loaded video sources
    if (state.sources.size() > targetFgIndex)
    {
        //Interrupts any active sequence to allow for manual foreground triggering without waiting for sequence completion
        state.isSequenceActive = false;
        //Clears out any sequence video or a foreground video that is waiting that the previous one has completed its fade out
        state.pendingForegroundIdx = -1;
        //Restores the target foreground video configuration to its defaults.
        // 
        //This removes any customized values that might have been applied to it while running in a sequence
        state.sources[targetFgIndex]->isSequenceLoop = false;
        state.sources[targetFgIndex]->looped = contentMgr->GetVideoContents().at(targetFgIndex).looped;
        state.sources[targetFgIndex]->fadeInDuration = contentMgr->GetVideoContents().at(targetFgIndex).fadeInDuration;
        state.sources[targetFgIndex]->fadeOutDuration = contentMgr->GetVideoContents().at(targetFgIndex).fadeOutDuration;

        //Launch the playback of the foreground video 
        RequestForegroundVideo(targetFgIndex);
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

AppState& App::GetAppState()
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

            // Use the video's fadeOutDuration, or a minimum of 1.0 second if it's 0
            float effectiveFadeOutDuration = (fgVideo->fadeOutDuration > 0.0f) ? fgVideo->fadeOutDuration : 1.0f;

            float forcedFactor = 1.0f - static_cast<float>(elapsedFadeTime / effectiveFadeOutDuration);
            currentAlpha = (std::min)(currentAlpha, forcedFactor);
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
            // Check for instant cut: video ended with zero fade-out duration
            if (!frameDecoded && !fgVideo->isSequenceLoop && fgVideo->fadeOutDuration <= 0.0f)
            {
                // Video ended, immediately transition to next in sequence
                if (state.isSequenceActive && state.pendingForegroundIdx == -1)
                {
                    int oldIdx = state.currentForegroundIdx;
                    
                    // Reset state BEFORE calling AdvanceSequence so RequestForegroundVideo goes to Case 1
                    state.currentForegroundIdx = -1;
                    state.fgState = ForegroundState::Idle;
                    
                    state.currentSequenceIdx++;
                    AdvanceSequence();
                    
                    // Clean up old video after new one starts
                    if (state.currentForegroundIdx != -1 && oldIdx != -1)
                    {
                        state.sources[oldIdx]->isActive = false;
                        state.sources[oldIdx]->alpha = 0.0f;
                    }
                    else if (state.currentForegroundIdx == -1)
                    {
                        // Sequence ended, clean up the old video
                        if (oldIdx != -1)
                        {
                            state.sources[oldIdx]->isActive = false;
                            state.sources[oldIdx]->alpha = 0.0f;
                        }
                    }
                }
                else
                {
                    // Not in sequence, normal cleanup
                    fgVideo->isActive = false;
                    fgVideo->alpha = 0.0f;
                    state.currentForegroundIdx = -1;
                    state.fgState = ForegroundState::Idle;
                }
                break;
            }
            
            // Only enter fade out if it's not a sequence loop and we're in fade-out window
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
                // --- SEQUENCING PIPELINE HOOK ---
                if (state.isSequenceActive && state.pendingForegroundIdx == -1)
                {
                    if (!fgVideo->isSequenceLoop)
                    {
                        // Store the old video index before advancing
                        int oldIdx = state.currentForegroundIdx;
                        
                        // Reset state BEFORE calling AdvanceSequence so RequestForegroundVideo goes to Case 1
                        state.currentForegroundIdx = -1;
                        state.fgState = ForegroundState::Idle;
                        state.isForcedFadingOut = false;
                        
                        state.currentSequenceIdx++;
                        AdvanceSequence();
                        
                        // Deactivate old video after new one is started
                        if (state.currentForegroundIdx != -1 && oldIdx != -1)
                        {
                            state.sources[oldIdx]->isActive = false;
                            state.sources[oldIdx]->alpha = 0.0f;
                        }
                        else if (state.currentForegroundIdx == -1)
                        {
                            // Sequence ended, clean up the old video
                            if (oldIdx != -1)
                            {
                                state.sources[oldIdx]->isActive = false;
                                state.sources[oldIdx]->alpha = 0.0f;
                            }
                        }
                    }
                }
                else if (state.pendingForegroundIdx != -1)
                {
                    int oldIdx = state.currentForegroundIdx;
                    int nextIdx = state.pendingForegroundIdx;
                    state.pendingForegroundIdx = -1;

                    state.currentForegroundIdx = nextIdx;
                    state.fgState = ForegroundState::FadingIn;
                    state.sources[nextIdx]->isActive = true;
                    state.sources[nextIdx]->fadeInComplete = false;
                    state.sources[nextIdx]->alpha = 0.0f;
                    
                    state.sources[nextIdx]->Rewind();
                    state.sources[nextIdx]->Play(GetTimeStd());
                    
                    // Decode first frame immediately to avoid green flash on instant cuts
                    state.sources[nextIdx]->GetNextFrame(renderer->GetContext());
                    
                    // If fadeInDuration is 0, start at full opacity for instant cut
                    if (state.sources[nextIdx]->fadeInDuration <= 0.0f)
                    {
                        state.sources[nextIdx]->alpha = 1.0f;
                        state.sources[nextIdx]->fadeInComplete = true;
                        state.fgState = ForegroundState::Playing;
                    }
                    
                    // Clean up old video
                    if (oldIdx != -1 && oldIdx != nextIdx)
                    {
                        state.sources[oldIdx]->isActive = false;
                        state.sources[oldIdx]->alpha = 0.0f;
                    }
                    
                    state.isForcedFadingOut = false;
                }
                else
                {
                    // No sequence or pending video, clean up normally
                    fgVideo->isActive = false;
                    fgVideo->alpha = 0.0f;
                    state.currentForegroundIdx = -1;
                    state.fgState = ForegroundState::Idle;
                    state.isForcedFadingOut = false;
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

    // Render all active foreground videos (supports overlap during transitions)
    for (size_t i = 0; i < state.sources.size(); ++i)
    {
        if (state.sources[i]->isActive && i != 0) // Skip background (index 0)
        {
            float currentAlpha = state.sources[i]->alpha;
            if (currentAlpha > 0.0f)
            {
                renderer->DrawVideo(state.sources[i], videoShader, currentAlpha, true, width, height);
            }
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