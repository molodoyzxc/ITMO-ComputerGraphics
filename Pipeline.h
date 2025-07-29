#pragma once
#include <wrl.h>
#include <d3d12.h>

class DX12Framework;
using Microsoft::WRL::ComPtr;

class Pipeline
{
public:
    Pipeline(DX12Framework* framework);
    ~Pipeline() = default;

    void Init();

    ID3D12RootSignature* GetRootSignature() const { return m_rootSignature.Get(); }
    ID3D12PipelineState* GetOpaquePSO()    const { return m_opaquePSO.Get(); }
    ID3D12PipelineState* GetTransparentPSO()    const { return m_transparentPSO.Get(); }

    ID3D12PipelineState* GetGBufferPSO()    const { return m_gBufferPSO.Get(); }
    ID3D12RootSignature* GetDeferredRS()   const { return m_deferredRootSig.Get(); }
    ID3D12PipelineState* GetDeferredPSO()  const { return m_deferredPSO.Get(); }
    ID3D12PipelineState* GetAmbientPSO()  const { return m_ambientPSO.Get(); }


private:
    DX12Framework* m_framework;

    ComPtr<ID3D12RootSignature> m_rootSignature;
    ComPtr<ID3D12PipelineState> m_opaquePSO;
    ComPtr<ID3D12PipelineState> m_transparentPSO;

    ComPtr<ID3D12PipelineState> m_gBufferPSO;
    ComPtr<ID3D12RootSignature> m_deferredRootSig;
    ComPtr<ID3D12PipelineState> m_deferredPSO;
    ComPtr<ID3D12PipelineState> m_ambientPSO;
};
