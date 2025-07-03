#include "Pipeline.h"
#include "DX12Framework.h"
#include <d3dcompiler.h>
#include "d3dx12.h"
#include <stdexcept>
#include <windows.h>
#include "Vertexes.h"
#pragma comment(lib, "d3dcompiler.lib")

inline void ThrowIfFailed(HRESULT hr)
{
    if (FAILED(hr)) throw std::runtime_error("HRESULT failed");
}

Pipeline::Pipeline(DX12Framework* framework)
    : m_framework(framework)
{
}

void Pipeline::Init()
{
    Microsoft::WRL::ComPtr<ID3DBlob> vsBlob, psBlob, errorBlob;
    UINT compileFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
    compileFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    HRESULT hr = D3DCompileFromFile(
        L"TexturedShaders.hlsl",
        nullptr, nullptr,
        "VSMain", "vs_5_0",
        compileFlags, 0,
        &vsBlob, &errorBlob
    );
    if (FAILED(hr)) {
        if (errorBlob) {
            OutputDebugStringA("VS compile error:\n");
            OutputDebugStringA((char*)errorBlob->GetBufferPointer());
        }
        ThrowIfFailed(hr);
    }

    hr = D3DCompileFromFile(
        L"TexturedShaders.hlsl",
        nullptr, nullptr,
        "PSMain", "ps_5_0",
        compileFlags, 0,
        &psBlob, &errorBlob
    );
    if (FAILED(hr)) {
        if (errorBlob) {
            OutputDebugStringA("PS compile error:\n");
            OutputDebugStringA((char*)errorBlob->GetBufferPointer());
        }
        ThrowIfFailed(hr);
    }

    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0,  24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    CD3DX12_DESCRIPTOR_RANGE srvRange;
    srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0); // t0
    CD3DX12_DESCRIPTOR_RANGE samplerRange;
    samplerRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 1, 0); // s0

    CD3DX12_ROOT_PARAMETER slotParam[3];
    slotParam[0].InitAsConstantBufferView(0);
    slotParam[1].InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_PIXEL);
    slotParam[2].InitAsDescriptorTable(1, &samplerRange, D3D12_SHADER_VISIBILITY_PIXEL);

    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc;
    rootSigDesc.Init(
        _countof(slotParam),
        slotParam,
        0, nullptr,
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
    );

    ComPtr<ID3DBlob> serializedRS, errorBlobRS;
    hr = D3D12SerializeRootSignature(
        &rootSigDesc,
        D3D_ROOT_SIGNATURE_VERSION_1,
        &serializedRS,
        &errorBlobRS
    );
    if (FAILED(hr)) {
        if (errorBlobRS) {
            OutputDebugStringA("RootSignature serialize failed:\n");
            OutputDebugStringA((char*)errorBlobRS->GetBufferPointer());
        }
        else {
            char buf[64];
            sprintf_s(buf, "RootSignature serialize HRESULT=0x%08X\n", hr);
            OutputDebugStringA(buf);
        }
        ThrowIfFailed(hr);
    }

    ThrowIfFailed(
        m_framework->GetDevice()->CreateRootSignature(
            0,
            serializedRS->GetBufferPointer(),
            serializedRS->GetBufferSize(),
            IID_PPV_ARGS(&m_rootSignature)
        )
    );

    // opaque PSO
    D3D12_GRAPHICS_PIPELINE_STATE_DESC opaqueDesc = {};
    opaqueDesc.InputLayout = { inputLayout, _countof(inputLayout) }; 
    opaqueDesc.pRootSignature = m_rootSignature.Get();               
    opaqueDesc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
    opaqueDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };

    // состояние растеризатора
    opaqueDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    opaqueDesc.RasterizerState.FrontCounterClockwise = FALSE;          
    opaqueDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK ;  

    // blend
    opaqueDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        
    // глубина и трафарет
    opaqueDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    opaqueDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    opaqueDesc.DepthStencilState.DepthEnable = TRUE;
    opaqueDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;

    opaqueDesc.SampleMask = UINT_MAX;
    opaqueDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    opaqueDesc.NumRenderTargets = 1;
    opaqueDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM; // формат цветового буффреа
    opaqueDesc.SampleDesc.Count = 1;

    // создание
    ThrowIfFailed(m_framework->GetDevice()->CreateGraphicsPipelineState(
        &opaqueDesc, IID_PPV_ARGS(&m_opaquePSO)
    ));

    // transparent PSO
    D3D12_GRAPHICS_PIPELINE_STATE_DESC transparentDesc = {};
    transparentDesc.InputLayout = { inputLayout, _countof(inputLayout) };
    transparentDesc.pRootSignature = m_rootSignature.Get();
    transparentDesc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
    transparentDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };

    transparentDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    transparentDesc.RasterizerState.FrontCounterClockwise = FALSE;
    transparentDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;

    // blending
    D3D12_BLEND_DESC blendDesc = {};
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    transparentDesc.BlendState = blendDesc;

    transparentDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    transparentDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    transparentDesc.DepthStencilState.DepthEnable = TRUE;
    transparentDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;

    transparentDesc.SampleMask = UINT_MAX;
    transparentDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    transparentDesc.NumRenderTargets = 1;
    transparentDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    transparentDesc.SampleDesc.Count = 1;

    ThrowIfFailed(m_framework->GetDevice()->CreateGraphicsPipelineState(
        &transparentDesc, IID_PPV_ARGS(&m_transparentPSO)
    ));
}