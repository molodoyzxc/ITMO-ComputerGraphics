#include <windows.h>
#include "Window.h"
#include "Timer.h"
#include "DX12Framework.h"
#include "RenderingSystem.h"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
    Window window(hInstance, nCmdShow, L"DirectX12", 1920.0f, 1080.0f);

    DX12Framework framework(window.GetHwnd(), window.GetWidth(), window.GetHeight());
    framework.Init();

    RenderingSystem system(&framework, window.GetInput());
    system.Initialize();

    Timer timer;    

    while (window.ProcessMessages())
    {
        timer.Tick();
        system.Update(timer.GetTotalSeconds());
        system.Render();
    }

    return 0;
}