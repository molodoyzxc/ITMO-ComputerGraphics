#include <windows.h>
#include "Window.h"
#include "Timer.h"
#include "DX12Framework.h"
#include "CubeApp.h"
#include "SolarSystemApp.h"
#include "PingPongApp.h"
#include "TestApp.h"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
    // Создание окна
    Window window(hInstance, nCmdShow, L"Dota 3", 1280.0f, 720.0f);

    // Фреймворк
    DX12Framework framework(window.GetHwnd(), window.GetWidth(), window.GetHeight());
    framework.Init();

    IGameApp* app = new PingPongApp(&framework, window.GetInput());
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