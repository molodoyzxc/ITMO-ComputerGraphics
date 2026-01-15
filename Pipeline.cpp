#include "Pipeline.h"
#include "DX12Framework.h"
#include <d3dcompiler.h>
#include "d3dx12.h"
#include <stdexcept>
#include <windows.h>
#include "Vertexes.h"
#pragma comment(lib, "d3dcompiler.lib")
#include <dxcapi.h>

namespace
{
    template<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE Type, typename T>
    struct alignas(void*) PSOSubobject
    {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type = Type;
        T data;
    };

    struct MeshGBufferStream
    {
        PSOSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE, ID3D12RootSignature*> RootSig;
        PSOSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS, D3D12_SHADER_BYTECODE> MS;
        PSOSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS, D3D12_SHADER_BYTECODE> PS;
        PSOSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER, D3D12_RASTERIZER_DESC> Raster;
        PSOSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND, D3D12_BLEND_DESC> Blend;
        PSOSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL, D3D12_DEPTH_STENCIL_DESC> Depth;
        PSOSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS, D3D12_RT_FORMAT_ARRAY> RTVs;
        PSOSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT, DXGI_FORMAT> DSV;
        PSOSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC, DXGI_SAMPLE_DESC> Sample;
        PSOSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK, UINT> SampleMask;
        PSOSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY, D3D12_PRIMITIVE_TOPOLOGY_TYPE> Topology;
    };
}

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
    Compile(L"Shaders.hlsl", L"VSMain", L"vs_6_5", vsBlob);
    Compile(L"Shaders.hlsl", L"PSMain", L"ps_6_5", psBlob);
    Compile(L"Shaders.hlsl", L"VS_GBuffer", L"vs_6_5", vsG);
    Compile(L"Shaders.hlsl", L"PS_GBuffer", L"ps_6_5", psG);
    Compile(L"Shaders.hlsl", L"VS_Quad", L"vs_6_5", vsQuad);
    Compile(L"Shaders.hlsl", L"PS_Lighting", L"ps_6_5", psLight);
    Compile(L"Shaders.hlsl", L"PS_Ambient", L"ps_6_5", psAmbientBlob);

    ComPtr<IDxcBlob> vsTessBlob, hsTessBlob, dsTessBlob;
    Compile(L"Tessellation.hlsl", L"VSMain", L"vs_6_5", vsTessBlob);
    Compile(L"Tessellation.hlsl", L"HSMain", L"hs_6_5", hsTessBlob);
    Compile(L"Tessellation.hlsl", L"DSMain", L"ds_6_5", dsTessBlob);

    ComPtr<IDxcBlob> vsShadow;
    Compile(L"Shaders.hlsl", L"VS_Shadow", L"vs_6_5", vsShadow);

    ComPtr<IDxcBlob> vsGPart, psGPart;
    Compile(L"Shaders.hlsl", L"VS_GBufferParticle", L"vs_6_5", vsGPart);
    Compile(L"Shaders.hlsl", L"PS_GBufferParticle", L"ps_6_5", psGPart);

    ComPtr<IDxcBlob> csUpdate, csEmit;
    Compile(L"ParticlesCS.hlsl", L"CS_Update", L"cs_6_5", csUpdate);
    Compile(L"ParticlesCS.hlsl", L"CS_Emit", L"cs_6_5", csEmit);

    ComPtr<IDxcBlob> psSkybox;
    Compile(L"Shaders.hlsl", L"PS_Skybox", L"ps_6_5", psSkybox);

    ComPtr<IDxcBlob> psCopyHDRtoLDR, psCopyLDR;
    ComPtr<IDxcBlob> psTonemap, psGamma, psVignette;
    ComPtr<IDxcBlob> psInvert, psGrayscale, psPixelate, psPosterize, psSaturation;

    Compile(L"PostEffects.hlsl", L"PS_CopyHDRtoLDR", L"ps_6_5", psCopyHDRtoLDR);
    Compile(L"PostEffects.hlsl", L"PS_CopyLDR", L"ps_6_5", psCopyLDR);

    Compile(L"PostEffects.hlsl", L"PS_Tonemap", L"ps_6_5", psTonemap);
    Compile(L"PostEffects.hlsl", L"PS_Gamma", L"ps_6_5", psGamma);
    Compile(L"PostEffects.hlsl", L"PS_Vignette", L"ps_6_5", psVignette);

    Compile(L"PostEffects.hlsl", L"PS_Invert", L"ps_6_5", psInvert);
    Compile(L"PostEffects.hlsl", L"PS_Grayscale", L"ps_6_5", psGrayscale);
    Compile(L"PostEffects.hlsl", L"PS_Pixelate", L"ps_6_5", psPixelate);
    Compile(L"PostEffects.hlsl", L"PS_Posterize", L"ps_6_5", psPosterize);
    Compile(L"PostEffects.hlsl", L"PS_Saturation", L"ps_6_5", psSaturation);

    ComPtr<IDxcBlob> psPreview;
    Compile(L"Shaders.hlsl", L"PS_PreviewGBuffer", L"ps_6_5", psPreview);

    ComPtr<IDxcBlob> vsTerrain, psTerrain;
    Compile(L"Terrain.hlsl", L"VS_TerrainGBuffer", L"vs_6_5", vsTerrain);
    Compile(L"Terrain.hlsl", L"PS_TerrainGBuffer", L"ps_6_5", psTerrain);

    ComPtr<IDxcBlob> psTAA;
    Compile(L"TAA.hlsl", L"PS_TAA", L"ps_6_5", psTAA);
    ComPtr<IDxcBlob> psVelocity;
    Compile(L"Velocity.hlsl", L"PS_Velocity", L"ps_6_5", psVelocity);
    
    ComPtr<IDxcBlob> msGBuffer;
    if (m_framework->IsMeshShaderSupported())
    {
        Compile(L"Shaders.hlsl", L"MS_GBuffer", L"ms_6_5", msGBuffer);
    }

    D3D12_INPUT_ELEMENT_DESC inputLayout[] = 
    {
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

    if (m_framework->IsMeshShaderSupported())
    {
        CD3DX12_DESCRIPTOR_RANGE meshletRange;
        meshletRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 0, 2);

        CD3DX12_ROOT_PARAMETER msParams[8];
        msParams[0].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);
        msParams[1].InitAsConstantBufferView(1, 0, D3D12_SHADER_VISIBILITY_ALL);
        msParams[2].InitAsConstantBufferView(2, 0, D3D12_SHADER_VISIBILITY_ALL);
        msParams[3].InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_ALL);
        msParams[4].InitAsDescriptorTable(1, &samplerRange, D3D12_SHADER_VISIBILITY_ALL);
        msParams[5].InitAsConstantBufferView(4, 0, D3D12_SHADER_VISIBILITY_ALL);
        msParams[6].InitAsShaderResourceView(0, 1, D3D12_SHADER_VISIBILITY_ALL);
        msParams[7].InitAsDescriptorTable(1, &meshletRange, D3D12_SHADER_VISIBILITY_ALL);

        CD3DX12_ROOT_SIGNATURE_DESC msRSDesc;
        msRSDesc.Init(_countof(msParams), msParams, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE);

        ComPtr<ID3DBlob> msRSBlob;
        ComPtr<ID3DBlob> msRSErr;
        ThrowIfFailed(D3D12SerializeRootSignature(&msRSDesc, D3D_ROOT_SIGNATURE_VERSION_1, &msRSBlob, &msRSErr));
        ThrowIfFailed(m_framework->GetDevice()->CreateRootSignature(0, msRSBlob->GetBufferPointer(), msRSBlob->GetBufferSize(),
            IID_PPV_ARGS(&m_meshletRootSignature)));
    }

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
    opaqueDesc.SampleDesc.Quality = 0;
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

    if (m_framework->IsMeshShaderSupported())
    {
        MeshGBufferStream s = {};
        s.RootSig.data = m_meshletRootSignature.Get();
        s.MS.data = { msGBuffer->GetBufferPointer(), msGBuffer->GetBufferSize() };
        s.PS.data = { psG->GetBufferPointer(), psG->GetBufferSize() };
        s.Raster.data = geoDesc.RasterizerState;
        s.Blend.data = geoDesc.BlendState;
        s.Depth.data = geoDesc.DepthStencilState;

        D3D12_RT_FORMAT_ARRAY rts = {};
        rts.NumRenderTargets = 4;
        rts.RTFormats[0] = geoDesc.RTVFormats[0];
        rts.RTFormats[1] = geoDesc.RTVFormats[1];
        rts.RTFormats[2] = geoDesc.RTVFormats[2];
        rts.RTFormats[3] = geoDesc.RTVFormats[3];
        s.RTVs.data = rts;

        s.DSV.data = geoDesc.DSVFormat;
        s.Sample.data = geoDesc.SampleDesc;
        s.SampleMask.data = geoDesc.SampleMask;
        s.Topology.data = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

        D3D12_PIPELINE_STATE_STREAM_DESC streamDesc = { sizeof(MeshGBufferStream), &s };

        ComPtr<ID3D12Device2> dev2;
        ThrowIfFailed(m_framework->GetDevice()->QueryInterface(IID_PPV_ARGS(&dev2)));
        ThrowIfFailed(dev2->CreatePipelineState(&streamDesc, IID_PPV_ARGS(&m_meshletGBufferPSO)));
    }

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

     CD3DX12_DESCRIPTOR_RANGE defSrvRanges[5];
    defSrvRanges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 0);
    defSrvRanges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 4);
    defSrvRanges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 5);
    defSrvRanges[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 8);
    defSrvRanges[4].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 9);

    CD3DX12_DESCRIPTOR_RANGE defSampRange;
    defSampRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 2, 0);

    CD3DX12_ROOT_PARAMETER defParams[8];
    defParams[0].InitAsDescriptorTable(1, &defSrvRanges[0], D3D12_SHADER_VISIBILITY_PIXEL);
    defParams[1].InitAsConstantBufferView(1, 0, D3D12_SHADER_VISIBILITY_PIXEL);
    defParams[2].InitAsConstantBufferView(2, 0, D3D12_SHADER_VISIBILITY_PIXEL);
    defParams[3].InitAsDescriptorTable(1, &defSampRange, D3D12_SHADER_VISIBILITY_PIXEL);
    defParams[4].InitAsDescriptorTable(1, &defSrvRanges[1], D3D12_SHADER_VISIBILITY_PIXEL);
    defParams[5].InitAsDescriptorTable(1, &defSrvRanges[2], D3D12_SHADER_VISIBILITY_PIXEL);
    defParams[6].InitAsDescriptorTable(1, &defSrvRanges[3], D3D12_SHADER_VISIBILITY_PIXEL);
    defParams[7].InitAsDescriptorTable(1, &defSrvRanges[4], D3D12_SHADER_VISIBILITY_PIXEL);

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
    psoGP.DepthStencilState.DepthEnable = TRUE;
    psoGP.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    psoGP.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

    psoGP.InputLayout = { inputLayout, _countof(inputLayout) };
    psoGP.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoGP.NumRenderTargets = 3;
    psoGP.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoGP.RTVFormats[1] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    psoGP.RTVFormats[2] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoGP.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    psoGP.SampleDesc.Count = 1;

    ThrowIfFailed(m_framework->GetDevice()->CreateGraphicsPipelineState(
        &psoGP, IID_PPV_ARGS(&m_gbufferParticlesPSO)));

    ThrowIfFailed(m_framework->GetDevice()->CreateGraphicsPipelineState(
        &psoGP, IID_PPV_ARGS(&m_gbufferParticlesPSO)));

    CD3DX12_DESCRIPTOR_RANGE uavRange;
    uavRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 0, 0);

    CD3DX12_DESCRIPTOR_RANGE srvDepthRange;
    srvDepthRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2, 0);

    CD3DX12_ROOT_PARAMETER cParams[4];
    cParams[0].InitAsDescriptorTable(1, &uavRange, D3D12_SHADER_VISIBILITY_ALL);
    cParams[1].InitAsConstantBufferView(5, 0, D3D12_SHADER_VISIBILITY_ALL);
    cParams[2].InitAsDescriptorTable(1, &srvDepthRange, D3D12_SHADER_VISIBILITY_ALL);
    cParams[3].InitAsConstantBufferView(6, 0, D3D12_SHADER_VISIBILITY_ALL);

    CD3DX12_ROOT_SIGNATURE_DESC csDesc;
    csDesc.Init(_countof(cParams), cParams, 0, nullptr,
        D3D12_ROOT_SIGNATURE_FLAG_NONE);

    ComPtr<ID3DBlob> sigBlob, sigErr;
    ThrowIfFailed(D3D12SerializeRootSignature(&csDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &sigErr));
    ThrowIfFailed(m_framework->GetDevice()->CreateRootSignature(
        0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(),
        IID_PPV_ARGS(&m_particlesComputeRS)));

    D3D12_COMPUTE_PIPELINE_STATE_DESC csd = {};
    csd.pRootSignature = m_particlesComputeRS.Get();
    csd.CS = { csUpdate->GetBufferPointer(), csUpdate->GetBufferSize() };
    ThrowIfFailed(m_framework->GetDevice()->CreateComputePipelineState(&csd, IID_PPV_ARGS(&m_particlesUpdateCSO)));
    csd.CS = { csEmit->GetBufferPointer(), csEmit->GetBufferSize() };
    ThrowIfFailed(m_framework->GetDevice()->CreateComputePipelineState(&csd, IID_PPV_ARGS(&m_particlesEmitCSO)));

    srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0);

    CD3DX12_ROOT_PARAMETER params[2];
    params[0].InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_PIXEL);
    params[1].InitAsConstantBufferView(1, 0, D3D12_SHADER_VISIBILITY_PIXEL);

    D3D12_STATIC_SAMPLER_DESC staticSam{};
    staticSam.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    staticSam.AddressU = staticSam.AddressV = staticSam.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSam.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    staticSam.ShaderRegister = 0;

    CD3DX12_ROOT_SIGNATURE_DESC rsDesc;
    rsDesc.Init(_countof(params), params, 1, &staticSam,
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> sig, err;
    ThrowIfFailed(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err));
    ThrowIfFailed(m_framework->GetDevice()->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
        IID_PPV_ARGS(&m_postRootSig)));

    auto MakePostDesc = [&](IDxcBlob* ps)->D3D12_GRAPHICS_PIPELINE_STATE_DESC 
        {
            D3D12_GRAPHICS_PIPELINE_STATE_DESC d = {};
            d.InputLayout = { nullptr, 0 };
            d.pRootSignature = m_postRootSig.Get();
            d.VS = { vsQuad->GetBufferPointer(), vsQuad->GetBufferSize() };
            d.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
            d.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
            d.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
            d.DepthStencilState.DepthEnable = FALSE;
            d.SampleMask = UINT_MAX;
            d.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            d.NumRenderTargets = 1;
            d.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
            d.SampleDesc.Count = 1;
            return d;
        };

    auto dCopyHDR = MakePostDesc(psCopyHDRtoLDR.Get());
    auto dCopyLDR = MakePostDesc(psCopyLDR.Get());
    ThrowIfFailed(m_framework->GetDevice()->CreateGraphicsPipelineState(&dCopyHDR, IID_PPV_ARGS(&m_copyHDRtoLDRPSO)));
    ThrowIfFailed(m_framework->GetDevice()->CreateGraphicsPipelineState(&dCopyLDR, IID_PPV_ARGS(&m_copyLDRPSO)));

    auto dTone = MakePostDesc(psTonemap.Get());
    auto dGam = MakePostDesc(psGamma.Get());
    auto dVin = MakePostDesc(psVignette.Get());
    ThrowIfFailed(m_framework->GetDevice()->CreateGraphicsPipelineState(&dTone, IID_PPV_ARGS(&m_tonemapPSO)));
    ThrowIfFailed(m_framework->GetDevice()->CreateGraphicsPipelineState(&dGam, IID_PPV_ARGS(&m_gammaPSO)));
    ThrowIfFailed(m_framework->GetDevice()->CreateGraphicsPipelineState(&dVin, IID_PPV_ARGS(&m_vignettePSO)));

    auto dInv = MakePostDesc(psInvert.Get());
    auto dGry = MakePostDesc(psGrayscale.Get());
    auto dPix = MakePostDesc(psPixelate.Get());
    auto dPos = MakePostDesc(psPosterize.Get());
    auto dSat = MakePostDesc(psSaturation.Get());
    ThrowIfFailed(m_framework->GetDevice()->CreateGraphicsPipelineState(&dInv, IID_PPV_ARGS(&m_invertPSO)));
    ThrowIfFailed(m_framework->GetDevice()->CreateGraphicsPipelineState(&dGry, IID_PPV_ARGS(&m_grayscalePSO)));
    ThrowIfFailed(m_framework->GetDevice()->CreateGraphicsPipelineState(&dPix, IID_PPV_ARGS(&m_pixelatePSO)));
    ThrowIfFailed(m_framework->GetDevice()->CreateGraphicsPipelineState(&dPos, IID_PPV_ARGS(&m_posterizePSO)));
    ThrowIfFailed(m_framework->GetDevice()->CreateGraphicsPipelineState(&dSat, IID_PPV_ARGS(&m_saturationPSO)));

    D3D12_GRAPHICS_PIPELINE_STATE_DESC skyDesc = {};
    skyDesc.InputLayout = { nullptr, 0 };
    skyDesc.pRootSignature = m_deferredRootSig.Get();
    skyDesc.VS = { vsQuad->GetBufferPointer(), vsQuad->GetBufferSize() };
    skyDesc.PS = { psSkybox->GetBufferPointer(), psSkybox->GetBufferSize() };
    skyDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);

    blendReplace = {};
    rt0 = blendReplace.RenderTarget[0];
    rt0.BlendEnable = TRUE;
    rt0.SrcBlend = D3D12_BLEND_ONE;
    rt0.DestBlend = D3D12_BLEND_ZERO;
    rt0.BlendOp = D3D12_BLEND_OP_ADD;
    rt0.SrcBlendAlpha = D3D12_BLEND_ONE;
    rt0.DestBlendAlpha = D3D12_BLEND_ZERO;
    rt0.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    rt0.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    skyDesc.BlendState = blendReplace;

    skyDesc.DepthStencilState.DepthEnable = FALSE;
    skyDesc.SampleMask = UINT_MAX;
    skyDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    skyDesc.NumRenderTargets = 1;
    skyDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    skyDesc.SampleDesc.Count = 1;

    ThrowIfFailed(m_framework->GetDevice()->CreateGraphicsPipelineState(
        &skyDesc, IID_PPV_ARGS(&m_skyPSO)
    ));

    srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 0);

    samplerRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 1, 0);

    CD3DX12_ROOT_PARAMETER previewParams[2];
    previewParams[0].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_PIXEL);
    previewParams[1].InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_PIXEL);

    CD3DX12_STATIC_SAMPLER_DESC staticSampler(
        0,
        D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP
    );

    CD3DX12_ROOT_SIGNATURE_DESC previewRootSigDesc;
    previewRootSigDesc.Init(
        _countof(previewParams),
        previewParams,
        1,
        &staticSampler,
        D3D12_ROOT_SIGNATURE_FLAG_NONE
    );

    ComPtr<ID3DBlob> serializedPreviewRS, errorBlobPreviewRS;
    ThrowIfFailed(D3D12SerializeRootSignature(
        &previewRootSigDesc,
        D3D_ROOT_SIGNATURE_VERSION_1,
        &serializedPreviewRS,
        &errorBlobPreviewRS
    ));
    if (errorBlobPreviewRS)
    {
        OutputDebugStringA((char*)errorBlobPreviewRS->GetBufferPointer());
        throw std::runtime_error("Failed to serialize preview root signature");
    }
    ThrowIfFailed(m_framework->GetDevice()->CreateRootSignature(
        0,
        serializedPreviewRS->GetBufferPointer(),
        serializedPreviewRS->GetBufferSize(),
        IID_PPV_ARGS(&m_previewRootSig)
    ));

    D3D12_GRAPHICS_PIPELINE_STATE_DESC previewDesc = {};
    previewDesc.InputLayout = { nullptr, 0 };
    previewDesc.pRootSignature = m_previewRootSig.Get();
    previewDesc.VS = { vsQuad->GetBufferPointer(), vsQuad->GetBufferSize() };
    previewDesc.PS = { psPreview->GetBufferPointer(), psPreview->GetBufferSize() };
    previewDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    previewDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    previewDesc.DepthStencilState.DepthEnable = FALSE;
    previewDesc.SampleMask = UINT_MAX;
    previewDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    previewDesc.NumRenderTargets = 1;
    previewDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    previewDesc.SampleDesc.Count = 1;
    ThrowIfFailed(m_framework->GetDevice()->CreateGraphicsPipelineState(&previewDesc, IID_PPV_ARGS(&m_previewPSO)));

    D3D12_GRAPHICS_PIPELINE_STATE_DESC terrainDesc = geoDesc;
    terrainDesc.VS = { vsTerrain->GetBufferPointer(), vsTerrain->GetBufferSize() };
    terrainDesc.PS = { psTerrain->GetBufferPointer(), psTerrain->GetBufferSize() };
    terrainDesc.RasterizerState.FrontCounterClockwise = TRUE;
    terrainDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    //terrainDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
    ThrowIfFailed(m_framework->GetDevice()->CreateGraphicsPipelineState(
        &terrainDesc, IID_PPV_ARGS(&m_terrainGBufferPSO)
    ));

    CD3DX12_DESCRIPTOR_RANGE rangeCurr;
    rangeCurr.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    CD3DX12_DESCRIPTOR_RANGE rangeHist;
    rangeHist.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);
    CD3DX12_DESCRIPTOR_RANGE rangeZPrev;
    rangeZPrev.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2);
    CD3DX12_DESCRIPTOR_RANGE rangeZCurr;
    rangeZCurr.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3);
    CD3DX12_DESCRIPTOR_RANGE rangeVel;
    rangeVel.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 4);

    CD3DX12_ROOT_PARAMETER taaParams[6];
    taaParams[0].InitAsDescriptorTable(1, &rangeCurr, D3D12_SHADER_VISIBILITY_PIXEL);
    taaParams[1].InitAsDescriptorTable(1, &rangeHist, D3D12_SHADER_VISIBILITY_PIXEL);
    taaParams[2].InitAsDescriptorTable(1, &rangeZPrev, D3D12_SHADER_VISIBILITY_PIXEL);
    taaParams[3].InitAsDescriptorTable(1, &rangeZCurr, D3D12_SHADER_VISIBILITY_PIXEL);
    taaParams[4].InitAsDescriptorTable(1, &rangeVel, D3D12_SHADER_VISIBILITY_PIXEL);
    taaParams[5].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_PIXEL);

    D3D12_STATIC_SAMPLER_DESC taaStaticSam{};
    taaStaticSam.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    taaStaticSam.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    taaStaticSam.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    taaStaticSam.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    taaStaticSam.MipLODBias = 0.0f;
    taaStaticSam.MaxAnisotropy = 1;
    taaStaticSam.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    taaStaticSam.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    taaStaticSam.MinLOD = 0.0f;
    taaStaticSam.MaxLOD = D3D12_FLOAT32_MAX;
    taaStaticSam.ShaderRegister = 0;
    taaStaticSam.RegisterSpace = 0;
    taaStaticSam.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    CD3DX12_ROOT_SIGNATURE_DESC taaRsDesc;
    taaRsDesc.Init(
        _countof(taaParams), taaParams,
        1, &taaStaticSam,
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
    );

    Microsoft::WRL::ComPtr<ID3DBlob> taaSig, taaErr;
    HRESULT hrRS = D3D12SerializeRootSignature(
        &taaRsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &taaSig, &taaErr);

    if (FAILED(hrRS)) {
        if (taaErr) OutputDebugStringA((const char*)taaErr->GetBufferPointer());
        throw std::runtime_error("Failed to serialize TAA root signature");
    }

    ThrowIfFailed(m_framework->GetDevice()->CreateRootSignature(
        0, taaSig->GetBufferPointer(), taaSig->GetBufferSize(),
        IID_PPV_ARGS(&m_taaRootSig)));

    D3D12_GRAPHICS_PIPELINE_STATE_DESC taaDesc = {};
    taaDesc.InputLayout = { nullptr, 0 };
    taaDesc.pRootSignature = m_taaRootSig.Get();
    taaDesc.VS = { vsQuad->GetBufferPointer(), vsQuad->GetBufferSize() };
    taaDesc.PS = { psTAA->GetBufferPointer(), psTAA->GetBufferSize() };
    taaDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    taaDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    taaDesc.DepthStencilState.DepthEnable = FALSE;
    taaDesc.SampleMask = UINT_MAX;
    taaDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    taaDesc.NumRenderTargets = 1;
    taaDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    taaDesc.SampleDesc.Count = 1;

    ThrowIfFailed(m_framework->GetDevice()->CreateGraphicsPipelineState(
        &taaDesc, IID_PPV_ARGS(&m_taaPSO)));

    D3D12_GRAPHICS_PIPELINE_STATE_DESC dVelocity = {};
    dVelocity.InputLayout = { nullptr, 0 };
    dVelocity.pRootSignature = m_postRootSig.Get();
    dVelocity.VS = { vsQuad->GetBufferPointer(), vsQuad->GetBufferSize() };
    dVelocity.PS = { psVelocity->GetBufferPointer(), psVelocity->GetBufferSize() };
    dVelocity.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    dVelocity.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    dVelocity.DepthStencilState.DepthEnable = FALSE;
    dVelocity.SampleMask = UINT_MAX;
    dVelocity.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    dVelocity.NumRenderTargets = 1;
    dVelocity.RTVFormats[0] = DXGI_FORMAT_R16G16_FLOAT;
    dVelocity.SampleDesc.Count = 1;

    ThrowIfFailed(m_framework->GetDevice()->CreateGraphicsPipelineState(
        &dVelocity, IID_PPV_ARGS(&m_velocityPSO)));
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

    LPCWSTR args[] = 
    {
        L"-HV",
        L"2021",
    };

    compiler->Compile(
        source.Get(),
        file,
        entry, target,
        args, _countof(args),
        nullptr, 0, nullptr,
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