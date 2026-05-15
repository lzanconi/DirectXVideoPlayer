#pragma once
#include <atomic>
#include <string>

// Forward declarations for FFmpeg structs to keep the header clean
struct AVFormatContext;
struct AVCodecContext;
struct AVBufferRef;
struct AVFrame;
struct AVPacket;
struct ID3D11Texture2D;
struct ID3D11ShaderResourceView;
struct ID3D11Device;
struct ID3D11DeviceContext;

class VideoSource
{
public:
    AVFormatContext* fmtCtx = nullptr;
    AVCodecContext* decCtx = nullptr;
    ID3D11Texture2D* videoTexture = nullptr;
    ID3D11ShaderResourceView* srvY = nullptr;
    ID3D11ShaderResourceView* srvUV = nullptr;
    int streamIdx = -1;
    int width = 0;
    int height = 0;
    double duration = 0.0;
    double startTime = 0.0;
    double lastPTS = -1.0;
    double internalPTS = 0.0;
    bool isInitialized = false;
    bool looped = false;
    float fadeInDuration = 2.0f;
    float fadeOutDuration = 2.0f;
    std::atomic<int64_t> bg_capture_time_ns;

public:
	VideoSource() = default;
    ~VideoSource();

    bool OpenFile(const std::string& path, ID3D11Device* device, ID3D11DeviceContext* context);
    bool GetNextFrame(ID3D11DeviceContext* context);
    void Play(double startTime);
	void Rewind();

private:
    bool CreateResources(ID3D11Device* device);
    void CopyFrameToDX11Texture(ID3D11DeviceContext* ctx, AVFrame* frame);
};

