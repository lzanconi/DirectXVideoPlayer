#pragma once
#include <string>
#include <vector>

class VideoSource;
class IRenderer;
class NetworkManager;

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

    // FPS Tracking
    double lastFPSUpdate = 0;
    int frameCount = 0;
};

struct VideoContent
{
    //Path to the .mp4 file
    std::string filename;
    //Fade in duration in seconds (default 2.5s)
    float fadeInDuration = 2.5f;
    //Fade out duration in seconds (default 1s)
    float fadeOutDuration = 1.0f;
    //Whether the video should loop (default false)
    bool looped = false;
    //Optional position data loaded from a corresponding .csv file
    std::vector<float> positions;
};