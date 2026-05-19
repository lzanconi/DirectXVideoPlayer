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
Responsible for managing the decoding loop, converting raw packets from the video stream into displayable DirectX textures 
while handling timing and looping
*/
bool VideoSource::GetNextFrame(ID3D11DeviceContext* context)
{
    //If the video file hasn't been opened yet, it exits immediately
    if (!isInitialized)
        return true;

    //Get the current system runtime in seconds
    double currentTime = GetTimeStd();

    //If startTime is 0 or negative, it means this specific video source has not been told to start playing yet
    if (startTime <= 0)
        return true;

    //Calculates the playback position (playPos) by subtracting the video's launch time (startTime) from the 
    //current system time. This tells the player how far into the video it should be right now
    double playPos = currentTime - startTime;

    //Holds raw, compressed data read from the file container
    AVPacket* raw_packet = av_packet_alloc();
    //An empty destination container that will hold the uncompressed picture data after decoding
    AVFrame* frame = av_frame_alloc();
    //A boolean flag initialized to false that will track whether we successfully extract an actual image frame
    bool frameDecoded = false;

    //If playPos is greater than lastPTS, it means the application's clock has moved forward past the current frame, 
    //and it is time to parse and decode the next image from the video file.
    if (playPos > lastPTS)
    {
        //Loop that will keep extracting and parsing packets until an actual video frame is fully generated and decoded
        while (!frameDecoded)
        {
            //read the next raw packet out of the video file container (fmtCtx) and stores it in raw_packet
            if (av_read_frame(fmtCtx, raw_packet) >= 0)
            {
                //Checks if the packet we just read belongs to our primary video track (streamIdx)
                if (raw_packet->stream_index == streamIdx)
                {
                    //Pushes the compressed raw video packet into the decoder context (decCtx)
                    //Returning 0 means the decoder successfully accepted the raw packet
                    if (avcodec_send_packet(decCtx, raw_packet) == 0)
                    {
                        //FRAME FULLY DECODED
                        //Attempts to retrieve an uncompressed, decoded image frame back from the decoder. 
                        if (avcodec_receive_frame(decCtx, frame) == 0)
                        {
                            //Calculates the exact playback timestamp of the decoded frame in seconds.
                            internalPTS = frame->best_effort_timestamp * av_q2d(fmtCtx->streams[streamIdx]->time_base);

                            //Performs a GPU-side copy transferring the decoded image from the decoder into our 
                            //DirectX 11 textures (srvY and srvUV) for rendering
                            CopyFrameToDX11Texture(context, frame);

                            //lastPTS to match our current playback clock location so we know what time marker is now on screen
                            lastPTS = playPos;

							//Tells that a frame has been decoded allowing the main loop to proceed with rendering it
                            frameDecoded = true;

                            //Takes a nanosecond system snapshot at the exact moment this background frame was processed. 
							//This timestamp is stored in an atomic variable so to be used by NetworkManger when sending positions
                            //allowing for better synchronization between video playback and network transmission.
                            bg_capture_time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                std::chrono::steady_clock::now().time_since_epoch()).count();
                        }
                    }
                }
                //Relase the raw packet memory
                av_packet_unref(raw_packet);
            }
			//END OF THE VIDEO FILE REACHED
            else
            {
                //If the video has to loop...
                if (looped)
                {
                    //Calls Rewind() to clear the decoder buffer and seek back to frame 0
                    Rewind();
                    //Restarts the playback 
                    Play(GetTimeStd());
                    //Forces frameDecoded = true so the application can immediately transition back into the first frame
                    frameDecoded = true;
                }
                //if the video is not set to loop...
                else
                {
					//Just clear frame and raw packet memory and exit the loop, leaving the last frame on screen until 
                    //the application decides to play or rewind again
                    av_frame_free(&frame);
                    av_packet_free(&raw_packet);
                    return false;
                }
            }
        }
    }

    //Frees the local frame and raw_packet pointers allocated at the top of the function to clean up heap allocations
    av_frame_free(&frame);
    av_packet_free(&raw_packet);

    //Returns the value of frameDecoded (true if a new frame was successfully loaded or if a loop cycle was forced).
    return frameDecoded;
}

/*
Establish the starting time for a specific video stream so that the decoding loop (GetNextFrame) can track precisely 
when to decode and display incoming frames.
*/
void VideoSource::Play(double startTime)
{
    this->startTime = startTime;
    internalPTS = 0.0;

    //By resetting lastPTS to -1.0, any valid playback time (playPos >= 0.0) is guaranteed to be greater than lastPTS. 
    //This forces the FFmpeg/DirectX pipeline to immediately pass the conditional check and decode the very first frame 
    //of the video on the next frame loop, preventing an initial lag or rendering delay.
    lastPTS = -1.0;
}

/*
Responsible for resetting a video stream back to its absolute beginning (frame 0) and flushing out any stale, cached frames 
from the hardware decoder.
*/
void VideoSource::Rewind()
{
    if (!isInitialized)
        return;

    //Completely wipe the internal processing buffers of the decoder context
    avcodec_flush_buffers(decCtx);
	//Instructs FFmpeg to change its read pointer inside the video container file (fmtCtx) 
    //back to the very first frame (timestamp 0)
    av_seek_frame(fmtCtx, streamIdx, 0, AVSEEK_FLAG_BACKWARD);

    lastPTS = -1.0;
    internalPTS = 0.0;
}

/*
Responsible for dynamically calculating the opacity of a foreground video frame at any given moment in its playback timeline
*/
float VideoSource::ComputeAlpha()
{
    //Resets the video alpha to a default state of 1.0f. 
    //If the video is not currently within its fade-in or its fade-out window, it will remain fully visible
    alpha = 1.0f;

    //Checks to see if the video has already finished its initial entry fade in
    if (!fadeInComplete)
    {

        if (internalPTS < fadeInDuration)
        {
            alpha = (float)internalPTS / fadeInDuration;
            return alpha;
        }
        fadeInComplete = true;
    }

    //isSequenceLoop - Verifies that the video isn't currently set to repeat inside an automated sequential playlist. 
    //(If a video is supposed to loop seamlessly back to the beginning, you don't want it fading to black at the end of its timeline).
    if (!isSequenceLoop && (duration - internalPTS < fadeOutDuration))
    {
        alpha = (float)(duration - internalPTS) / fadeOutDuration;
    }
    return alpha;
}

/*
Responsible for allocating the DirectX 11 textures and Shader Resource Views (SRVs) required to display
the video frames decoded by FFmpeg.
*/
bool VideoSource::CreateResources(ID3D11Device* device)
{
    D3D11_TEXTURE2D_DESC td =
    {
        (UINT)width,
        (UINT)height, 1, 1,
        DXGI_FORMAT_NV12,
        {1,0},
        D3D11_USAGE_DEFAULT,
        D3D11_BIND_SHADER_RESOURCE
    };

    device->CreateTexture2D(&td, nullptr, &videoTexture);

    D3D11_SHADER_RESOURCE_VIEW_DESC yd =
    {
        DXGI_FORMAT_R8_UNORM,
        D3D11_SRV_DIMENSION_TEXTURE2D
    };

    yd.Texture2D.MipLevels = 1;

    device->CreateShaderResourceView(videoTexture, &yd, &srvY);

    D3D11_SHADER_RESOURCE_VIEW_DESC uvd = yd;

    uvd.Format = DXGI_FORMAT_R8G8_UNORM;

    device->CreateShaderResourceView(videoTexture, &uvd, &srvUV);
    return true;
}

void VideoSource::CopyFrameToDX11Texture(ID3D11DeviceContext* ctx, AVFrame* frame)
{
    ID3D11Texture2D* src = (ID3D11Texture2D*)frame->data[0];
    UINT idx = (UINT)(intptr_t)frame->data[1];
    D3D11_TEXTURE2D_DESC d; src->GetDesc(&d);
    ctx->CopySubresourceRegion(videoTexture, 0, 0, 0, 0, src, idx, nullptr);
    ctx->CopySubresourceRegion(videoTexture, 1, 0, 0, 0, src, d.ArraySize + idx, nullptr);
}