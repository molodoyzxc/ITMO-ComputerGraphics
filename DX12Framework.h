#pragma once
#include <wrl.h>
#include <d3d12.h>
#include "d3dx12.h"
#include <dxgi1_4.h>
#include <stdexcept>

using Microsoft::WRL::ComPtr;

class DX12Framework
{
public:
    DX12Framework(HWND hwnd, UINT width, UINT height);
    ~DX12Framework();
    void Init();
    void Clear(const FLOAT clearColor[4]);
    void Present();
    D3D12_CPU_DESCRIPTOR_HANDLE GetDSVHandle() const { return m_dsvHandle; }
    ID3D12Device* GetDevice() const { return m_device.Get(); }
    // Командная очередь
    ID3D12CommandQueue* GetCommandQueue() const { return m_commandQueue.Get(); }
    // Командный список
    ID3D12GraphicsCommandList* GetCommandList() const { return m_commandList.Get(); }
    // Аллокатор для сброса командного списка
    ID3D12CommandAllocator* GetCommandAllocator() const { return m_commandAllocator.Get(); }
    // Текущий RTV-дескриптор
    D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentRTVHandle() const {
        return CD3DX12_CPU_DESCRIPTOR_HANDLE(
            m_rtvHeap->GetCPUDescriptorHandleForHeapStart(),
            m_backBufferIndex,
            m_rtvDescriptorSize
        );
    }
    ID3D12Resource* GetCurrentBackBufferResource() const {
        return m_renderTargets[m_backBufferIndex].Get();
    }

    ID3D12DescriptorHeap* GetSrvHeap() const { return m_srvHeap.Get(); }
    ID3D12DescriptorHeap* GetSamplerHeap() const { return m_samplerHeap.Get(); }
    UINT GetSrvDescriptorSize() const { return m_srvDescriptorSize; }
    UINT GetSamplerDescriptorSize() const { return m_samplerDescriptorSize; }
    float GetWidth() const { return m_width; };
    float GetHeight() const { return m_height; };

    void BeginFrame();
    void EndFrame();
    void WaitForGpu();
    void ClearColorAndDepthBuffer(float clear[4]);
    void SetViewportAndScissors();
    void SetRootSignatureAndPSO(ID3D12RootSignature* root, ID3D12PipelineState* state);
    UINT AllocateSrvDescriptor() {
        if (m_nextSrvDescriptor >= m_srvHeap->GetDesc().NumDescriptors)
            throw std::runtime_error("SRV heap is full");
        return m_nextSrvDescriptor++;
    }



private:
    void CreateDevice();
    void CreateCommandObjects();
    void CreateSwapChain();
    void CreateDescriptorHeaps();
    void CreateRenderTargetViews();
    void CreateDepthResources();

    HWND                m_hwnd;
    float                m_width;
    float                m_height;

    static const UINT   FrameCount = 2;
    UINT m_nextSrvDescriptor = 0;
    ComPtr<IDXGISwapChain3> m_swapChain;
    ComPtr<ID3D12Device> m_device;
    ComPtr<ID3D12CommandQueue> m_commandQueue;
    ComPtr<ID3D12CommandAllocator> m_commandAllocator;
    ComPtr<ID3D12GraphicsCommandList> m_commandList;
    ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
    ComPtr<ID3D12DescriptorHeap> m_srvHeap;
    ComPtr<ID3D12DescriptorHeap> m_samplerHeap;
    UINT m_srvDescriptorSize;
    UINT m_samplerDescriptorSize;
    UINT m_rtvDescriptorSize;
    ComPtr<ID3D12Resource> m_renderTargets[FrameCount];
    UINT m_backBufferIndex;
    ComPtr<ID3D12Resource> m_depthBuffer;
    D3D12_CPU_DESCRIPTOR_HANDLE m_dsvHandle;
    ComPtr<ID3D12Fence> m_fence;
    UINT64 m_fenceValue;
    HANDLE m_fenceEvent;
};
