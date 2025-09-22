#pragma once
#include <wrl.h>
#include <d3d12.h>
#include <dxcapi.h>

class DX12Framework;
using Microsoft::WRL::ComPtr;

class Pipeline
{
public:
    Pipeline(DX12Framework* framework);
    ~Pipeline() = default;

    void Init();

    ID3D12RootSignature* GetRootSignature() const { return m_rootSignature.Get(); }
    ID3D12PipelineState* GetOpaquePSO() const { return m_opaquePSO.Get(); }
    ID3D12PipelineState* GetTransparentPSO() const { return m_transparentPSO.Get(); }
    ID3D12PipelineState* GetGBufferPSO() const { return m_gBufferPSO.Get(); }
    ID3D12RootSignature* GetDeferredRS() const { return m_deferredRootSig.Get(); }
    ID3D12PipelineState* GetDeferredPSO()const { return m_deferredPSO.Get(); }
    ID3D12PipelineState* GetAmbientPSO() const { return m_ambientPSO.Get(); }
    ID3D12PipelineState* GetGBufferTessellationPSO() const { return m_gBufferTessellationPSO.Get(); }
    ID3D12PipelineState* GetGBufferTessellationWireframePSO() const { return m_gBufferTessellationWireframePSO.Get(); }
    ID3D12PipelineState* GetShadowPSO() const { return m_shadowPSO.Get(); }
    ID3D12PipelineState* GetGBufferParticlesPSO() const { return m_gbufferParticlesPSO.Get(); }
    ID3D12RootSignature* GetParticlesComputeRS() const { return m_particlesComputeRS.Get(); }
    ID3D12PipelineState* GetParticlesUpdateCSO() const { return m_particlesUpdateCSO.Get(); }
    ID3D12PipelineState* GetParticlesEmitCSO() const { return m_particlesEmitCSO.Get(); }
    ID3D12PipelineState* GetPostPSO() const { return m_postPSO.Get(); }
    ID3D12PipelineState* GetSkyPSO() const { return m_skyPSO.Get(); }
    ID3D12PipelineState* GetTonemapPSO()  const { return m_tonemapPSO.Get(); }
    ID3D12PipelineState* GetGammaPSO()    const { return m_gammaPSO.Get(); }
    ID3D12PipelineState* GetVignettePSO() const { return m_vignettePSO.Get(); }
    ID3D12RootSignature* GetPostRS() const { return m_postRootSig.Get(); }
    ID3D12PipelineState* GetCopyHDRtoLDRPSO() const { return m_copyHDRtoLDRPSO.Get(); }
    ID3D12PipelineState* GetCopyLDRPSO() const { return m_copyLDRPSO.Get(); }

private:
    DX12Framework* m_framework;

    ComPtr<ID3D12RootSignature> m_rootSignature;
    ComPtr<ID3D12PipelineState> m_opaquePSO;
    ComPtr<ID3D12PipelineState> m_transparentPSO;
    ComPtr<ID3D12PipelineState> m_gBufferPSO;
    ComPtr<ID3D12RootSignature> m_deferredRootSig;
    ComPtr<ID3D12PipelineState> m_deferredPSO;
    ComPtr<ID3D12PipelineState> m_ambientPSO;
    ComPtr<ID3D12PipelineState> m_gBufferTessellationPSO;
    ComPtr<ID3D12PipelineState> m_gBufferTessellationWireframePSO;
    ComPtr<ID3D12PipelineState> m_shadowPSO;
    ComPtr<ID3D12PipelineState> m_gbufferParticlesPSO;
    ComPtr<ID3D12RootSignature> m_particlesComputeRS;
    ComPtr<ID3D12PipelineState> m_particlesUpdateCSO;
    ComPtr<ID3D12PipelineState> m_particlesEmitCSO;
    ComPtr<ID3D12PipelineState> m_postPSO;
    ComPtr<ID3D12PipelineState> m_skyPSO;
    ComPtr<ID3D12PipelineState> m_tonemapPSO;
    ComPtr<ID3D12PipelineState> m_gammaPSO;
    ComPtr<ID3D12PipelineState> m_vignettePSO;
    ComPtr<ID3D12RootSignature> m_postRootSig;
    ComPtr<ID3D12PipelineState> m_copyHDRtoLDRPSO;
    ComPtr<ID3D12PipelineState> m_copyLDRPSO;

    void Compile(LPCWSTR file, LPCWSTR entry, LPCWSTR target, ComPtr<IDxcBlob>& outBlob);
};
