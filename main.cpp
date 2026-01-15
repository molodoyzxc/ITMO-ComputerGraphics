#include <windows.h>
#include "Window.h"
#include "Timer.h"
#include "DX12Framework.h"
#include "RenderingSystem.h"
#include "imgui.h"
#include "imgui_impl_dx12.h"
#include "imgui_impl_win32.h"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
    ImGui_ImplWin32_EnableDpiAwareness();

    Window window(hInstance, nCmdShow, L"DirectX12", 1920.0f, 1080.0f);

    DX12Framework framework(window.GetHwnd(), window.GetWidth(), window.GetHeight());
    framework.Init();

    float main_scale = ImGui_ImplWin32_GetDpiScaleForHwnd(window.GetHwnd());

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(main_scale);   
    style.FontScaleDpi = main_scale;
    ImGui_ImplWin32_Init(window.GetHwnd());

    ImGui_ImplDX12_InitInfo init_info = {};
    init_info.Device = framework.GetDevice();
    init_info.CommandQueue = framework.GetCommandQueue();
    init_info.NumFramesInFlight = framework.GetFrameCount() + 1;
    init_info.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    init_info.DSVFormat = DXGI_FORMAT_UNKNOWN;
    init_info.SrvDescriptorHeap = framework.GetSrvHeap();
    UINT imguiSrvIndex = framework.AllocateSrvDescriptor();
    CD3DX12_CPU_DESCRIPTOR_HANDLE imguiCpuHandle(
        framework.GetSrvHeap()->GetCPUDescriptorHandleForHeapStart(),
        imguiSrvIndex,
        framework.GetSrvDescriptorSize()
    );
    CD3DX12_GPU_DESCRIPTOR_HANDLE imguiGpuHandle(
        framework.GetSrvHeap()->GetGPUDescriptorHandleForHeapStart(),
        imguiSrvIndex,
        framework.GetSrvDescriptorSize()
    );

    init_info.LegacySingleSrvCpuDescriptor = imguiCpuHandle;
    init_info.LegacySingleSrvGpuDescriptor = imguiGpuHandle;

    ImGui_ImplDX12_Init(&init_info);

    RenderingSystem system(&framework, window.GetInput());
    system.Initialize();

    Timer timer;    

    while (window.ProcessMessages())
    {
        system.Update(timer.GetTotalSeconds());
        system.Render();
    }

    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    return 0;
}