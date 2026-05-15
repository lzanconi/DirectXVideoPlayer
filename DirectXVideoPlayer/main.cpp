#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d3dcompiler.h>
#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <wrl/client.h>
#include "utils.h"

// FFmpeg headers
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_d3d11va.h>
}

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "avcodec.lib")
#pragma comment(lib, "avformat.lib")
#pragma comment(lib, "avutil.lib")
#pragma comment(lib, "d3dcompiler.lib")

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

// --- DXShader Class ---
class DXShader 
{
public:
    ID3D11VertexShader* vs = nullptr;
    ID3D11PixelShader* ps = nullptr;
    ID3D11InputLayout* layout = nullptr;

    ~DXShader() {
        if (vs) vs->Release();
        if (ps) ps->Release();
        if (layout) layout->Release();
    }

    bool LoadFromFile(ID3D11Device* device, const std::wstring& filename) 
    {
        if (!CompileVertexShader(device, filename))
            return false;
        if (!CompilePixelShader(device, filename))
            return false;

        return true;
	}

    bool CompileVertexShader(ID3D11Device* device, const std::wstring& filename)
    {
        Microsoft::WRL::ComPtr<ID3DBlob> vsBlob;
        Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;

        HRESULT hr = D3DCompileFromFile(
            filename.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
            "VS", "vs_5_0", 0, 0, &vsBlob, &errorBlob
        );

        if (FAILED(hr))
        {
            OutputCompileErrors(errorBlob.Get(), filename, L"Vertex Shader");
            return false;
        }

        hr = device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &vs);

        if (FAILED(hr))
        {
			std::cerr << "Failed to create vertex shader from file: " << filename.c_str() << std::endl;
            return false;
        }

        D3D11_INPUT_ELEMENT_DESC ied[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 }
        };

        hr = device->CreateInputLayout(ied, 2, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &layout);
        
        if (FAILED(hr))
        {
			std::cerr << "Failed to create input layout from file: " << filename.c_str() << std::endl;
            return false;
        }

        return SUCCEEDED(hr);
    }

    bool CompilePixelShader(ID3D11Device* device, const std::wstring& filename)
    {
        Microsoft::WRL::ComPtr<ID3DBlob> psBlob;
        Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
        HRESULT hr = D3DCompileFromFile(
            filename.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
            "PS", "ps_5_0", 0, 0, &psBlob, &errorBlob
        );

        if (FAILED(hr))
        {
            OutputCompileErrors(errorBlob.Get(), filename, L"Pixel Shader");
            return false;
        }

        hr = device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &ps);

        return SUCCEEDED(hr);
	}

    void OutputCompileErrors(ID3DBlob* errorBlob, const std::wstring& filename, const std::wstring& shaderType)
    {
        if (errorBlob)
        {
            char* compileErrors = (char*)(errorBlob->GetBufferPointer());
			std::wcerr << "Error compiling " << shaderType.c_str() << " shader from file " << filename.c_str() << ":\n" << compileErrors << std::endl;
        }
        else
        {
			std::wcerr << "Could not find or open shader file: " << filename.c_str() << std::endl;
        }
    }
};

// --- VideoSource Class ---
class VideoSource 
{
public:
    AVFormatContext* fmtCtx = nullptr;
    AVCodecContext* decCtx = nullptr;
    ID3D11Texture2D* videoTexture = nullptr;
    ID3D11ShaderResourceView *srvY = nullptr; 
    ID3D11ShaderResourceView *srvUV = nullptr;
    int streamIdx = -1;
    int width = 0; 
    int height = 0;
    double duration = 0.0;
	double startTime = 0.0; 
    double lastPTS = -1.0;
	bool isInitialized = false;
	bool looped = false;
    
    std::atomic<int64_t> bg_capture_time_ns;
public:
    ~VideoSource() 
    {
        if (srvY) 
            srvY->Release();
        if (srvUV) 
            srvUV->Release();
        if (videoTexture) 
            videoTexture->Release();
        if (decCtx) 
            avcodec_free_context(&decCtx);
        if (fmtCtx) 
            avformat_close_input(&fmtCtx);
    }

    /*
    Responsible for initializing the FFmpeg decoder and setting up the DirectX 11 hardware acceleration resources for a specific video file.
    */
    bool OpenFile(const std::string& path, ID3D11Device* device, ID3D11DeviceContext* context) 
    {
        //Opens the video file and reads the header to understand the container format
        if (avformat_open_input(&fmtCtx, path.c_str(), nullptr, nullptr) < 0)
        {
			std::wstring message = L"Failed to open video file: " + stringToWS(path);
			MessageBox(nullptr, message.c_str(), L"Error", MB_ICONERROR);
            return false;
        }

        //Analyzes the file to get detailed information about the streams (video, audio, etc.)
        avformat_find_stream_info(fmtCtx, nullptr);

        //Specifically searches for the primary video stream within the file and returns its index
        streamIdx = av_find_best_stream(fmtCtx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        if (streamIdx < 0) 
            return false;

        //Looks up the appropriate decoder (like H.264, HEVC, VP9 etc.) based on the video's codec ID
        const AVCodec* codec = avcodec_find_decoder(fmtCtx->streams[streamIdx]->codecpar->codec_id);
        if (!codec)
        {
			std::wstring message = L"Unsupported video codec: " + stringToWS(avcodec_get_name(fmtCtx->streams[streamIdx]->codecpar->codec_id));
			MessageBox(nullptr, message.c_str(), L"Error", MB_ICONERROR);
            return false;
        }

        //Creates a codec context which holds the settings and state for the decoding process
        decCtx = avcodec_alloc_context3(codec);

        //Copies the settings from the file (like resolution and framerate) into the decoder context
        avcodec_parameters_to_context(decCtx, fmtCtx->streams[streamIdx]->codecpar);

        //Allocates a reference for a hardware device context specifically for DirectX 11 Video Acceleration (D3D11VA).
        AVBufferRef* hw_ctx = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D11VA);

        //Retrieves the internal D3D11VA-specific context from the generic hardware device context.
        AVD3D11VADeviceContext* d3d = (AVD3D11VADeviceContext*)((AVHWDeviceContext*)hw_ctx->data)->hwctx;
        
        //Links the FFmpeg hardware context to the existing DirectX 11 device and increments its reference 
        //count to prevent it from being deleted.
        d3d->device = device; 
        device->AddRef();

        //Links the FFmpeg hardware context to the existing DirectX 11 context (the immediate context) and increments its reference count.
        d3d->device_context = context; 
        context->AddRef();

        //Initializes the hardware device context after the D3D11 device and context have been assigned.
        av_hwdevice_ctx_init(hw_ctx);

        //Assigns the initialized hardware context to the decoder, enabling GPU-accelerated decoding.
        decCtx->hw_device_ctx = av_buffer_ref(hw_ctx);

        //Sets a callback function that forces the decoder to output frames in the AV_PIX_FMT_D3D11 format for hardware acceleration.
        decCtx->get_format = [](AVCodecContext*, const AVPixelFormat* pix_fmts) { return AV_PIX_FMT_D3D11; };

        //Decrements the reference count of the local hw_ctx handle, as the decoder now holds its own reference.
        av_buffer_unref(&hw_ctx);

        //Initializes the decoder context with the specified codec. Returns false if the decoder cannot be opened.
        if (avcodec_open2(decCtx, codec, nullptr) < 0)
        {
			MessageBox(nullptr, L"Failed to initialize decoder context!", L"Error", MB_ICONERROR);
            return false;
        }

        //Stores the video's width and height in the class members for later use in rendering.
        width = decCtx->width; 
        height = decCtx->height;

        //Calculates the total duration of the video in seconds by converting the stream's duration using its time base.
        duration = (fmtCtx->streams[streamIdx]->duration != AV_NOPTS_VALUE) ?
            fmtCtx->streams[streamIdx]->duration * av_q2d(fmtCtx->streams[streamIdx]->time_base) : 0;

		//Marks the video source as successfully initialized, allowing the main loop to start decoding and rendering frames.
		isInitialized = true;

        //Calls the private CreateResources method to create the necessary DirectX 11 textures and views for rendering the decoded frames
        return CreateResources(device);
    }

    /*
    Manages the decoding loop, converting raw packets from the video stream into displayable DirectX textures while handling timing and looping.
    */
    bool GetNextFrame(ID3D11DeviceContext* context, double& pts) 
    {
        if (!isInitialized) 
			return true; // No frames to decode yet, but not an error

		double currentTime = GetTimeStd();

        if (startTime <= 0)
            return true;

		double playPos = currentTime - startTime;

        AVPacket* raw_packet = av_packet_alloc();
        AVFrame* frame = av_frame_alloc();
        bool frameDecoded = false;

        if (playPos > lastPTS)
        {
            while (!frameDecoded)
            {
                if (av_read_frame(fmtCtx, raw_packet) >= 0)
                {
                    if (raw_packet->stream_index == streamIdx)
                    {
                        if (avcodec_send_packet(decCtx, raw_packet) == 0)
                        {
                            if (avcodec_receive_frame(decCtx, frame) == 0)
                            {
                                pts = frame->best_effort_timestamp * av_q2d(fmtCtx->streams[streamIdx]->time_base);
                                CopyFrameToDX11Texture(context, frame);
                                lastPTS = playPos;
                                frameDecoded = true;
                                bg_capture_time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                    std::chrono::steady_clock::now().time_since_epoch()).count();
                            }
                        }
                    }
                    av_packet_unref(raw_packet);
                }
                else
                {
                    if (looped)
                    {
						Rewind();
						Play(GetTimeStd()); 
						frameDecoded = true; // Allow loop to start immediately 
                    }
                    else
                    {
                        return false;
                    }
                }
            }
        }

        //if (av_read_frame(fmtCtx, raw_packet) >= 0) {
        //    if (raw_packet->stream_index == streamIdx && avcodec_send_packet(decCtx, raw_packet) == 0) {
        //        if (avcodec_receive_frame(decCtx, frame) == 0) {
        //            pts = frame->best_effort_timestamp * av_q2d(fmtCtx->streams[streamIdx]->time_base);
        //            CopyFrameToDX11Texture(context, frame);
        //            frameDecoded = true;
        //        }
        //    }
        //    av_packet_unref(raw_packet);
        //}
        //else {
        //    // End of stream logic: Loop
        //    avcodec_flush_buffers(decCtx);
        //    av_seek_frame(fmtCtx, streamIdx, 0, AVSEEK_FLAG_BACKWARD);
        //}

        av_frame_free(&frame);
        av_packet_free(&raw_packet);

        return frameDecoded;
    }

    void Play(double startTime)
    {
		this->startTime = startTime;
    }

    void Rewind()
    {
        if (!isInitialized)
            return;

        avcodec_flush_buffers(decCtx);
        av_seek_frame(fmtCtx, streamIdx, 0, AVSEEK_FLAG_BACKWARD);
        lastPTS = -1.0;
    }

private:
    /*
    Responsible for allocating the DirectX 11 textures and Shader Resource Views (SRVs) required to display 
    the video frames decoded by FFmpeg.
    */
    bool CreateResources(ID3D11Device* device) 
    {
        /*
        This initializes a descriptor for a 2D texture:
		    -The width and height are set to match the video's dimensions.
		    -DXGI_FORMAT_NV12 is chosen as the format, which is a common YUV format used in video processing.
		    -D3D11_BIND_SHADER_RESOURCE flag indicates that this texture will be used as a shader resource, allowing it to be sampled in the pixel shader.
        */
        D3D11_TEXTURE2D_DESC td = 
        { 
            (UINT)width, 
            (UINT)height, 1, 1, 
            DXGI_FORMAT_NV12, 
            {1,0}, 
            D3D11_USAGE_DEFAULT, 
            D3D11_BIND_SHADER_RESOURCE 
        };

        /*
        This calls the DirectX 11 device to actually allocate the memory for the texture on the GPU based on the descriptor above,
        storing it in videoTexture.
        */
        device->CreateTexture2D(&td, nullptr, &videoTexture);

        /*
        This creates a descriptor for a Shader Resource View(SRV) for the Luminance(Y) channel:
		    -The format is set to DXGI_FORMAT_R8_UNORM, which means each pixel will be represented by an 8-bit unsigned normalized integer (0-255) 
             for the Y brightness channel.
        */
        D3D11_SHADER_RESOURCE_VIEW_DESC yd = 
        { 
            DXGI_FORMAT_R8_UNORM, 
            D3D11_SRV_DIMENSION_TEXTURE2D 
        };

        //This specifies that the video texture has only one mipmap level (the full resolution).
        yd.Texture2D.MipLevels = 1;

        //This creates the SRV for the Y plane, allowing the pixel shader to access the brightness data of the video.
        device->CreateShaderResourceView(videoTexture, &yd, &srvY);

        //This copies the previous SRV descriptor to use as a starting point for the UV plane.
        D3D11_SHADER_RESOURCE_VIEW_DESC uvd = yd;

        /*
        This changes the format to DXGI_FORMAT_R8G8_UNORM (two 8-bit channels). 
        This allows the shader to read the interleaved U and V (color) data from the second plane of the NV12 texture.
        */
        uvd.Format = DXGI_FORMAT_R8G8_UNORM;

        //This creates the SRV for the UV plane, allowing the pixel shader to access the color data of the video.
        device->CreateShaderResourceView(videoTexture, &uvd, &srvUV);
        return true;
    }

    /*
    Performs the critical task of transferring decoded video data from FFmpeg's internal hardware surfaces into the DirectX 11 texture 
    that the application uses for rendering.
    */
    void CopyFrameToDX11Texture(ID3D11DeviceContext* ctx, AVFrame* frame) 
    {
        ID3D11Texture2D* src = (ID3D11Texture2D*)frame->data[0];
        UINT idx = (UINT)(intptr_t)frame->data[1];
        D3D11_TEXTURE2D_DESC d; src->GetDesc(&d);
        ctx->CopySubresourceRegion(videoTexture, 0, 0, 0, 0, src, idx, nullptr);
        ctx->CopySubresourceRegion(videoTexture, 1, 0, 0, 0, src, d.ArraySize + idx, nullptr);
    }
};

// --- DXRenderer Class ---
class DXRenderer {
public:
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    IDXGISwapChain* swapChain = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    ID3D11SamplerState* sampler = nullptr;
    ID3D11Buffer* vBuffer = nullptr;
    ID3D11Buffer* alphaCB = nullptr;
    ID3D11BlendState* blendState = nullptr;

    ~DXRenderer() {
        if (vBuffer) vBuffer->Release(); if (alphaCB) alphaCB->Release();
        if (sampler) sampler->Release(); if (rtv) rtv->Release();
        if (blendState) blendState->Release();
        if (swapChain) swapChain->Release();
        if (context) context->Release(); if (device) device->Release();
    }

    bool Init(HWND hwnd) {
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

    void Resize(int w, int h) {
        if (rtv) rtv->Release();
        swapChain->ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN, 0);
        ID3D11Texture2D* bb;
        swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb);
        device->CreateRenderTargetView(bb, nullptr, &rtv);
        bb->Release();
    }

    void BeginFrame() {
        float clearColor[4] = { 0, 0, 0, 1 };
        context->ClearRenderTargetView(rtv, clearColor);
        UINT stride = sizeof(Vertex), offset = 0;
        context->IASetVertexBuffers(0, 1, &vBuffer, &stride, &offset);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->PSSetSamplers(0, 1, &sampler);
        context->OMSetRenderTargets(1, &rtv, nullptr);
    }

    void DrawVideo(VideoSource& src, DXShader& shader, float alpha, bool blend, float winW, float winH) {
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

    void EndFrame() 
    { 
        swapChain->Present(1, 0); 
    }
};

// --- Window & Main ---
DXRenderer g_Renderer;
bool g_Space = false;
bool isFullscreen = false;
WINDOWPLACEMENT g_WindowedPlacement = { sizeof(WINDOWPLACEMENT) };

void ToggleFullscreen(HWND hwnd) {
    isFullscreen = !isFullscreen;
    if (isFullscreen) {
        // Save windowed placement and style, then go borderless fullscreen
        GetWindowPlacement(hwnd, &g_WindowedPlacement);
        LONG style = GetWindowLong(hwnd, GWL_STYLE);
        SetWindowLong(hwnd, GWL_STYLE, style & ~WS_OVERLAPPEDWINDOW);
        MONITORINFO mi = { sizeof(mi) };
        GetMonitorInfo(MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY), &mi);
        SetWindowPos(hwnd, HWND_TOP,
            mi.rcMonitor.left, mi.rcMonitor.top,
            mi.rcMonitor.right - mi.rcMonitor.left,
            mi.rcMonitor.bottom - mi.rcMonitor.top,
            SWP_NOOWNERZORDER | SWP_FRAMECHANGED);

        while (ShowCursor(FALSE) >= 0);

    } else {
        // Restore windowed style and placement
        LONG style = GetWindowLong(hwnd, GWL_STYLE);
        SetWindowLong(hwnd, GWL_STYLE, style | WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(hwnd, &g_WindowedPlacement);
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);

        while (ShowCursor(TRUE) < 0);
    }
}

LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_DESTROY) PostQuitMessage(0);
    if (m == WM_KEYDOWN && w == VK_SPACE) g_Space = true;
    if (m == WM_KEYDOWN && w == 'F')
		ToggleFullscreen(h);

    if (m == WM_SIZE && g_Renderer.swapChain) g_Renderer.Resize(0, 0);
    return DefWindowProc(h, m, w, l);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
//int main() 
{
    WNDCLASS wc = { 0 }; wc.lpfnWndProc = WndProc; wc.lpszClassName = L"VP"; wc.hInstance = GetModuleHandle(NULL);
    RegisterClass(&wc);
    HWND hwnd = CreateWindow(L"VP", L"OOP Video Player", WS_OVERLAPPEDWINDOW, 100, 100, 1280, 720, 0, 0, wc.hInstance, 0);

    if (!g_Renderer.Init(hwnd)) return -1;
    DXShader videoShader;
    /*videoShader.Init(g_Renderer.device, shaderSource);*/
	videoShader.LoadFromFile(g_Renderer.device, L"shaders.hlsl");

    VideoSource bgVideo, fgVideo;
    // Update these paths to your local files
    bgVideo.OpenFile("Videos/13.mp4", g_Renderer.device, g_Renderer.context);
    fgVideo.OpenFile("Videos/1.mp4", g_Renderer.device, g_Renderer.context);
    
	bgVideo.looped = true;
	bgVideo.Play(GetTimeStd());

    bool fgActive = false;
    double fgPts = 0;
    ShowWindow(hwnd, SW_SHOW);
    ToggleFullscreen(hwnd);

    MSG msg = { 0 };
    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) { DispatchMessage(&msg); continue; }

        if (g_Space) 
        { 
            g_Space = false; 
            fgActive = true; 
			fgVideo.Rewind();
			fgVideo.Play(GetTimeStd());
        }

        double bgPts;
        bgVideo.GetNextFrame(g_Renderer.context, bgPts);

        if (fgActive) 
        {
            if (!fgVideo.GetNextFrame(g_Renderer.context, fgPts)) 
                fgActive = false;
        }

        RECT rc; GetClientRect(hwnd, &rc);
        float w = (float)(rc.right - rc.left);
        float h = (float)(rc.bottom - rc.top);

        g_Renderer.BeginFrame();
        g_Renderer.DrawVideo(bgVideo, videoShader, 1.0f, false, w, h);
        
        if (fgActive) 
        {
            float fade = (fgPts < 1.0) ? (float)fgPts : ((fgVideo.duration - fgPts < 1.0) ? (float)(fgVideo.duration - fgPts) : 1.0f);
            g_Renderer.DrawVideo(fgVideo, videoShader, max(0.0f, fade), true, w, h);
        }
        g_Renderer.EndFrame();
    }
    return 0;
}