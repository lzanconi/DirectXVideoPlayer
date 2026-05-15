#pragma once
#include <windows.h>

class VideoSource;
class DXShader;

class IRenderer
{
public:
	virtual ~IRenderer() = default;
	virtual bool Initialize(HWND hwnd) = 0;
	virtual void Resize(int width, int height) = 0;
	virtual void BeginFrame() = 0;
	virtual void EndFrame() = 0;
	virtual void DrawVideo(VideoSource& src, DXShader& shader, float alpha, bool blend, float winW, float winH);

};

