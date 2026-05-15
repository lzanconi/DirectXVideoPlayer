#pragma once
#include <windows.h>
#include "customtypes.h"
#include "IApp.h"

class IRenderer;
class VideoSource;
class DXShader;
struct AVBufferRef;
struct AVPacket;
struct AVFrame;

class App : public IApp
{
public:
	static AppState state;

private:
	bool isFullscreen = false;
	bool spaceBarPressed = false;
	bool fgActive = false;
	IRenderer* renderer = nullptr;
	DXShader* videoShader = nullptr;
	VideoSource* bgVideo = nullptr; 
	VideoSource* fgVideo = nullptr;
	AVBufferRef* hw_ctx;
	AVPacket* raw_packet;
	AVFrame* frame;
	WNDCLASS wndClass = { 0 };
	MSG msg = { 0 };
	HWND window = nullptr;
	// Stores window position before going fullscreen
	RECT windowRect = { 0 };
	WINDOWPLACEMENT windowPlacement = { sizeof(WINDOWPLACEMENT) };

public:
	App(int width, int height);
	~App();

	void Run();

	VideoSource* GetBackgroundVideo() override;
	std::vector<float> GetPositions() override;
	double GetLastPTS() override;
	int64_t GetBGCaptureTimeNS() override;

private:
	static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
	LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
	void ToggleFullscreen(HWND hwnd);
};

