#include "NetworkManager.h"
#include "IApp.h"

#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#pragma comment(lib, "ws2_32.lib")

NetworkManager::NetworkManager(const std::string& serverIP, int serverPort, IApp* appInterface)
    : serverIP(serverIP), serverPort(serverPort), appInterface(appInterface), running(false)
{
}

NetworkManager::~NetworkManager()
{
    Stop();
}

void NetworkManager::Start()
{
    if (running)
        return;
    running = true;
    workerThread = std::thread(&NetworkManager::Run, this);
    std::cout << "NetworkManager: Background thread started." << std::endl;
}

void NetworkManager::Stop()
{
    running = false;
    if (workerThread.joinable())
    {
        workerThread.join();
    }
}

void NetworkManager::Run()
{
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
        return;
    while (running)
    {
        SOCKET clientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (clientSocket == (SOCKET)-1) // INVALID_SOCKET
        {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }

        sockaddr_in serverAddr;
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(serverPort);
        inet_pton(AF_INET, serverIP.c_str(), &serverAddr.sin_addr);

        // Attempt connection
        std::cout << "[Network] Attempting to connect to server at " << serverIP << ":" << serverPort << "..." << std::endl;
        if (connect(clientSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == -1) // SOCKET_ERROR
        {
            closesocket(clientSocket);
            std::this_thread::sleep_for(std::chrono::seconds(2)); // Retry interval
            continue;
        }

        std::cout << "NetworkManager: Connected to Position Server." << std::endl;
        HandlePositionSend(clientSocket);
        closesocket(clientSocket);
    }

	WSACleanup();
}   

void NetworkManager::HandlePositionSend(SOCKET socket)
{
    // 25 FPS = 1 message every 40ms
    /*const std::chrono::milliseconds frameDuration(1000 / targetFramerate);
    auto period_duration = frameDuration;*/

    auto period_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::duration<double>(1.0 / positions_framerate)
    );

    auto next_frame = std::chrono::steady_clock::now() + period_duration;

    while (running)
    {
        auto now = std::chrono::steady_clock::now();
        auto time_left = std::chrono::duration_cast<std::chrono::milliseconds>(next_frame - now);

        // Precise sleep to reduce CPU usage while maintaining accuracy
        if (time_left.count() > 2)
            std::this_thread::sleep_for(time_left - std::chrono::milliseconds(2));

        while (std::chrono::steady_clock::now() < next_frame)
        {
            /* spin until it's time to send */
        }

        auto trigger_time = std::chrono::steady_clock::now();
        int64_t trigger_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(trigger_time.time_since_epoch()).count();

        // Gather data from App via IApp interface
        std::vector<float> positions = appInterface->GetPositions();
        double last_video_time = appInterface->GetLastPTS();
        int64_t capture_time_ns = appInterface->GetBGCaptureTimeNS();
        double calc_time = 0.0;

        // Calculate interpolated playback time for precise position mapping
        if (capture_time_ns > 0)
        {
            int64_t elapsed_ns = trigger_ns - capture_time_ns;
            double elapsed_sec = (double)elapsed_ns / 1000000000.0;

            if (elapsed_sec > 2)
                elapsed_sec = 0;

            calc_time = last_video_time + elapsed_sec;
        }
        else
        {
            calc_time = last_video_time;
        }

        // Apply delay offset
        double delay_s = positions_delay_ms / 1000.0;
        calc_time -= delay_s;
        if (calc_time < 0.0)
            calc_time = 0.0;

        // Map time to position CSV index
        int count = (int)positions.size();
        double exact_index = calc_time * positions_framerate;
        int base_idx = (int)std::floor(exact_index);
        double frac = exact_index - base_idx;

        int idx0 = (std::min)(base_idx, count - 1);
        int idx1 = (std::min)(base_idx + 1, count - 1);

        if (idx0 < 0) idx0 = 0;
        if (idx1 < 0) idx1 = 0;

        float val0 = positions[idx0];
        float val1 = positions[idx1];

        // Linear interpolation between two CSV values
        float calculated_csv_pos = (float)(val0 * (1.0 - frac) + val1 * frac);

        // Prepare JSON payload
        char buffer[128];
        int len = snprintf(buffer, sizeof(buffer), "{\"positions\":[%.4f]}", calculated_csv_pos);

        if (len > 0) {
            // Append null terminator as expected by the Python emulator
            if (send(socket, buffer, len + 1, 0) == -1) // SOCKET_ERROR
            {
                std::cout << "[Network] Connection lost." << std::endl;
                break;
            }
        }

        next_frame += period_duration;
    }
}