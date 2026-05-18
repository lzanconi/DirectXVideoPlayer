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
#include "DXShader.h"
#include "VideoSource.h"
#include "DXRenderer.h"
#include "App.h"    

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "avcodec.lib")
#pragma comment(lib, "avformat.lib")
#pragma comment(lib, "avutil.lib")
#pragma comment(lib, "d3dcompiler.lib")

//WINDOW APP
//Make it a Windows application without a console window, and specify the entry point as mainCRTStartup to avoid linker errors about missing WinMain.
//#pragma comment(linker, "/SUBSYSTEM:windows /ENTRY:mainCRTStartup")

//CONSOLE APP
//Make it a console application to allow for console output, and specify the entry point as mainCRTStartup to avoid linker errors about missing WinMain.
#pragma comment(linker, "/SUBSYSTEM:console /ENTRY:mainCRTStartup")

//int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
int main() 
{
    try
    {
		App app(1280, 720);
		app.Run();
    }
    catch (const std::exception& ex)
    {
        MessageBoxA(nullptr, ex.what(), "Error", MB_ICONERROR);
	}
}