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

    ComPtr<IDxcBlob> psCopyHDRtoLDR, psCopyLDR, psTonemap, psGamma, psVignette;
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

    ComPtr<IDxcBlob> psTAA, psVelocity;
    Compile(L"TAA.hlsl", L"PS_TAA", L"ps_6_5", psTAA);
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
        { "TANGENT",  0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "HAND",     0, DXGI_FORMAT_R32_FLOAT,       0, 44, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    // Geometry RS
    {
        CD3DX12_DESCRIPTOR_RANGE samplerRange(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 1, 0);
        CD3DX12_DESCRIPTOR_RANGE srvRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
            m_framework->GetSrvHeap()->GetDesc().NumDescriptors, 0);

        CD3DX12_ROOT_PARAMETER params[7] = {};
        params[0].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);
        params[1].InitAsConstantBufferView(1, 0, D3D12_SHADER_VISIBILITY_ALL);
        params[2].InitAsConstantBufferView(3, 0, D3D12_SHADER_VISIBILITY_ALL);
        params[3].InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_ALL);
        params[4].InitAsDescriptorTable(1, &samplerRange, D3D12_SHADER_VISIBILITY_ALL);
        params[5].InitAsConstantBufferView(4, 0, D3D12_SHADER_VISIBILITY_ALL);
        params[6].InitAsShaderResourceView(0, 1, D3D12_SHADER_VISIBILITY_ALL);

        CD3DX12_ROOT_SIGNATURE_DESC desc(_countof(params), params, 0, nullptr,
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        ComPtr<ID3DBlob> sig, err;
        ThrowIfFailed(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err));
        ThrowIfFailed(m_framework->GetDevice()->CreateRootSignature(0, sig->GetBufferPointer(),
            sig->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)));
    }

    // Mesh Shader RS
    if (m_framework->IsMeshShaderSupported())
    {
        CD3DX12_DESCRIPTOR_RANGE srvRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
            m_framework->GetSrvHeap()->GetDesc().NumDescriptors, 0);
        CD3DX12_DESCRIPTOR_RANGE meshletRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 0, 2);

        CD3DX12_DESCRIPTOR_RANGE samplerRange(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 1, 0);

        CD3DX12_ROOT_PARAMETER params[8] = {};
        params[0].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);
        params[1].InitAsConstantBufferView(1, 0, D3D12_SHADER_VISIBILITY_ALL);
        params[2].InitAsConstantBufferView(2, 0, D3D12_SHADER_VISIBILITY_ALL);
        params[3].InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_ALL);
        params[4].InitAsDescriptorTable(1, &samplerRange, D3D12_SHADER_VISIBILITY_ALL);
        params[5].InitAsConstantBufferView(4, 0, D3D12_SHADER_VISIBILITY_ALL);
        params[6].InitAsShaderResourceView(0, 1, D3D12_SHADER_VISIBILITY_ALL);
        params[7].InitAsDescriptorTable(1, &meshletRange, D3D12_SHADER_VISIBILITY_ALL);

        CD3DX12_ROOT_SIGNATURE_DESC desc(_countof(params), params, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE);

        ComPtr<ID3DBlob> sig, err;
        ThrowIfFailed(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err));
        ThrowIfFailed(m_framework->GetDevice()->CreateRootSignature(0, sig->GetBufferPointer(),
            sig->GetBufferSize(), IID_PPV_ARGS(&m_meshletRootSignature)));
    }

    // Deferred Lighting RS
    {
        CD3DX12_DESCRIPTOR_RANGE srv[6];
        srv[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 0); 
        srv[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 4); 
        srv[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 5); 
        srv[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 8); 
        srv[4].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 9); 

        srv[5].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 30);

        CD3DX12_DESCRIPTOR_RANGE sampler(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 2, 0); 

        CD3DX12_ROOT_PARAMETER params[10] = {};
        params[0].InitAsDescriptorTable(1, &srv[0], D3D12_SHADER_VISIBILITY_PIXEL);
        params[1].InitAsConstantBufferView(1, 0, D3D12_SHADER_VISIBILITY_PIXEL);
        params[2].InitAsConstantBufferView(2, 0, D3D12_SHADER_VISIBILITY_PIXEL);
        params[3].InitAsDescriptorTable(1, &sampler, D3D12_SHADER_VISIBILITY_PIXEL);
        params[4].InitAsDescriptorTable(1, &srv[1], D3D12_SHADER_VISIBILITY_PIXEL);
        params[5].InitAsDescriptorTable(1, &srv[2], D3D12_SHADER_VISIBILITY_PIXEL);
        params[6].InitAsDescriptorTable(1, &srv[3], D3D12_SHADER_VISIBILITY_PIXEL);
        params[7].InitAsDescriptorTable(1, &srv[4], D3D12_SHADER_VISIBILITY_PIXEL);

        params[8].InitAsConstantBufferView(6, 0, D3D12_SHADER_VISIBILITY_PIXEL);

        params[9].InitAsDescriptorTable(1, &srv[5], D3D12_SHADER_VISIBILITY_PIXEL);

        CD3DX12_ROOT_SIGNATURE_DESC desc(_countof(params), params, 0, nullptr,
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        ComPtr<ID3DBlob> sig, err;
        ThrowIfFailed(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err));
        ThrowIfFailed(m_framework->GetDevice()->CreateRootSignature(0, sig->GetBufferPointer(),
            sig->GetBufferSize(), IID_PPV_ARGS(&m_deferredRootSig)));
    }

    // Particles RS
    {
        CD3DX12_DESCRIPTOR_RANGE uav(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 0);
        CD3DX12_DESCRIPTOR_RANGE srv(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2);

        CD3DX12_ROOT_PARAMETER params[4] = {};
        params[0].InitAsDescriptorTable(1, &uav, D3D12_SHADER_VISIBILITY_ALL);
        params[1].InitAsConstantBufferView(5, 0, D3D12_SHADER_VISIBILITY_ALL);
        params[2].InitAsDescriptorTable(1, &srv, D3D12_SHADER_VISIBILITY_ALL);
        params[3].InitAsConstantBufferView(6, 0, D3D12_SHADER_VISIBILITY_ALL);

        CD3DX12_ROOT_SIGNATURE_DESC desc(_countof(params), params, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE);

        ComPtr<ID3DBlob> sig, err;
        ThrowIfFailed(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err));
        ThrowIfFailed(m_framework->GetDevice()->CreateRootSignature(0, sig->GetBufferPointer(),
            sig->GetBufferSize(), IID_PPV_ARGS(&m_particlesComputeRS)));
    }

    // Post RS
    {
        CD3DX12_DESCRIPTOR_RANGE srv(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
        CD3DX12_ROOT_PARAMETER params[2] = {};
        params[0].InitAsDescriptorTable(1, &srv, D3D12_SHADER_VISIBILITY_PIXEL);
        params[1].InitAsConstantBufferView(1, 0, D3D12_SHADER_VISIBILITY_PIXEL);

        D3D12_STATIC_SAMPLER_DESC sampler = CD3DX12_STATIC_SAMPLER_DESC(
            0, D3D12_FILTER_MIN_MAG_MIP_LINEAR,
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

        CD3DX12_ROOT_SIGNATURE_DESC desc(_countof(params), params, 1, &sampler,
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        ComPtr<ID3DBlob> sig, err;
        ThrowIfFailed(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err));
        ThrowIfFailed(m_framework->GetDevice()->CreateRootSignature(0, sig->GetBufferPointer(),
            sig->GetBufferSize(), IID_PPV_ARGS(&m_postRootSig)));
    }

    // Preview RS
    {
        CD3DX12_DESCRIPTOR_RANGE srv(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 0);

        CD3DX12_ROOT_PARAMETER params[2] = {};
        params[0].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_PIXEL);
        params[1].InitAsDescriptorTable(1, &srv, D3D12_SHADER_VISIBILITY_PIXEL);

        D3D12_STATIC_SAMPLER_DESC sampler = CD3DX12_STATIC_SAMPLER_DESC(0,
            D3D12_FILTER_MIN_MAG_MIP_LINEAR,
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

        CD3DX12_ROOT_SIGNATURE_DESC desc(_countof(params), params, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_NONE);

        ComPtr<ID3DBlob> sig, err;
        ThrowIfFailed(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err));
        ThrowIfFailed(m_framework->GetDevice()->CreateRootSignature(0, sig->GetBufferPointer(),
            sig->GetBufferSize(), IID_PPV_ARGS(&m_previewRootSig)));
    }

    // TAA RS
    {
        CD3DX12_DESCRIPTOR_RANGE ranges[5];
        ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
        ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);
        ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2);
        ranges[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3);
        ranges[4].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 4);

        CD3DX12_ROOT_PARAMETER params[6] = {};
        for (int i = 0; i < 5; ++i)
            params[i].InitAsDescriptorTable(1, &ranges[i], D3D12_SHADER_VISIBILITY_PIXEL);
        params[5].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_PIXEL);

        D3D12_STATIC_SAMPLER_DESC sampler = {};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.ShaderRegister = 0;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        CD3DX12_ROOT_SIGNATURE_DESC desc(_countof(params), params, 1, &sampler,
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        ComPtr<ID3DBlob> sig, err;
        ThrowIfFailed(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err));
        ThrowIfFailed(m_framework->GetDevice()->CreateRootSignature(0, sig->GetBufferPointer(),
            sig->GetBufferSize(), IID_PPV_ARGS(&m_taaRootSig)));
    }

    // Opaque 
    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
        desc.InputLayout = { inputLayout, _countof(inputLayout) };
        desc.pRootSignature = m_rootSignature.Get();
        desc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
        desc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
        desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        desc.SampleMask = UINT_MAX;
        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        desc.NumRenderTargets = 1;
        desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        ThrowIfFailed(m_framework->GetDevice()->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&m_opaquePSO)));
    }

    // Transparent 
    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
        desc.InputLayout = { inputLayout, _countof(inputLayout) };
        desc.pRootSignature = m_rootSignature.Get();
        desc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
        desc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
        desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        desc.BlendState.RenderTarget[0].BlendEnable = TRUE;
        desc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        desc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        desc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        desc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        desc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
        desc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        desc.SampleMask = UINT_MAX;
        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        desc.NumRenderTargets = 1;
        desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        ThrowIfFailed(m_framework->GetDevice()->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&m_transparentPSO)));
    }

    // G-Buffer
    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
        desc.InputLayout = { inputLayout, _countof(inputLayout) };
        desc.pRootSignature = m_rootSignature.Get();
        desc.VS = { vsG->GetBufferPointer(), vsG->GetBufferSize() };
        desc.PS = { psG->GetBufferPointer(), psG->GetBufferSize() };
        desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        desc.SampleMask = UINT_MAX;
        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        desc.NumRenderTargets = 4;
        desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.RTVFormats[1] = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.RTVFormats[2] = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.RTVFormats[3] = DXGI_FORMAT_R32G32B32A32_FLOAT;
        desc.SampleDesc.Count = 1;
        ThrowIfFailed(m_framework->GetDevice()->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&m_gBufferPSO)));
    }

    // Mesh Shader
    if (m_framework->IsMeshShaderSupported())
    {
        struct alignas(void*) MeshGBufferStream
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
        } stream = {};

        stream.RootSig.data = m_meshletRootSignature.Get();
        stream.MS.data = { msGBuffer->GetBufferPointer(), msGBuffer->GetBufferSize() };
        stream.PS.data = { psG->GetBufferPointer(), psG->GetBufferSize() };
        stream.Raster.data = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        stream.Blend.data = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        stream.Depth.data = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        D3D12_RT_FORMAT_ARRAY rts = {};
        rts.NumRenderTargets = 4;
        rts.RTFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        rts.RTFormats[1] = DXGI_FORMAT_R16G16B16A16_FLOAT;
        rts.RTFormats[2] = DXGI_FORMAT_R8G8B8A8_UNORM;
        rts.RTFormats[3] = DXGI_FORMAT_R32G32B32A32_FLOAT;
        stream.RTVs.data = rts;
        stream.DSV.data = DXGI_FORMAT_D32_FLOAT;
        stream.Sample.data = { 1, 0 };
        stream.SampleMask.data = UINT_MAX;
        stream.Topology.data = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

        D3D12_PIPELINE_STATE_STREAM_DESC streamDesc = { sizeof(stream), &stream };
        ComPtr<ID3D12Device2> dev2;
        m_framework->GetDevice()->QueryInterface(IID_PPV_ARGS(&dev2));
        ThrowIfFailed(dev2->CreatePipelineState(&streamDesc, IID_PPV_ARGS(&m_meshletGBufferPSO)));
    }

    // Tessellation
    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
        desc.InputLayout = { inputLayout, _countof(inputLayout) };
        desc.pRootSignature = m_rootSignature.Get();
        desc.VS = { vsTessBlob->GetBufferPointer(), vsTessBlob->GetBufferSize() };
        desc.HS = { hsTessBlob->GetBufferPointer(), hsTessBlob->GetBufferSize() };
        desc.DS = { dsTessBlob->GetBufferPointer(), dsTessBlob->GetBufferSize() };
        desc.PS = { psG->GetBufferPointer(), psG->GetBufferSize() };
        desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        desc.SampleMask = UINT_MAX;
        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
        desc.NumRenderTargets = 4;
        desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.RTVFormats[1] = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.RTVFormats[2] = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.RTVFormats[3] = DXGI_FORMAT_R32G32B32A32_FLOAT;
        desc.SampleDesc.Count = 1;
        ThrowIfFailed(m_framework->GetDevice()->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&m_gBufferTessellationPSO)));
    }

    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
        desc.InputLayout = { inputLayout, _countof(inputLayout) };
        desc.pRootSignature = m_rootSignature.Get();
        desc.VS = { vsTessBlob->GetBufferPointer(), vsTessBlob->GetBufferSize() };
        desc.HS = { hsTessBlob->GetBufferPointer(), hsTessBlob->GetBufferSize() };
        desc.DS = { dsTessBlob->GetBufferPointer(), dsTessBlob->GetBufferSize() };
        desc.PS = { psG->GetBufferPointer(), psG->GetBufferSize() };
        desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        desc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
        desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        desc.SampleMask = UINT_MAX;
        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
        desc.NumRenderTargets = 4;
        desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.RTVFormats[1] = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.RTVFormats[2] = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.RTVFormats[3] = DXGI_FORMAT_R32G32B32A32_FLOAT;
        desc.SampleDesc.Count = 1;
        ThrowIfFailed(m_framework->GetDevice()->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&m_gBufferTessellationWireframePSO)));
    }

    // Shadow
    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
        desc.InputLayout = { inputLayout, _countof(inputLayout) };
        desc.pRootSignature = m_rootSignature.Get();
        desc.VS = { vsShadow->GetBufferPointer(), vsShadow->GetBufferSize() };
        desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        desc.RasterizerState.DepthBias = 64;
        desc.RasterizerState.SlopeScaledDepthBias = 1.5f;
        desc.RasterizerState.DepthBiasClamp = 0.0f;
        desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        desc.SampleMask = UINT_MAX;
        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        desc.NumRenderTargets = 0;
        desc.SampleDesc.Count = 1;
        ThrowIfFailed(m_framework->GetDevice()->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&m_shadowPSO)));
    }

    // Particles
    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
        desc.InputLayout = { inputLayout, _countof(inputLayout) };
        desc.pRootSignature = m_rootSignature.Get();
        desc.VS = { vsGPart->GetBufferPointer(), vsGPart->GetBufferSize() };
        desc.PS = { psGPart->GetBufferPointer(), psGPart->GetBufferSize() };
        desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        desc.SampleMask = UINT_MAX;
        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        desc.NumRenderTargets = 3;
        desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.RTVFormats[1] = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.RTVFormats[2] = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        ThrowIfFailed(m_framework->GetDevice()->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&m_gbufferParticlesPSO)));
    }

    {
        D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature = m_particlesComputeRS.Get();
        desc.CS = { csUpdate->GetBufferPointer(), csUpdate->GetBufferSize() };
        ThrowIfFailed(m_framework->GetDevice()->CreateComputePipelineState(&desc, IID_PPV_ARGS(&m_particlesUpdateCSO)));

        desc.CS = { csEmit->GetBufferPointer(), csEmit->GetBufferSize() };
        ThrowIfFailed(m_framework->GetDevice()->CreateComputePipelineState(&desc, IID_PPV_ARGS(&m_particlesEmitCSO)));
    }

    // Deferred
    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature = m_deferredRootSig.Get();
        desc.VS = { vsQuad->GetBufferPointer(), vsQuad->GetBufferSize() };
        desc.PS = { psLight->GetBufferPointer(), psLight->GetBufferSize() };
        desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);

        desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        auto& rt = desc.BlendState.RenderTarget[0];
        rt.BlendEnable = TRUE;
        rt.SrcBlend = D3D12_BLEND_ONE;
        rt.DestBlend = D3D12_BLEND_ONE;
        rt.BlendOp = D3D12_BLEND_OP_ADD;
        rt.SrcBlendAlpha = D3D12_BLEND_ONE;
        rt.DestBlendAlpha = D3D12_BLEND_ONE;
        rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

        desc.DepthStencilState.DepthEnable = FALSE;
        desc.SampleMask = UINT_MAX;
        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        desc.NumRenderTargets = 1;
        desc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.SampleDesc.Count = 1;

        ThrowIfFailed(m_framework->GetDevice()->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&m_deferredPSO)));
    }

    // Ambient
    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature = m_deferredRootSig.Get();
        desc.VS = { vsQuad->GetBufferPointer(), vsQuad->GetBufferSize() };
        desc.PS = { psAmbientBlob->GetBufferPointer(), psAmbientBlob->GetBufferSize() };
        desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);

        desc.BlendState.AlphaToCoverageEnable = FALSE;
        desc.BlendState.IndependentBlendEnable = FALSE;

        auto& rt = desc.BlendState.RenderTarget[0];
        rt.BlendEnable = TRUE;
        rt.SrcBlend = D3D12_BLEND_ONE;
        rt.DestBlend = D3D12_BLEND_ZERO;
        rt.BlendOp = D3D12_BLEND_OP_ADD;
        rt.SrcBlendAlpha = D3D12_BLEND_ONE;
        rt.DestBlendAlpha = D3D12_BLEND_ZERO;
        rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        rt.LogicOpEnable = FALSE;
        rt.LogicOp = D3D12_LOGIC_OP_NOOP;
        rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

        desc.DepthStencilState.DepthEnable = FALSE;
        desc.SampleMask = UINT_MAX;
        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        desc.NumRenderTargets = 1;
        desc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.SampleDesc.Count = 1;

        ThrowIfFailed(m_framework->GetDevice()->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&m_ambientPSO)));
    }

    // Sky 
    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature = m_deferredRootSig.Get();
        desc.VS = { vsQuad->GetBufferPointer(), vsQuad->GetBufferSize() };
        desc.PS = { psSkybox->GetBufferPointer(), psSkybox->GetBufferSize() };
        desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);

        desc.BlendState.AlphaToCoverageEnable = FALSE;
        desc.BlendState.IndependentBlendEnable = FALSE;

        auto& rt = desc.BlendState.RenderTarget[0];
        rt.BlendEnable = TRUE;
        rt.SrcBlend = D3D12_BLEND_ONE;
        rt.DestBlend = D3D12_BLEND_ZERO;
        rt.BlendOp = D3D12_BLEND_OP_ADD;
        rt.SrcBlendAlpha = D3D12_BLEND_ONE; 
        rt.DestBlendAlpha = D3D12_BLEND_ZERO;   
        rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        rt.LogicOpEnable = FALSE;
        rt.LogicOp = D3D12_LOGIC_OP_NOOP;
        rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

        desc.DepthStencilState.DepthEnable = FALSE;
        desc.SampleMask = UINT_MAX;
        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        desc.NumRenderTargets = 1;
        desc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.SampleDesc.Count = 1;

        ThrowIfFailed(m_framework->GetDevice()->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&m_skyPSO)));
    }

    // Post
    auto CreatePostPSO = [&](ComPtr<IDxcBlob>& psBlob, ComPtr<ID3D12PipelineState>& pso)
        {
            D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
            desc.pRootSignature = m_postRootSig.Get();
            desc.VS = { vsQuad->GetBufferPointer(), vsQuad->GetBufferSize() };
            desc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
            desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
            desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
            desc.DepthStencilState.DepthEnable = FALSE;
            desc.SampleMask = UINT_MAX;
            desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            desc.NumRenderTargets = 1;
            desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.SampleDesc.Count = 1;
            ThrowIfFailed(m_framework->GetDevice()->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pso)));
        };

    CreatePostPSO(psCopyHDRtoLDR, m_copyHDRtoLDRPSO);
    CreatePostPSO(psCopyLDR, m_copyLDRPSO);
    CreatePostPSO(psTonemap, m_tonemapPSO);
    CreatePostPSO(psGamma, m_gammaPSO);
    CreatePostPSO(psVignette, m_vignettePSO);
    CreatePostPSO(psInvert, m_invertPSO);
    CreatePostPSO(psGrayscale, m_grayscalePSO);
    CreatePostPSO(psPixelate, m_pixelatePSO);
    CreatePostPSO(psPosterize, m_posterizePSO);
    CreatePostPSO(psSaturation, m_saturationPSO);

    // Preview
    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature = m_previewRootSig.Get();
        desc.VS = { vsQuad->GetBufferPointer(), vsQuad->GetBufferSize() };
        desc.PS = { psPreview->GetBufferPointer(), psPreview->GetBufferSize() };
        desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        desc.DepthStencilState.DepthEnable = FALSE;
        desc.SampleMask = UINT_MAX;
        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        desc.NumRenderTargets = 1;
        desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        ThrowIfFailed(m_framework->GetDevice()->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&m_previewPSO)));
    }

    // Terrain
    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
        desc.InputLayout = { inputLayout, _countof(inputLayout) };
        desc.pRootSignature = m_rootSignature.Get();
        desc.VS = { vsTerrain->GetBufferPointer(), vsTerrain->GetBufferSize() };
        desc.PS = { psTerrain->GetBufferPointer(), psTerrain->GetBufferSize() };
        desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        desc.RasterizerState.FrontCounterClockwise = TRUE;
        desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        desc.SampleMask = UINT_MAX;
        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        desc.NumRenderTargets = 4;
        desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.RTVFormats[1] = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.RTVFormats[2] = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.RTVFormats[3] = DXGI_FORMAT_R32G32B32A32_FLOAT;
        desc.SampleDesc.Count = 1;
        ThrowIfFailed(m_framework->GetDevice()->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&m_terrainGBufferPSO)));
    }

    // TAA
    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature = m_taaRootSig.Get();
        desc.VS = { vsQuad->GetBufferPointer(), vsQuad->GetBufferSize() };
        desc.PS = { psTAA->GetBufferPointer(), psTAA->GetBufferSize() };
        desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        desc.DepthStencilState.DepthEnable = FALSE;
        desc.SampleMask = UINT_MAX;
        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        desc.NumRenderTargets = 1;
        desc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.SampleDesc.Count = 1;
        ThrowIfFailed(m_framework->GetDevice()->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&m_taaPSO)));
    }

    // Velocity
    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature = m_postRootSig.Get();
        desc.VS = { vsQuad->GetBufferPointer(), vsQuad->GetBufferSize() };
        desc.PS = { psVelocity->GetBufferPointer(), psVelocity->GetBufferSize() };
        desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        desc.DepthStencilState.DepthEnable = FALSE;
        desc.SampleMask = UINT_MAX;
        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        desc.NumRenderTargets = 1;
        desc.RTVFormats[0] = DXGI_FORMAT_R16G16_FLOAT;
        desc.SampleDesc.Count = 1;
        ThrowIfFailed(m_framework->GetDevice()->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&m_velocityPSO)));
    }
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