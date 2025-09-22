#include "ParticleSystem.h"
#include <stdexcept>
#include <DirectXMath.h>
#include "Meshes.h"
using namespace DirectX;

struct UpdateCB
{
    float dt; float accel[3];
    UINT  spawnCount;
    float emitterPos[3];
    float initialSpeed;
    UINT  aliveCount;
};

static inline void ThrowIfFailed(HRESULT hr) { if (FAILED(hr)) throw std::runtime_error("hr"); }

ParticleSystem::ParticleSystem(DX12Framework* fw, Pipeline* pipe)
    : m_framework(fw), m_pipeline(pipe) {
}

void ParticleSystem::Initialize(UINT maxParticles, UINT initialSpawn)
{
    m_maxParticles = maxParticles;
    m_initialSpawn = initialSpawn;

    auto* dev = m_framework->GetDevice();

    {
        const UINT64 totalBytes = static_cast<UINT64>(m_maxParticles) * sizeof(ParticleCPU);
        CD3DX12_HEAP_PROPERTIES defHeap(D3D12_HEAP_TYPE_DEFAULT);
        auto bufDesc = CD3DX12_RESOURCE_DESC::Buffer(totalBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

        ThrowIfFailed(dev->CreateCommittedResource(
            &defHeap, D3D12_HEAP_FLAG_NONE, &bufDesc,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&m_bufA)));

        ThrowIfFailed(dev->CreateCommittedResource(
            &defHeap, D3D12_HEAP_FLAG_NONE, &bufDesc,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&m_bufB)));

        m_stateBufA = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        m_stateBufB = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }

    {
        CD3DX12_HEAP_PROPERTIES defHeap(D3D12_HEAP_TYPE_DEFAULT);
        auto cntDesc = CD3DX12_RESOURCE_DESC::Buffer(4, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

        ThrowIfFailed(dev->CreateCommittedResource(
            &defHeap, D3D12_HEAP_FLAG_NONE, &cntDesc,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&m_cntA)));

        ThrowIfFailed(dev->CreateCommittedResource(
            &defHeap, D3D12_HEAP_FLAG_NONE, &cntDesc,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&m_cntB)));

        m_stateCntA = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        m_stateCntB = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }

    {
        CD3DX12_HEAP_PROPERTIES rbHeap(D3D12_HEAP_TYPE_READBACK);
        auto rbDesc = CD3DX12_RESOURCE_DESC::Buffer(4);
        ThrowIfFailed(dev->CreateCommittedResource(
            &rbHeap, D3D12_HEAP_FLAG_NONE, &rbDesc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_readbackCount)));

        CD3DX12_HEAP_PROPERTIES upHeap(D3D12_HEAP_TYPE_UPLOAD);
        ThrowIfFailed(dev->CreateCommittedResource(
            &upHeap, D3D12_HEAP_FLAG_NONE, &rbDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_uploadZero)));

        uint32_t* p = nullptr; CD3DX12_RANGE rr(0, 0);
        ThrowIfFailed(m_uploadZero->Map(0, &rr, reinterpret_cast<void**>(&p)));
        *p = 0;
        CD3DX12_RANGE wr(0, 0); m_uploadZero->Unmap(0, &wr);
    }

    {
        CD3DX12_HEAP_PROPERTIES upHeap(D3D12_HEAP_TYPE_UPLOAD);
        auto cbDesc = CD3DX12_RESOURCE_DESC::Buffer(Align256(64));
        ThrowIfFailed(dev->CreateCommittedResource(
            &upHeap, D3D12_HEAP_FLAG_NONE, &cbDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_updateCB)));

        CD3DX12_RANGE rr(0, 0);
        ThrowIfFailed(m_updateCB->Map(0, &rr, reinterpret_cast<void**>(&m_updatePtr)));
    }

    {
        struct ObjCB { XMFLOAT4X4 World; XMFLOAT4X4 ViewProj; } obj{};
        XMStoreFloat4x4(&obj.World, XMMatrixIdentity());
        XMStoreFloat4x4(&obj.ViewProj, XMMatrixIdentity());

        CD3DX12_HEAP_PROPERTIES upHeap(D3D12_HEAP_TYPE_UPLOAD);
        auto cbDesc = CD3DX12_RESOURCE_DESC::Buffer(Align256(sizeof(ObjCB)));
        ThrowIfFailed(dev->CreateCommittedResource(
            &upHeap, D3D12_HEAP_FLAG_NONE, &cbDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_objectCB)));

        uint8_t* p = nullptr; CD3DX12_RANGE rr(0, 0);
        ThrowIfFailed(m_objectCB->Map(0, &rr, reinterpret_cast<void**>(&p)));
        memcpy(p, &obj, sizeof(obj));
        m_objectCBAddr = m_objectCB->GetGPUVirtualAddress();
    }

    {
        Mesh plane = CreateCube();
        const UINT vbSize = static_cast<UINT>(plane.vertices.size() * sizeof(Vertex));
        const UINT ibSize = static_cast<UINT>(plane.indices.size() * sizeof(uint32_t));
        if (vbSize == 0 || ibSize == 0) throw std::runtime_error("Particle mesh empty");

        ComPtr<ID3D12Resource> vbUpload, ibUpload;
        auto* cl = m_framework->GetCommandList();
        auto* al = m_framework->GetCommandAllocator();

        ThrowIfFailed(al->Reset());
        ThrowIfFailed(cl->Reset(al, nullptr));

        m_framework->CreateDefaultBuffer(cl, plane.vertices.data(), vbSize, m_vb, vbUpload);
        m_framework->CreateDefaultBuffer(cl, plane.indices.data(), ibSize, m_ib, ibUpload);

        m_vbv.BufferLocation = m_vb->GetGPUVirtualAddress();
        m_vbv.SizeInBytes = vbSize;
        m_vbv.StrideInBytes = sizeof(Vertex);

        m_ibv.BufferLocation = m_ib->GetGPUVirtualAddress();
        m_ibv.SizeInBytes = ibSize;
        m_ibv.Format = DXGI_FORMAT_R32_UINT;

        m_indexCount = (UINT)plane.indices.size();

        ThrowIfFailed(cl->Close());
        ID3D12CommandList* lists[] = { cl };
        m_framework->GetCommandQueue()->ExecuteCommandLists(1, lists);
        m_framework->WaitForGpu();
    }

    {
        D3D12_DESCRIPTOR_HEAP_DESC hd = {};
        hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        hd.NumDescriptors = 2;
        hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        ThrowIfFailed(dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&m_computeHeap)));

        m_dhInc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        m_gpuBase = m_computeHeap->GetGPUDescriptorHandleForHeapStart();
    }

    {
        auto* cmd = m_framework->GetCommandList();
        auto* alloc = m_framework->GetCommandAllocator();
        ThrowIfFailed(alloc->Reset());
        ThrowIfFailed(cmd->Reset(alloc, nullptr));

        ResetUavCounter(cmd, m_cntB.Get(), m_stateCntB);

        WriteUavDescriptors(m_bufA.Get(), m_cntA.Get(), m_bufB.Get(), m_cntB.Get());

        UpdateCB cb{};
        cb.dt = 0.0f;
        cb.accel[0] = 0.0f;
        cb.accel[1] = 0.0f;
        cb.accel[2] = 0.0f;
        cb.spawnCount = m_initialSpawn;
        cb.emitterPos[0] = 0.0f;
        cb.emitterPos[1] = 50.0f;
        cb.emitterPos[2] = 0.0f;
        cb.initialSpeed = 10.0f;
        cb.aliveCount = 0u;

        memcpy(m_updatePtr, &cb, sizeof(cb));

        ID3D12DescriptorHeap* heaps[] = { m_computeHeap.Get() };
        cmd->SetDescriptorHeaps(1, heaps);
        cmd->SetComputeRootSignature(m_pipeline->GetParticlesComputeRS());
        cmd->SetPipelineState(m_pipeline->GetParticlesEmitCSO());
        cmd->SetComputeRootDescriptorTable(0, m_gpuBase);
        cmd->SetComputeRootConstantBufferView(1, m_updateCB->GetGPUVirtualAddress());

        const UINT groups = (m_initialSpawn + 255u) / 256u;
        if (groups) cmd->Dispatch(groups, 1, 1);

        CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::UAV(nullptr);
        cmd->ResourceBarrier(1, &barrier);
        TransitIfNeeded(cmd, m_bufB.Get(), m_stateBufB, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        ThrowIfFailed(cmd->Close());
        ID3D12CommandList* lists[] = { cmd };
        m_framework->GetCommandQueue()->ExecuteCommandLists(1, lists);
        m_framework->WaitForGpu();

        m_usingAasRead = false;
        m_stateBufA = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }
}

void ParticleSystem::ResetUavCounter(ID3D12GraphicsCommandList* cmd, ID3D12Resource* counter, D3D12_RESOURCE_STATES& stateVar)
{
    TransitIfNeeded(cmd, counter, stateVar, D3D12_RESOURCE_STATE_COPY_DEST);
    cmd->CopyBufferRegion(counter, 0, m_uploadZero.Get(), 0, 4);
    TransitIfNeeded(cmd, counter, stateVar, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
}

void ParticleSystem::WriteUavDescriptors(
    ID3D12Resource* inBuf, ID3D12Resource* inCounter,
    ID3D12Resource* outBuf, ID3D12Resource* outCounter)
{
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
    uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uav.Format = DXGI_FORMAT_UNKNOWN;
    uav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
    uav.Buffer.StructureByteStride = sizeof(ParticleCPU);
    uav.Buffer.NumElements = m_maxParticles;
    uav.Buffer.CounterOffsetInBytes = 0;

    auto dev = m_framework->GetDevice();
    auto hCPU0 = m_computeHeap->GetCPUDescriptorHandleForHeapStart();
    auto hCPU1 = CD3DX12_CPU_DESCRIPTOR_HANDLE(hCPU0, 1, m_dhInc);

    dev->CreateUnorderedAccessView(inBuf, inCounter, &uav, hCPU0);
    dev->CreateUnorderedAccessView(outBuf, outCounter, &uav, hCPU1);
}

void ParticleSystem::Simulate(ID3D12GraphicsCommandList* cmd, float dt)
{
    ID3D12Resource* srcBuf = m_usingAasRead ? m_bufA.Get() : m_bufB.Get();
    ID3D12Resource* dstBuf = m_usingAasRead ? m_bufB.Get() : m_bufA.Get();
    ID3D12Resource* srcCnt = m_usingAasRead ? m_cntA.Get() : m_cntB.Get();
    ID3D12Resource* dstCnt = m_usingAasRead ? m_cntB.Get() : m_cntA.Get();

    auto& stateSrc = (m_usingAasRead ? m_stateBufA : m_stateBufB);
    auto& stateDst = (m_usingAasRead ? m_stateBufB : m_stateBufA);
    auto& stateCntSrc = (m_usingAasRead ? m_stateCntA : m_stateCntB);
    auto& stateCntDst = (m_usingAasRead ? m_stateCntB : m_stateCntA);

    TransitIfNeeded(cmd, srcBuf, stateSrc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    TransitIfNeeded(cmd, dstBuf, stateDst, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    ResetUavCounter(cmd, dstCnt, stateCntDst);

    TransitIfNeeded(cmd, srcCnt, stateCntSrc, D3D12_RESOURCE_STATE_COPY_SOURCE);
    cmd->CopyBufferRegion(m_readbackCount.Get(), 0, srcCnt, 0, 4);
    TransitIfNeeded(cmd, srcCnt, stateCntSrc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    cmd->Close();
    {
        ID3D12CommandList* lists[] = { cmd };
        m_framework->GetCommandQueue()->ExecuteCommandLists(1, lists);
        m_framework->WaitForGpu();
    }

    {
        uint32_t* p = nullptr; CD3DX12_RANGE rr(0, 4);
        ThrowIfFailed(m_readbackCount->Map(0, &rr, reinterpret_cast<void**>(&p)));
        m_aliveCount = *p;
        CD3DX12_RANGE wr(0, 0); m_readbackCount->Unmap(0, &wr);
    }

    auto* alloc = m_framework->GetCommandAllocator();
    ThrowIfFailed(alloc->Reset());
    ThrowIfFailed(m_framework->GetCommandList()->Reset(alloc, nullptr));
    cmd = m_framework->GetCommandList();

    if (m_aliveCount > 0)
    {
        UpdateCB cb{};
        cb.dt = dt;
        cb.accel[0] = 0.0f;
        cb.accel[1] = 0.0f;
        cb.accel[2] = 0.0f;
        cb.spawnCount = 0;
        cb.emitterPos[0] = 0.0f;
        cb.emitterPos[1] = 50.0f;
        cb.emitterPos[2] = 0.0f;
        cb.initialSpeed = 0.0f;
        cb.aliveCount = m_aliveCount;
        memcpy(m_updatePtr, &cb, sizeof(cb));

        WriteUavDescriptors(srcBuf, srcCnt, dstBuf, dstCnt);

        ID3D12DescriptorHeap* heaps[] = { m_computeHeap.Get() };
        cmd->SetDescriptorHeaps(1, heaps);
        cmd->SetComputeRootSignature(m_pipeline->GetParticlesComputeRS());
        cmd->SetPipelineState(m_pipeline->GetParticlesUpdateCSO());
        cmd->SetComputeRootDescriptorTable(0, m_gpuBase);
        cmd->SetComputeRootConstantBufferView(1, m_updateCB->GetGPUVirtualAddress());

        const UINT threads = 256;
        const UINT groups = (m_aliveCount + threads - 1) / threads;
        cmd->Dispatch(groups, 1, 1);
    }

    CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::UAV(nullptr);
    cmd->ResourceBarrier(1, &barrier);
    TransitIfNeeded(cmd, dstBuf, stateDst, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    m_usingAasRead = !m_usingAasRead;
}

void ParticleSystem::DrawGBuffer(ID3D12GraphicsCommandList* cmd)
{
    if (m_indexCount == 0 || m_aliveCount == 0) return;

    cmd->SetGraphicsRootSignature(m_pipeline->GetRootSignature());
    cmd->SetPipelineState(m_pipeline->GetGBufferParticlesPSO());

    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->IASetVertexBuffers(0, 1, &m_vbv);
    cmd->IASetIndexBuffer(&m_ibv);

    cmd->SetGraphicsRootConstantBufferView(0, m_objectCBAddr);

    ID3D12Resource* readBuf = m_usingAasRead ? m_bufA.Get() : m_bufB.Get();
    cmd->SetGraphicsRootShaderResourceView(6, readBuf->GetGPUVirtualAddress());

    cmd->DrawIndexedInstanced(m_indexCount, m_aliveCount, 0, 0, 0);
}
