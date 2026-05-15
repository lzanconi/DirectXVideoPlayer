#include "DXRenderer.h"
#include <d3d11.h>
#include <dxgi1_2.h>
#include "VideoSource.h"
#include "DXShader.h"

#ifndef __ID3D11Multithread_INTERFACE_DEFINED__
#define __ID3D11Multithread_INTERFACE_DEFINED__
MIDL_INTERFACE("9b7e4e00-342c-4106-a19f-4f2704f689f0")
ID3D11Multithread : public IUnknown
{
public:
    virtual void STDMETHODCALLTYPE Enter(void) = 0;
    virtual void STDMETHODCALLTYPE Leave(void) = 0;
    virtual BOOL STDMETHODCALLTYPE SetMultithreadProtected(BOOL bMTProtect) = 0;
    virtual BOOL STDMETHODCALLTYPE GetMultithreadProtected(void) = 0;
};
#endif

struct Vertex { float x, y, z; float u, v; };

DXRenderer::~DXRenderer()
{
    if (vBuffer) 
        vBuffer->Release(); 
    if (alphaCB) 
        alphaCB->Release();
    if (sampler) 
        sampler->Release(); 
    if (rtv) 
        rtv->Release();
    if (blendState) 
        blendState->Release();
    if (swapChain) 
        swapChain->Release();
    if (context) 
        context->Release(); 
    if (device) 
        device->Release();
}

bool DXRenderer::Initialize(HWND hwnd)
{
    DXGI_SWAP_CHAIN_DESC sd = { {0, 0, {60, 1}, DXGI_FORMAT_R8G8B8A8_UNORM}, {1, 0}, DXGI_USAGE_RENDER_TARGET_OUTPUT, 2, hwnd, TRUE, DXGI_SWAP_EFFECT_FLIP_DISCARD };
    D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_VIDEO_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &sd, &swapChain, &device, nullptr, &context);

    // Multithreading is vital for FFmpeg background decoding
    ID3D11Multithread* mt = nullptr;
    device->QueryInterface(__uuidof(ID3D11Multithread), (void**)&mt);
    if (mt) { mt->SetMultithreadProtected(TRUE); mt->Release(); }

    Resize(0, 0); // Init RTV

    Vertex vertices[] = { {-1,1,0,0,0}, {1,1,0,1,0}, {-1,-1,0,0,1}, {1,1,0,1,0}, {1,-1,0,1,1}, {-1,-1,0,0,1} };
    D3D11_BUFFER_DESC bd = { sizeof(vertices), D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER };
    D3D11_SUBRESOURCE_DATA id = { vertices };
    device->CreateBuffer(&bd, &id, &vBuffer);

    D3D11_BUFFER_DESC cbd = { 16, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE };
    device->CreateBuffer(&cbd, nullptr, &alphaCB);

    D3D11_SAMPLER_DESC spd = { D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_CLAMP, D3D11_TEXTURE_ADDRESS_CLAMP, D3D11_TEXTURE_ADDRESS_CLAMP };
    device->CreateSamplerState(&spd, &sampler);

    D3D11_BLEND_DESC bld = {};
    bld.RenderTarget[0].BlendEnable = TRUE;
    bld.RenderTarget[0].SrcBlend = bld.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_SRC_ALPHA;
    bld.RenderTarget[0].DestBlend = bld.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    bld.RenderTarget[0].BlendOp = bld.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bld.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    device->CreateBlendState(&bld, &blendState);

    return true;
}

void DXRenderer::Resize(int width, int height)
{
    if (rtv) 
        rtv->Release();
    swapChain->ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN, 0);
    ID3D11Texture2D* bb;
    swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb);
    device->CreateRenderTargetView(bb, nullptr, &rtv);
    bb->Release();
}

void DXRenderer::BeginFrame()
{
    float clearColor[4] = { 0, 0, 0, 1 };
    context->ClearRenderTargetView(rtv, clearColor);
    UINT stride = sizeof(Vertex), offset = 0;
    context->IASetVertexBuffers(0, 1, &vBuffer, &stride, &offset);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->PSSetSamplers(0, 1, &sampler);
    context->OMSetRenderTargets(1, &rtv, nullptr);
}

void DXRenderer::EndFrame()
{
    swapChain->Present(1, 0);
}

void DXRenderer::DrawVideo(VideoSource& src, DXShader& shader, float alpha, bool blend, float winW, float winH)
{
    // Setup aspect ratio viewport
    float ar = (float)src.width / (float)src.height;
    float wAR = winW / winH;
    float vpW = (wAR > ar) ? winH * ar : winW;
    float vpH = (wAR > ar) ? winH : winW / ar;
    D3D11_VIEWPORT vp = { (winW - vpW) * 0.5f, (winH - vpH) * 0.5f, vpW, vpH, 0.0f, 1.0f };
    context->RSSetViewports(1, &vp);

    // Update Alpha
    D3D11_MAPPED_SUBRESOURCE ms;
    context->Map(alphaCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms);
    ((float*)ms.pData)[0] = alpha;
    context->Unmap(alphaCB, 0);

    context->IASetInputLayout(shader.layout);
    context->VSSetShader(shader.vs, nullptr, 0);
    context->PSSetShader(shader.ps, nullptr, 0);
    context->PSSetConstantBuffers(0, 1, &alphaCB);
    context->PSSetShaderResources(0, 1, &src.srvY);
    context->PSSetShaderResources(1, 1, &src.srvUV);
    context->OMSetBlendState(blend ? blendState : nullptr, nullptr, 0xFFFFFFFF);
    context->Draw(6, 0);
}