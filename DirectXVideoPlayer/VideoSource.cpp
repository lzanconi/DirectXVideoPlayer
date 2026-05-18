#include "VideoSource.h"
#include <windows.h>
#include <d3d11.h>
#include <chrono>
#include "utils.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/frame.h>
#include <libavutil/hwcontext_d3d11va.h>
}

/*
Responsible for initializing the FFmpeg decoder and setting up the DirectX 11 hardware acceleration resources for a specific video file.
*/

VideoSource::~VideoSource()
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

bool VideoSource::OpenFile(const std::string& path, ID3D11Device* device, ID3D11DeviceContext* context)
{
	file_name = GetFilenameFromPath(path);
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
bool VideoSource::GetNextFrame(ID3D11DeviceContext* context)
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
                            internalPTS = frame->best_effort_timestamp * av_q2d(fmtCtx->streams[streamIdx]->time_base);
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

void VideoSource::Play(double startTime)
{
    this->startTime = startTime;
	internalPTS = 0.0;
    lastPTS = -1.0; // Reset lastPTS to allow immediate frame decoding
}

void VideoSource::Rewind()
{
    if (!isInitialized)
        return;

    avcodec_flush_buffers(decCtx);
    av_seek_frame(fmtCtx, streamIdx, 0, AVSEEK_FLAG_BACKWARD);
    lastPTS = -1.0;
    internalPTS = 0.0;
}

/*
Responsible for allocating the DirectX 11 textures and Shader Resource Views (SRVs) required to display
the video frames decoded by FFmpeg.
*/
bool VideoSource::CreateResources(ID3D11Device* device)
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
void VideoSource::CopyFrameToDX11Texture(ID3D11DeviceContext* ctx, AVFrame* frame)
{
    ID3D11Texture2D* src = (ID3D11Texture2D*)frame->data[0];
    UINT idx = (UINT)(intptr_t)frame->data[1];
    D3D11_TEXTURE2D_DESC d; src->GetDesc(&d);
    ctx->CopySubresourceRegion(videoTexture, 0, 0, 0, 0, src, idx, nullptr);
    ctx->CopySubresourceRegion(videoTexture, 1, 0, 0, 0, src, d.ArraySize + idx, nullptr);
}