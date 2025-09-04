#include "Pipeline.h"
#include "DX12Framework.h"
#include <d3dcompiler.h>
#include "d3dx12.h"
#include <stdexcept>
#include <windows.h>
#include "Vertexes.h"
#pragma comment(lib, "d3dcompiler.lib")
#include <dxcapi.h>

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
    UINT compileFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
    compileFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    ComPtr<IDxcBlob> vsBlob, psBlob, vsG, psG, vsQuad, psLight, psAmbientBlob;
    Compile(L"Shaders.hlsl", L"VSMain", L"vs_6_0", vsBlob);
    Compile(L"Shaders.hlsl", L"PSMain", L"ps_6_0", psBlob);
    Compile(L"Shaders.hlsl", L"VS_GBuffer", L"vs_6_0", vsG);
    Compile(L"Shaders.hlsl", L"PS_GBuffer", L"ps_6_0", psG);
    Compile(L"Shaders.hlsl", L"VS_Quad", L"vs_6_0", vsQuad);
    Compile(L"Shaders.hlsl", L"PS_Lighting", L"ps_6_0", psLight);
    Compile(L"Shaders.hlsl", L"PS_Ambient", L"ps_6_0", psAmbientBlob);

    ComPtr<IDxcBlob> vsTessBlob, hsTessBlob, dsTessBlob;
    Compile(L"Tessellation.hlsl", L"VSMain", L"vs_6_0", vsTessBlob);
    Compile(L"Tessellation.hlsl", L"HSMain", L"hs_6_0", hsTessBlob);
    Compile(L"Tessellation.hlsl", L"DSMain", L"ds_6_0", dsTessBlob);

    ComPtr<IDxcBlob> vsShadow;
    Compile(L"Shaders.hlsl", L"VS_Shadow", L"vs_6_0", vsShadow);

    ComPtr<IDxcBlob> vsGPart, psGPart;
    Compile(L"Shaders.hlsl", L"VS_GBufferParticle", L"vs_6_0", vsGPart);
    Compile(L"Shaders.hlsl", L"PS_GBufferParticle", L"ps_6_0", psGPart);

    ComPtr<IDxcBlob> csUpdate, csEmit;
    Compile(L"ParticlesCS.hlsl", L"CS_Update", L"cs_6_0", csUpdate);
    Compile(L"ParticlesCS.hlsl", L"CS_Emit", L"cs_6_0", csEmit);

    ComPtr<IDxcBlob> psPost;
    Compile(L"Shaders.hlsl", L"PS_Post", L"ps_6_0", psPost);

    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TANGENT",  0, DXGI_FORMAT_R32G32B32_FLOAT, 0,32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "HAND",     0, DXGI_FORMAT_R32_FLOAT,      0,44, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    CD3DX12_DESCRIPTOR_RANGE samplerRange;
    samplerRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 1, 0);

    const UINT MaxSrv = m_framework->GetSrvHeap()->GetDesc().NumDescriptors;
    
    CD3DX12_DESCRIPTOR_RANGE srvRange;
    srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, MaxSrv, 0);
    
    CD3DX12_ROOT_PARAMETER slotParam[7];
    slotParam[0].InitAsConstantBufferView(0, D3D12_SHADER_VISIBILITY_ALL);
    slotParam[1].InitAsConstantBufferView(1, D3D12_SHADER_VISIBILITY_ALL);
    slotParam[2].InitAsConstantBufferView(3, D3D12_SHADER_VISIBILITY_ALL);
    slotParam[3].InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_ALL);
    slotParam[4].InitAsDescriptorTable(1, &samplerRange, D3D12_SHADER_VISIBILITY_ALL);
    slotParam[5].InitAsConstantBufferView(4, D3D12_SHADER_VISIBILITY_ALL);
    slotParam[6].InitAsShaderResourceView(0, 1,D3D12_SHADER_VISIBILITY_ALL);

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
    opaqueDesc.RasterizerState.FrontCounterClockwise = FALSE;
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

    D3D12_GRAPHICS_PIPELINE_STATE_DESC gbTessDesc = geoDesc;
    gbTessDesc.VS = { vsTessBlob->GetBufferPointer(), vsTessBlob->GetBufferSize() };
    gbTessDesc.HS = { hsTessBlob->GetBufferPointer(), hsTessBlob->GetBufferSize() };
    gbTessDesc.DS = { dsTessBlob->GetBufferPointer(), dsTessBlob->GetBufferSize() };
    gbTessDesc.PS = { psG->GetBufferPointer(), psG->GetBufferSize() };
    gbTessDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
    ThrowIfFailed(m_framework->GetDevice()->CreateGraphicsPipelineState(
        &gbTessDesc, IID_PPV_ARGS(&m_gBufferTessellationPSO)
    ));

    D3D12_GRAPHICS_PIPELINE_STATE_DESC gbTessWireDesc = gbTessDesc;
    gbTessWireDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
    ThrowIfFailed(m_framework->GetDevice()->CreateGraphicsPipelineState(
        &gbTessWireDesc, IID_PPV_ARGS(&m_gBufferTessellationWireframePSO)
    ));

    CD3DX12_DESCRIPTOR_RANGE defSrvRanges[3];
    defSrvRanges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 0);
    defSrvRanges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 4);
    defSrvRanges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 5);

    CD3DX12_DESCRIPTOR_RANGE defSampRange;
    defSampRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 2, 0);

    CD3DX12_ROOT_PARAMETER defParams[6];
    defParams[0].InitAsDescriptorTable(1, &defSrvRanges[0], D3D12_SHADER_VISIBILITY_PIXEL);
    defParams[1].InitAsConstantBufferView(1, 0, D3D12_SHADER_VISIBILITY_PIXEL);
    defParams[2].InitAsConstantBufferView(2, 0, D3D12_SHADER_VISIBILITY_PIXEL);
    defParams[3].InitAsDescriptorTable(1, &defSampRange, D3D12_SHADER_VISIBILITY_PIXEL);
    defParams[4].InitAsDescriptorTable(1, &defSrvRanges[1], D3D12_SHADER_VISIBILITY_PIXEL);
    defParams[5].InitAsDescriptorTable(1, &defSrvRanges[2], D3D12_SHADER_VISIBILITY_PIXEL);

    CD3DX12_ROOT_SIGNATURE_DESC deferredRSDesc;
    deferredRSDesc.Init(
        _countof(defParams),
        defParams,
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
    defDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;;
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
    ambientDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;;

    for (int i = 1; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i) {
        ambientDesc.RTVFormats[i] = DXGI_FORMAT_UNKNOWN;
    }

    ThrowIfFailed(m_framework->GetDevice()->CreateGraphicsPipelineState(
        &ambientDesc, IID_PPV_ARGS(&m_ambientPSO)
    ));

    D3D12_GRAPHICS_PIPELINE_STATE_DESC shadowDesc{};
    shadowDesc.InputLayout = { inputLayout, _countof(inputLayout) };
    shadowDesc.pRootSignature = m_rootSignature.Get();
    shadowDesc.VS = { vsShadow->GetBufferPointer(), vsShadow->GetBufferSize() };
    shadowDesc.PS = { nullptr, 0 };
    shadowDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    shadowDesc.RasterizerState.DepthBias = 64;
    shadowDesc.RasterizerState.SlopeScaledDepthBias = 1.5f;
    shadowDesc.RasterizerState.DepthBiasClamp = 0.0f;
    shadowDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    shadowDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    shadowDesc.SampleMask = UINT_MAX;
    shadowDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    shadowDesc.NumRenderTargets = 0;
    for (int i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i) shadowDesc.RTVFormats[i] = DXGI_FORMAT_UNKNOWN;
    shadowDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    shadowDesc.SampleDesc.Count = 1;
    ThrowIfFailed(m_framework->GetDevice()->CreateGraphicsPipelineState(&shadowDesc, IID_PPV_ARGS(&m_shadowPSO)));

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoGP = {};
    psoGP.pRootSignature = m_rootSignature.Get();
    psoGP.VS = { vsGPart->GetBufferPointer(), vsGPart->GetBufferSize() };
    psoGP.PS = { psGPart->GetBufferPointer(), psGPart->GetBufferSize() };
    psoGP.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoGP.SampleMask = UINT_MAX;
    psoGP.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoGP.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoGP.InputLayout = { inputLayout, _countof(inputLayout) };
    psoGP.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
    psoGP.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoGP.NumRenderTargets = 3;
    psoGP.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoGP.RTVFormats[1] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    psoGP.RTVFormats[2] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoGP.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    psoGP.SampleDesc.Count = 1;

    ThrowIfFailed(m_framework->GetDevice()->CreateGraphicsPipelineState(
        &psoGP, IID_PPV_ARGS(&m_gbufferParticlesPSO)));

    CD3DX12_DESCRIPTOR_RANGE uavRange;
    uavRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 0, 0);

    CD3DX12_ROOT_PARAMETER crp[2];
    crp[0].InitAsDescriptorTable(1, &uavRange, D3D12_SHADER_VISIBILITY_ALL);
    crp[1].InitAsConstantBufferView(5, D3D12_SHADER_VISIBILITY_ALL);        

    CD3DX12_ROOT_SIGNATURE_DESC csDesc;
    csDesc.Init(_countof(crp), crp, 0, nullptr);

    ComPtr<ID3DBlob> sigBlob, sigErr;
    ThrowIfFailed(D3D12SerializeRootSignature(&csDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &sigErr));
    ThrowIfFailed(m_framework->GetDevice()->CreateRootSignature(
        0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(), IID_PPV_ARGS(&m_particlesComputeRS)));

    D3D12_COMPUTE_PIPELINE_STATE_DESC csd = {};
    csd.pRootSignature = m_particlesComputeRS.Get();
    csd.CS = { csUpdate->GetBufferPointer(), csUpdate->GetBufferSize() };
    ThrowIfFailed(m_framework->GetDevice()->CreateComputePipelineState(&csd, IID_PPV_ARGS(&m_particlesUpdateCSO)));
    csd.CS = { csEmit->GetBufferPointer(), csEmit->GetBufferSize() };
    ThrowIfFailed(m_framework->GetDevice()->CreateComputePipelineState(&csd, IID_PPV_ARGS(&m_particlesEmitCSO)));

    D3D12_GRAPHICS_PIPELINE_STATE_DESC postDesc = {};
    postDesc.InputLayout = { nullptr, 0 };
    postDesc.pRootSignature = m_deferredRootSig.Get();
    postDesc.VS = { vsQuad->GetBufferPointer(), vsQuad->GetBufferSize() };
    postDesc.PS = { psPost->GetBufferPointer(), psPost->GetBufferSize() };
    postDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    postDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    postDesc.DepthStencilState.DepthEnable = FALSE;
    postDesc.SampleMask = UINT_MAX;
    postDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    postDesc.NumRenderTargets = 1;
    postDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    postDesc.SampleDesc.Count = 1;
    ThrowIfFailed(m_framework->GetDevice()->CreateGraphicsPipelineState(
        &postDesc, IID_PPV_ARGS(&m_postPSO)
    ));
}

void Pipeline::Compile(LPCWSTR file, LPCWSTR entry, LPCWSTR target, ComPtr<IDxcBlob>& outBlob)
{
    ComPtr<IDxcLibrary>  library;
    ComPtr<IDxcCompiler> compiler;
    DxcCreateInstance(CLSID_DxcLibrary, IID_PPV_ARGS(&library));
    DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler));

    ComPtr<IDxcBlobEncoding> source;
    library->CreateBlobFromFile(file, nullptr, &source);
    ComPtr<IDxcOperationResult> result;
    compiler->Compile(
        source.Get(),
        file,
        entry, target,
        nullptr, 0, nullptr, 0, nullptr,
        &result
    );
    HRESULT hr;
    result->GetStatus(&hr);
    if (FAILED(hr)) {
        ComPtr<IDxcBlobEncoding> errors;
        result->GetErrorBuffer(&errors);
        OutputDebugStringA((char*)errors->GetBufferPointer());
        throw std::runtime_error("DXC compile failed");
    }
    result->GetResult(&outBlob);
}