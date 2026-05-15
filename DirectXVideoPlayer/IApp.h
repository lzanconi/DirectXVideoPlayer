#pragma once
#include <vector>

class VideoSource;

class IApp 
{
public:
	virtual ~IApp() = default;

    virtual VideoSource* GetBackgroundVideo() = 0;
    virtual std::vector<float> GetPositions() = 0;
    virtual double GetLastPTS() = 0;
    virtual int64_t GetBGCaptureTimeNS() = 0;
};
