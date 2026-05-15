#pragma once
#include <windows.h>

class VideoSource;
class DXShader;
struct IDXGISwapChain;
struct ID3D11DeviceContext;
struct ID3D11Device;

class IRenderer
{

public:
	virtual ~IRenderer() = default;
	virtual bool Initialize(HWND hwnd) = 0;
	virtual void Resize(int width, int height) = 0;
	virtual void BeginFrame() = 0;
	virtual void EndFrame() = 0;
	virtual void DrawVideo(VideoSource* src, DXShader* shader, float alpha, bool blend, float winW, float winH) = 0;
	virtual IDXGISwapChain *GetSwapChain() = 0;
	virtual ID3D11DeviceContext* GetContext() = 0;
	virtual ID3D11Device* GetDevice() = 0;

};

