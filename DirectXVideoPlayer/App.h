#pragma once
#include <windows.h>
#include "customtypes.h"
#include "IApp.h"

class IRenderer;
class VideoSource;
class ContentManager;
class DXShader;
class NetworkManager;
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
	bool sKeyPressed = false;
	IRenderer* renderer = nullptr;
	DXShader* videoShader = nullptr;
	ContentManager* contentMgr = nullptr;
	AVBufferRef* hw_ctx;
	AVPacket* raw_packet;
	AVFrame* frame;
	WNDCLASS wndClass = { 0 };
	MSG msg = { 0 };
	HWND window = nullptr;
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
	AppState &GetAppState() override;

	void ComputeVideoFrames();
	void DrawVideos(float width, float height);

private:
	void RequestForegroundVideo(int index);
	void AdvanceSequence();

	static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
	LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
	void ToggleFullscreen(HWND hwnd);
};