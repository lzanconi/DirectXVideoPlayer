#pragma once
#include <vector>

class VideoSource;
struct AppState;

class IApp 
{
public:
	virtual ~IApp() = default;

    virtual VideoSource* GetBackgroundVideo() = 0;
    virtual std::vector<float> GetPositions() = 0;
    virtual double GetLastPTS() = 0;
    virtual int64_t GetBGCaptureTimeNS() = 0;
	virtual AppState& GetAppState() = 0;
};
