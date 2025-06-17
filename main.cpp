#include <windows.h>
#include "Window.h"
#include "Timer.h"
#include "DX12Framework.h"
#include "CubeApp.h"
#include "SolarSystemApp.h"
#include "PingPongApp.h"
#include "TestApp.h"
#include "TexturedCubeApp.h"
#include "ModelApp.h"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
    Window window(hInstance, nCmdShow, L"DirectX12", 1280.0f, 720.0f);

    DX12Framework framework(window.GetHwnd(), window.GetWidth(), window.GetHeight());
    framework.Init();

    IGameApp* app = new ModelApp(&framework, window.GetInput());
    app->Initialize();

    Timer timer;    

    while (window.ProcessMessages())
    {
        timer.Tick();
        app->Update(timer.GetTotalSeconds());
        app->Render();
    }

    delete app;
    return 0;
}