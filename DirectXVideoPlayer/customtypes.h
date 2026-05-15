#pragma once
#include <string>
#include <vector>

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