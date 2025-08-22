#pragma once
#include <wrl/client.h>
#include <d3d12.h>
#include <vector>
#include "d3dx12.h"
#include "DX12Framework.h"
#include "Pipeline.h"
#include <DirectXMath.h>

using namespace DirectX;

using Microsoft::WRL::ComPtr;

struct ParticleCPU {
    float pos[3];
    float vel[3];
    float age;
    float lifetime;
    float size;
};

class ParticleSystem {
public:
    ParticleSystem(DX12Framework* fw, Pipeline* pipe);
    ~ParticleSystem() = default;

    void Initialize(UINT maxParticles, UINT initialSpawn);
    void Simulate(ID3D12GraphicsCommandList* cmd, float dt);
    void DrawGBuffer(ID3D12GraphicsCommandList* cmd);

    void UpdateViewProj(const XMMATRIX& viewProj) {
        if (!m_objectCB) return;
        uint8_t* p = nullptr; CD3DX12_RANGE r(0, 0);
        if (SUCCEEDED(m_objectCB->Map(0, &r, reinterpret_cast<void**>(&p)))) {
            struct ObjCB { XMFLOAT4X4 World; XMFLOAT4X4 ViewProj; };
            ObjCB* cb = reinterpret_cast<ObjCB*>(p);
            XMStoreFloat4x4(&cb->ViewProj, viewProj);
            CD3DX12_RANGE wr(0, 0); m_objectCB->Unmap(0, &wr);
        }
    }

private:
    static UINT Align256(UINT x) { return (x + 255u) & ~255u; }

    inline void TransitIfNeeded(ID3D12GraphicsCommandList* cmd,
        ID3D12Resource* res,
        D3D12_RESOURCE_STATES& current,
        D3D12_RESOURCE_STATES target)
    {
        if (current == target) return;
        auto b = CD3DX12_RESOURCE_BARRIER::Transition(res, current, target);
        cmd->ResourceBarrier(1, &b);
        current = target;
    }

    void ResetUavCounter(ID3D12GraphicsCommandList* cmd, ID3D12Resource* counter, D3D12_RESOURCE_STATES& stateVar);
    void WriteUavDescriptors(ID3D12Resource* inBuf, ID3D12Resource* inCounter,
        ID3D12Resource* outBuf, ID3D12Resource* outCounter);

private:
    DX12Framework* m_framework = nullptr;
    Pipeline* m_pipeline = nullptr;

    UINT m_maxParticles = 0;
    UINT m_aliveCount = 0;
    UINT m_initialSpawn = 0;

    ComPtr<ID3D12Resource> m_bufA;
    ComPtr<ID3D12Resource> m_bufB;
    ComPtr<ID3D12Resource> m_cntA;
    ComPtr<ID3D12Resource> m_cntB;

    bool m_usingAasRead = false;

    D3D12_RESOURCE_STATES m_stateBufA = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    D3D12_RESOURCE_STATES m_stateBufB = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    D3D12_RESOURCE_STATES m_stateCntA = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    D3D12_RESOURCE_STATES m_stateCntB = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    ComPtr<ID3D12Resource> m_readbackCount;
    ComPtr<ID3D12Resource> m_uploadZero;   

    ComPtr<ID3D12Resource> m_updateCB;
    uint8_t* m_updatePtr = nullptr;

    ComPtr<ID3D12Resource> m_objectCB;
    D3D12_GPU_VIRTUAL_ADDRESS m_objectCBAddr = 0;

    ComPtr<ID3D12DescriptorHeap> m_computeHeap;
    D3D12_GPU_DESCRIPTOR_HANDLE  m_gpuBase{};
    UINT                         m_dhInc = 0;

    ComPtr<ID3D12Resource> m_vb;
    ComPtr<ID3D12Resource> m_ib;
    D3D12_VERTEX_BUFFER_VIEW m_vbv{};
    D3D12_INDEX_BUFFER_VIEW  m_ibv{};
    UINT m_indexCount = 0;
};
