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
    Microsoft::WRL::ComPtr<ID3DBlob> vsBlob, psBlob, vsG, psG, vsQuad, psLight, psAmbientBlob, errorBlob;

    UINT compileFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
    compileFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    HRESULT hr;

    hr = D3DCompileFromFile(
        L"Deferred.hlsl", nullptr, nullptr,
        "VSMain", "vs_5_0", compileFlags, 0,
        &vsBlob, &errorBlob
    );
    if (FAILED(hr) && errorBlob) {
        OutputDebugStringA("Compile VSMain error:\n");
        OutputDebugStringA((char*)errorBlob->GetBufferPointer());
    }
    ThrowIfFailed(hr);

    hr = D3DCompileFromFile(
        L"Deferred.hlsl", nullptr, nullptr,
        "PSMain", "ps_5_0", compileFlags, 0,
        &psBlob, &errorBlob
    );
    if (FAILED(hr) && errorBlob) {
        OutputDebugStringA("Compile PSMain error:\n");
        OutputDebugStringA((char*)errorBlob->GetBufferPointer());
    }
    ThrowIfFailed(hr);

    hr = D3DCompileFromFile(
        L"Deferred.hlsl", nullptr, nullptr,
        "VS_GBuffer", "vs_5_0", compileFlags, 0,
        &vsG, &errorBlob
    );
    if (FAILED(hr) && errorBlob) {
        OutputDebugStringA("Compile VS gbuffer error:\n");
        OutputDebugStringA((char*)errorBlob->GetBufferPointer());
    }
    ThrowIfFailed(hr);

    hr = D3DCompileFromFile(
        L"Deferred.hlsl", nullptr, nullptr,
        "PS_GBuffer", "ps_5_0", compileFlags, 0,
        &psG, &errorBlob
    );
    if (FAILED(hr) && errorBlob) {
        OutputDebugStringA("Compile PS gbuffer error:\n");
        OutputDebugStringA((char*)errorBlob->GetBufferPointer());
    }
    ThrowIfFailed(hr);

    hr = D3DCompileFromFile(
        L"Deferred.hlsl", nullptr, nullptr,
        "VS_Quad", "vs_5_0", compileFlags, 0,
        &vsQuad, &errorBlob
    );
    if (FAILED(hr) && errorBlob) {
        OutputDebugStringA("Compile VS quad error:\n");
        OutputDebugStringA((char*)errorBlob->GetBufferPointer());
    }
    ThrowIfFailed(hr);

    hr = D3DCompileFromFile(
        L"Deferred.hlsl", nullptr, nullptr,
        "PS_Lighting", "ps_5_0", compileFlags, 0,
        &psLight, &errorBlob
    );
    if (FAILED(hr) && errorBlob) {
        OutputDebugStringA("Compile PS lightning error:\n");
        OutputDebugStringA((char*)errorBlob->GetBufferPointer());
    }
    ThrowIfFailed(hr);

    hr = D3DCompileFromFile(
        L"Deferred.hlsl", nullptr, nullptr,
        "PS_Ambient", "ps_5_0", compileFlags, 0,
        &psAmbientBlob, &errorBlob
    );
    if (FAILED(hr) && errorBlob) {
        OutputDebugStringA("Compile PS lightning error:\n");
        OutputDebugStringA((char*)errorBlob->GetBufferPointer());
    }
    ThrowIfFailed(hr);

    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    CD3DX12_DESCRIPTOR_RANGE srvRange, samplerRange;
    srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    samplerRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 1, 0);

    CD3DX12_ROOT_PARAMETER slotParam[4];
    slotParam[0].InitAsConstantBufferView(0);
    slotParam[1].InitAsConstantBufferView(1);
    slotParam[2].InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_PIXEL);
    slotParam[3].InitAsDescriptorTable(1, &samplerRange, D3D12_SHADER_VISIBILITY_PIXEL);

    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc;
    rootSigDesc.Init(
        _countof(slotParam),
        slotParam,
        0, nullptr,
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
    );

    Microsoft::WRL::ComPtr<ID3DBlob> serializedRS, errorBlobRS;
    ThrowIfFailed(D3D12SerializeRootSignature(
        &rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
        &serializedRS, &errorBlobRS
    ));
    ThrowIfFailed(m_framework->GetDevice()->CreateRootSignature(
        0,
        serializedRS->GetBufferPointer(),
        serializedRS->GetBufferSize(),
        IID_PPV_ARGS(&m_rootSignature)
    ));

    D3D12_GRAPHICS_PIPELINE_STATE_DESC opaqueDesc = {};
    opaqueDesc.InputLayout = { inputLayout, _countof(inputLayout) };
    opaqueDesc.pRootSignature = m_rootSignature.Get();
    opaqueDesc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
    opaqueDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
    opaqueDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    opaqueDesc.RasterizerState.FrontCounterClockwise = TRUE;
    opaqueDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    opaqueDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    opaqueDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    opaqueDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    opaqueDesc.DepthStencilState.DepthEnable = TRUE;
    opaqueDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    opaqueDesc.SampleMask = UINT_MAX;
    opaqueDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    opaqueDesc.NumRenderTargets = 1;
    opaqueDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    opaqueDesc.SampleDesc.Count = 1;
    ThrowIfFailed(m_framework->GetDevice()->CreateGraphicsPipelineState(
        &opaqueDesc, IID_PPV_ARGS(&m_opaquePSO)
    ));

    D3D12_GRAPHICS_PIPELINE_STATE_DESC transparentDesc = opaqueDesc;
    {
        D3D12_BLEND_DESC blend = {};
        blend.RenderTarget[0].BlendEnable = TRUE;
        blend.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        blend.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        blend.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        blend.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
        blend.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        transparentDesc.BlendState = blend;
    }
    transparentDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    ThrowIfFailed(m_framework->GetDevice()->CreateGraphicsPipelineState(
        &transparentDesc, IID_PPV_ARGS(&m_transparentPSO)
    ));

    D3D12_GRAPHICS_PIPELINE_STATE_DESC geoDesc = opaqueDesc;
    geoDesc.NumRenderTargets = 4;
    geoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    geoDesc.RTVFormats[1] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    geoDesc.RTVFormats[2] = DXGI_FORMAT_R8G8B8A8_UNORM;
    geoDesc.RTVFormats[3] = DXGI_FORMAT_R32G32B32A32_FLOAT;
    geoDesc.VS = { vsG->GetBufferPointer(), vsG->GetBufferSize() };
    geoDesc.PS = { psG->GetBufferPointer(), psG->GetBufferSize() };
    ThrowIfFailed(m_framework->GetDevice()->CreateGraphicsPipelineState(
        &geoDesc, IID_PPV_ARGS(&m_gBufferPSO)
    ));

    srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 0);
    CD3DX12_ROOT_PARAMETER deferredParams[4];
    deferredParams[0].InitAsDescriptorTable(1, &srvRange);
    deferredParams[1].InitAsConstantBufferView(1);
    deferredParams[2].InitAsConstantBufferView(2);
    deferredParams[3].InitAsDescriptorTable(1, &samplerRange);

    CD3DX12_ROOT_SIGNATURE_DESC deferredRSDesc;
    deferredRSDesc.Init(
        _countof(deferredParams),
        deferredParams,
        0, nullptr,
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
    );

    ThrowIfFailed(D3D12SerializeRootSignature(
        &deferredRSDesc, D3D_ROOT_SIGNATURE_VERSION_1, &serializedRS, &errorBlobRS
    ));
    ThrowIfFailed(m_framework->GetDevice()->CreateRootSignature(
        0, serializedRS->GetBufferPointer(), serializedRS->GetBufferSize(),
        IID_PPV_ARGS(&m_deferredRootSig)
    ));

    D3D12_GRAPHICS_PIPELINE_STATE_DESC defDesc = {};
    defDesc.InputLayout = { nullptr, 0 };
    defDesc.pRootSignature = m_deferredRootSig.Get();
    defDesc.VS = { vsQuad->GetBufferPointer(), vsQuad->GetBufferSize() };
    defDesc.PS = { psLight->GetBufferPointer(), psLight->GetBufferSize() };
    defDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    opaqueDesc.RasterizerState.FrontCounterClockwise = TRUE;
    opaqueDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;

    D3D12_BLEND_DESC blendDesc = {};
    blendDesc.AlphaToCoverageEnable = FALSE;
    blendDesc.IndependentBlendEnable = FALSE;
    auto& rt = blendDesc.RenderTarget[0];
    rt.BlendEnable = TRUE;
    rt.SrcBlend = D3D12_BLEND_ONE;
    rt.DestBlend = D3D12_BLEND_ONE;
    rt.BlendOp = D3D12_BLEND_OP_ADD;
    rt.SrcBlendAlpha = D3D12_BLEND_ONE;
    rt.DestBlendAlpha = D3D12_BLEND_ONE;
    rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    defDesc.BlendState = blendDesc;

    defDesc.DepthStencilState.DepthEnable = FALSE;
    defDesc.SampleMask = UINT_MAX;
    defDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    defDesc.NumRenderTargets = 1;
    defDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    defDesc.SampleDesc.Count = 1;
    ThrowIfFailed(m_framework->GetDevice()->CreateGraphicsPipelineState(
        &defDesc, IID_PPV_ARGS(&m_deferredPSO)
    ));

    D3D12_GRAPHICS_PIPELINE_STATE_DESC ambientDesc = defDesc;
    D3D12_BLEND_DESC blendReplace = {};
    blendReplace.AlphaToCoverageEnable = FALSE;
    blendReplace.IndependentBlendEnable = FALSE;

    auto& rt0 = blendReplace.RenderTarget[0];
    rt0.BlendEnable = TRUE;
    rt0.LogicOpEnable = FALSE;

    rt0.SrcBlend = D3D12_BLEND_ONE;
    rt0.DestBlend = D3D12_BLEND_ZERO;
    rt0.BlendOp = D3D12_BLEND_OP_ADD;

    rt0.SrcBlendAlpha = D3D12_BLEND_ONE;
    rt0.DestBlendAlpha = D3D12_BLEND_ZERO;
    rt0.BlendOpAlpha = D3D12_BLEND_OP_ADD; 

    rt0.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    ambientDesc.BlendState = blendReplace;
    ambientDesc.PS = { psAmbientBlob->GetBufferPointer(), psAmbientBlob->GetBufferSize() };

    ambientDesc.NumRenderTargets = 1;
    ambientDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;

    for (int i = 1; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i) {
        ambientDesc.RTVFormats[i] = DXGI_FORMAT_UNKNOWN;
    }

    ThrowIfFailed(m_framework->GetDevice()->CreateGraphicsPipelineState(
        &ambientDesc, IID_PPV_ARGS(&m_ambientPSO)
    ));
}
