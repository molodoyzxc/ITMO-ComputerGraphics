#pragma once
#include <wrl.h>
#include <d3d12.h>

class DX12Framework;

class Pipeline
{
public:
    Pipeline(DX12Framework* framework);
    ~Pipeline() = default;

    void Init();

    ID3D12RootSignature* GetRootSignature() const { return m_rootSignature.Get(); }
    ID3D12PipelineState* GetPipelineState()    const { return m_pipelineState.Get(); }

private:
    DX12Framework* m_framework;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pipelineState;
};
