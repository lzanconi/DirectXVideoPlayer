#pragma once
#include <string>
#include <vector>

class VideoSource;
class IRenderer;
class NetworkManager;

// --- State Machine States for Foreground Playback ---
enum class ForegroundState
{
    Idle,
    FadingIn,
    Playing,
    FadingOut
};

struct AppState
{
    int activeIndex = 0;
    int previousIndex = -1;
    int lastForegroundIndex = 0;
    bool interruptRead = false;
    std::vector<VideoSource*> sources;
    IRenderer* renderer = nullptr;
    NetworkManager* networkMgr = nullptr;
    double lastBackgroundPTS = -1.0;
    bool isRotated = false;

    // --- State Machine Tracking Variables ---
    ForegroundState fgState = ForegroundState::Idle;
    int currentForegroundIdx = -1; // -1 means no foreground video is active
    int pendingForegroundIdx = -1; // Index of the video waiting to play next

    // --- Added for Safe Mid-playback Interruptions without changing native metadata ---
    bool isForcedFadingOut = false;
    double forcedFadeOutStartTime = 0.0;

    // FPS Tracking
    double lastFPSUpdate = 0;
    int frameCount = 0;
};

struct VideoContent
{
    //Path to the .mp4 file
    std::string filename;
    //Fade in duration in seconds (default 2.0s)
    float fadeInDuration = 2.0f;
    //Fade out duration in seconds (default 2.0s)
    float fadeOutDuration = 2.0f;
    //Whether the video should loop (default false)
    bool looped = false;
    //Optional position data loaded from a corresponding .csv file
    std::vector<float> positions;
};