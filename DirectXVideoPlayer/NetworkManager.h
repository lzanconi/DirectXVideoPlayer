#pragma once
#include <cstdint>
#include <string>
#include <thread>
#include <atomic>

#if defined(_WIN32) || defined(_WIN64)
typedef uintptr_t SOCKET;
#else
typedef int SOCKET;
#endif

class IApp;

class NetworkManager
{
private:
	std::string serverIP;
	int serverPort;
	std::thread workerThread;
	std::atomic<bool> running;
	IApp* appInterface;

	// Configuration for position sending
	double positions_delay_ms = -130.0;
	double positions_framerate = 60.0;

public:
	NetworkManager(const std::string& serverIP, int serverPort, IApp* appInterface);
	~NetworkManager();

	void Start();
	void Stop();

private:
	void Run();
	void HandlePositionSend(SOCKET socket);
};