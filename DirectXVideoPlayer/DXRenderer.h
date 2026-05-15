#pragma once
#include <windows.h>
#include "IRenderer.h"

struct ID3D11Texture2D;
struct ID3D11ShaderResourceView;
struct ID3D11Device;
struct ID3D11DeviceContext;
struct IDXGISwapChain;
struct ID3D11RenderTargetView;
struct ID3D11SamplerState;
struct ID3D11Buffer;
struct ID3D11BlendState;
class VideoSource;
class DXShader; 

//struct Vertex { float x, y, z; float u, v; };

class DXRenderer : public IRenderer
{
private:
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    IDXGISwapChain* swapChain = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    ID3D11SamplerState* sampler = nullptr;
    ID3D11Buffer* vBuffer = nullptr;
    ID3D11Buffer* alphaCB = nullptr;
    ID3D11BlendState* blendState = nullptr;

public:
	DXRenderer() = default;
	~DXRenderer();

    bool Initialize(HWND hwnd);
    void Resize(int width, int height);
    void BeginFrame();
    void EndFrame();
	void DrawVideo(VideoSource* src, DXShader* shader, float alpha, bool blend, float winW, float winH);
    IDXGISwapChain* GetSwapChain();
    ID3D11DeviceContext* GetContext();
    ID3D11Device* GetDevice();
};

