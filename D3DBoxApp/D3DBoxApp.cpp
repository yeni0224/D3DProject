
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <Windowsx.h>

#include <algorithm>
#include <vector>
#include <wrl.h>
#include <d3d11.h>
#include <dxgi.h>
#include <d3dcompiler.h>
#include <SimpleMath.h>
#include <WICTextureLoader.h>
#include <DDSTextureLoader.h>


#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
//#pragma comment(lib, "DirectXTK.lib")

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::SimpleMath;

struct VertexPC
{
    Vector3 pos;
    Vector3 col;
};

struct VertexPT
{
    Vector3 pos;
    Vector2 uv;
};

struct VertexP  // Skybox용
{
    Vector3 pos;
};

struct CBVS
{
    Matrix gWorld;
    Matrix gViewProj;
};

struct App
{
    // DX Core
    ComPtr<IDXGISwapChain>           m_SwapChain;
    ComPtr<ID3D11Device>             m_Device;
    ComPtr<ID3D11DeviceContext>      m_Context;
    ComPtr<ID3D11RenderTargetView>   m_RTV;
    ComPtr<ID3D11Texture2D>          m_DSVTex;
    ComPtr<ID3D11DepthStencilView>   m_DSV;

    // Shaders / Pipeline
    ComPtr<ID3D11VertexShader>       m_VSColor; // Grid: Color shader
    ComPtr<ID3D11PixelShader>        m_PSColor;
    
    ComPtr<ID3D11VertexShader>       m_VSTex;
    ComPtr<ID3D11PixelShader>        m_PSTex;

    ComPtr<ID3D11InputLayout>        m_InputLayoutColor;
    ComPtr<ID3D11InputLayout>        m_InputLayoutTex;
    
    ComPtr<ID3D11Buffer>             m_CBVS;

    // Skybox
    ComPtr<ID3D11VertexShader>       m_VSSky;
    ComPtr<ID3D11PixelShader>        m_PSSky;
    ComPtr<ID3D11InputLayout>        m_InputLayoutSky;
    ComPtr<ID3D11Buffer>             m_SkyVB;
    ComPtr<ID3D11Buffer>             m_SkyIB;
    ComPtr<ID3D11ShaderResourceView> m_SkySRV;
    ComPtr<ID3D11SamplerState>       m_SkySampler;
    ComPtr<ID3D11DepthStencilState>  m_SkyDSS;
    ComPtr<ID3D11RasterizerState>    m_SkyRS;
    UINT                             m_SkyIndexCount = 0;

    // Texture (Box 전용)
    ComPtr<ID3D11ShaderResourceView> m_TexSRV;
    ComPtr<ID3D11SamplerState>       m_Sampler;
    ComPtr<ID3D11ShaderResourceView> m_TexSRVGrass;
    ComPtr<ID3D11SamplerState>       m_SamplerGrass;

    // Geometry
    ComPtr<ID3D11Buffer>             m_GridVB;
    UINT                             m_GridVertexCount = 0;

    ComPtr<ID3D11Buffer>             m_BoxVB;
    ComPtr<ID3D11Buffer>             m_BoxIB;
    UINT                             m_BoxIndexCount = 0;

    // Transform
    Matrix                           m_BoxWorld = Matrix::CreateTranslation(0, -1000, 0);

    // Camera
    Matrix                           m_View;
    Matrix                           m_Proj;
    /*float                            m_CamYaw = XMConvertToRadians(-135.0f);
    float                            m_CamPitch = XMConvertToRadians(30.0f);*/
    //
    // float                            m_CamRadius = 18.0f;
    float                            m_CamYaw = XMConvertToRadians(-0.0f);
    float                            m_CamPitch = XMConvertToRadians(0.0f);

    float                            m_CamRadius = 0.0f;

    POINT                            m_LastMouse{ 0,0 };
    bool                             m_RBtnDown = false;

    // Window / Grid
    HWND                             m_hWnd = nullptr;
    UINT                             m_Width = 1280;
    UINT                             m_Height = 720;
    float                            m_CellSize = 1.0f;
    int                              m_HalfCells = 20;

    bool Init(HWND hWnd)
    {
        m_hWnd = hWnd;

        RECT rc;
        GetClientRect(hWnd, &rc);
        m_Width = rc.right - rc.left;
        m_Height = rc.bottom - rc.top;

        // --------------------------------------------------------
        // 1. SwapChain 설정
        // --------------------------------------------------------
        DXGI_SWAP_CHAIN_DESC sd{};
        sd.BufferDesc.Width = m_Width;
        sd.BufferDesc.Height = m_Height;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.SampleDesc.Count = 1;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.BufferCount = 2;
        sd.OutputWindow = hWnd;
        sd.Windowed = TRUE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        // --------------------------------------------------------
        // 2. Direct3D 11 Device 생성 (하드웨어 → WARP 순서로 시도)
        // --------------------------------------------------------
        D3D_FEATURE_LEVEL featureLevel{};
        HRESULT hr = D3D11CreateDeviceAndSwapChain(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,  // 1순위: GPU 기반
            nullptr,
            0,
            nullptr, 0,
            D3D11_SDK_VERSION,
            &sd,
            m_SwapChain.GetAddressOf(),
            m_Device.GetAddressOf(),
            &featureLevel,
            m_Context.GetAddressOf()
        );

        if (FAILED(hr))
        {
            OutputDebugString(L"[D3D] HARDWARE device creation failed, trying WARP...\n");

            hr = D3D11CreateDeviceAndSwapChain(
                nullptr,
                D3D_DRIVER_TYPE_WARP,    // 2순위: CPU 기반 WARP
                nullptr,
                0,
                nullptr, 0,
                D3D11_SDK_VERSION,
                &sd,
                m_SwapChain.GetAddressOf(),
                m_Device.GetAddressOf(),
                &featureLevel,
                m_Context.GetAddressOf()
            );
        }

        if (FAILED(hr))
        {
            OutputDebugString(L"[D3D] Failed to create any D3D11 device.\n");
            return false;
        }

        // --------------------------------------------------------
        // 3. RTV/DSV 생성
        // --------------------------------------------------------
        CreateRTVDSV();

        // --------------------------------------------------------
        // 4. 셰이더 및 리소스 초기화
        // --------------------------------------------------------
        if (!CreateShaders()) return false;
        if (!CreateSkyShader()) return false;
        
        CreateConstantBuffer();
        CreateGridVB();
        CreateBoxMesh();
        CreateGrassBoxMesh();
        CreateSkyMesh();

        LoadBoxTexture();
        LoadGrassBoxTexture();
        LoadSkyTexture();

        CreateSkyRenderStates();

        // --------------------------------------------------------
        // 5. 카메라 기본 설정
        // --------------------------------------------------------
        m_Proj = Matrix::CreatePerspectiveFieldOfView(
            XMConvertToRadians(60.0f),
            float(m_Width) / float(m_Height),
            0.1f, 1000.0f);

        UpdateView();

        OutputDebugString(L"[D3D] Init complete.\n");
        return true;
    }


    void CreateRTVDSV()
    {
        if (m_Context) m_Context->OMSetRenderTargets(0, nullptr, nullptr);
        m_RTV.Reset(); m_DSV.Reset(); m_DSVTex.Reset();

        ComPtr<ID3D11Texture2D> backBuffer;
        m_SwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)backBuffer.GetAddressOf());
        m_Device->CreateRenderTargetView(backBuffer.Get(), nullptr, m_RTV.GetAddressOf());

        D3D11_TEXTURE2D_DESC td{};
        td.Width = m_Width;
        td.Height = m_Height;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        td.SampleDesc.Count = 1;
        td.BindFlags = D3D11_BIND_DEPTH_STENCIL;

        m_Device->CreateTexture2D(&td, nullptr, m_DSVTex.GetAddressOf());
        m_Device->CreateDepthStencilView(m_DSVTex.Get(), nullptr, m_DSV.GetAddressOf());
    }

    bool CreateShaders()
    {
        ComPtr<ID3DBlob> vsb, psb, err;
        UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if _DEBUG
        flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

        // ---- Grid: Color shader ----
        if (FAILED(D3DCompileFromFile(L"BasicColor.hlsl", nullptr, nullptr, "VSMain", "vs_5_0", flags, 0,
            vsb.GetAddressOf(), err.GetAddressOf())))
        {
            if (err) OutputDebugStringA((char*)err->GetBufferPointer());
            return false;
        }
        m_Device->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, m_VSColor.GetAddressOf());

        if (FAILED(D3DCompileFromFile(L"BasicColor.hlsl", nullptr, nullptr, "PSMain", "ps_5_0", flags, 0,
            psb.GetAddressOf(), err.ReleaseAndGetAddressOf())))
        {
            if (err) OutputDebugStringA((char*)err->GetBufferPointer());
            return false;
        }
        m_Device->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, m_PSColor.GetAddressOf());

        D3D11_INPUT_ELEMENT_DESC ilColor[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(VertexPC, pos), D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(VertexPC, col), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
        m_Device->CreateInputLayout(ilColor, _countof(ilColor),
            vsb->GetBufferPointer(), vsb->GetBufferSize(),
            m_InputLayoutColor.GetAddressOf());

        // ---- Box: Texture shader ----
        if (FAILED(D3DCompileFromFile(L"BasicTex.hlsl", nullptr, nullptr, "VSMain", "vs_5_0", flags, 0,
            vsb.ReleaseAndGetAddressOf(), err.ReleaseAndGetAddressOf())))
        {
            if (err) OutputDebugStringA((char*)err->GetBufferPointer());
            return false;
        }
        m_Device->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, m_VSTex.GetAddressOf());

        if (FAILED(D3DCompileFromFile(L"BasicTex.hlsl", nullptr, nullptr, "PSMain", "ps_5_0", flags, 0,
            psb.ReleaseAndGetAddressOf(), err.ReleaseAndGetAddressOf())))
        {
            if (err) OutputDebugStringA((char*)err->GetBufferPointer());
            return false;
        }
        m_Device->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, m_PSTex.GetAddressOf());

        D3D11_INPUT_ELEMENT_DESC ilTex[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(VertexPT, pos), D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, offsetof(VertexPT, uv),  D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };

        m_Device->CreateInputLayout(ilTex, _countof(ilTex),
            vsb->GetBufferPointer(), vsb->GetBufferSize(),
            m_InputLayoutTex.GetAddressOf());

        return true;
    }

    bool CreateSkyShader()
    {
        ComPtr<ID3DBlob> vsb, psb, err;
        UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if _DEBUG
        flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

        // Vertex Shader
        if (FAILED(D3DCompileFromFile(L"BasicSkyCubeMap.hlsl", nullptr, nullptr, "VSMain", "vs_5_0", flags, 0,
            vsb.GetAddressOf(), err.GetAddressOf())))
        {
            if (err) OutputDebugStringA((char*)err->GetBufferPointer());
            return false;
        }
        m_Device->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, m_VSSky.GetAddressOf());

        // Pixel Shader
        if (FAILED(D3DCompileFromFile(L"BasicSkyCubeMap.hlsl", nullptr, nullptr, "PSMain", "ps_5_0", flags, 0,
            psb.GetAddressOf(), err.ReleaseAndGetAddressOf())))
        {
            if (err) OutputDebugStringA((char*)err->GetBufferPointer());
            return false;
        }
        m_Device->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, m_PSSky.GetAddressOf());

        // Input Layout (POSITION only)
        D3D11_INPUT_ELEMENT_DESC ilSky[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };


        HRESULT hrIL = m_Device->CreateInputLayout(ilSky, _countof(ilSky),
            vsb->GetBufferPointer(), vsb->GetBufferSize(),
            m_InputLayoutSky.GetAddressOf());

        if (FAILED(hrIL)) OutputDebugString(L"[Skybox] InputLayout creation FAILED\n");
        else OutputDebugString(L"[Skybox] InputLayout creation OK\n");

        return true;
    }


    void CreateSkyMesh()
    {
        float s = 50.0f;
        VertexP v[8] =
        {
            {{-s, -s, -s}}, {{+s, -s, -s}}, {{+s, +s, -s}}, {{-s, +s, -s}},
            {{-s, -s, +s}}, {{+s, -s, +s}}, {{+s, +s, +s}}, {{-s, +s, +s}},
        };

        D3D11_BUFFER_DESC vbd{};
        vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        vbd.ByteWidth = sizeof(v);
        vbd.Usage = D3D11_USAGE_IMMUTABLE;
        D3D11_SUBRESOURCE_DATA vsd{ v, 0, 0 };
        m_Device->CreateBuffer(&vbd, &vsd, m_SkyVB.GetAddressOf());

        uint16_t idx[] =
        {
            0,3,2, 0,2,1,
            1,2,6, 1,6,5,
            5,6,7, 5,7,4,
            4,7,3, 4,3,0,
            3,7,6, 3,6,2,
            4,0,1, 4,1,5
        };
        m_SkyIndexCount = _countof(idx);

        D3D11_BUFFER_DESC ibd{};
        ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
        ibd.ByteWidth = sizeof(idx);
        ibd.Usage = D3D11_USAGE_IMMUTABLE;
        D3D11_SUBRESOURCE_DATA isd{ idx, 0, 0 };
        m_Device->CreateBuffer(&ibd, &isd, m_SkyIB.GetAddressOf());
    }

    void LoadSkyTexture()
    {
        // DDS CubeMap 로드
        HRESULT hr = CreateDDSTextureFromFile(
            m_Device.Get(),
            L"skybox.dds",
            nullptr,
            m_SkySRV.GetAddressOf()
        );

        if (FAILED(hr))
            OutputDebugString(L"Failed to load CubeMap skybox.dds\n");

        // Cube 샘플러: CLAMP 대신 WRAP을 써도 무방
        D3D11_SAMPLER_DESC sd{};
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
        sd.MaxLOD = D3D11_FLOAT32_MAX;

        m_Device->CreateSamplerState(&sd, m_SkySampler.GetAddressOf());
    }

    void CreateSkyRenderStates() // depth stencil 만드는 과정
    {
        D3D11_DEPTH_STENCIL_DESC dsd{};
        dsd.DepthEnable = TRUE;
        dsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        dsd.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
        m_Device->CreateDepthStencilState(&dsd, m_SkyDSS.GetAddressOf());


        D3D11_RASTERIZER_DESC rd{};
        rd.FillMode = D3D11_FILL_SOLID;
        rd.CullMode = D3D11_CULL_FRONT;
        rd.FrontCounterClockwise = TRUE;
        m_Device->CreateRasterizerState(&rd, m_SkyRS.GetAddressOf());
    }

    void CreateConstantBuffer()
    {
        D3D11_BUFFER_DESC bd{};
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bd.ByteWidth = sizeof(CBVS);
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        m_Device->CreateBuffer(&bd, nullptr, m_CBVS.GetAddressOf());
    }

    void CreateGridVB()
    {
        const int   N = m_HalfCells;
        const float s = m_CellSize;
        const float half = N * s;

        std::vector<VertexPC> v;
        v.reserve((N * 2 + 1) * 4);

        Vector3 cMajor(1.0f, 1.0f, 0.0f); // 노란색
        Vector3 cMinor(0.15f, 0.15f, 0.15f);

        Vector3 cAxisX(0.8f, 0.2f, 0.2f);
        Vector3 cAxisZ(0.2f, 0.4f, 0.8f);

        for (int i = -N; i <= N; ++i)
        {
            float x = i * s;
            Vector3 col = (i == 0) ? cAxisZ : ((i % 5 == 0) ? cMajor : cMinor);
            v.push_back({ Vector3(-half, 0, x), col });
            v.push_back({ Vector3(+half, 0, x), col });

            float z = i * s;
            col = (i == 0) ? cAxisX : ((i % 5 == 0) ? cMajor : cMinor);
            v.push_back({ Vector3(z, 0, -half), col });
            v.push_back({ Vector3(z, 0, +half), col });
        }

        m_GridVertexCount = (UINT)v.size();

        D3D11_BUFFER_DESC bd{};
        bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        bd.ByteWidth = UINT(v.size() * sizeof(VertexPC));
        bd.Usage = D3D11_USAGE_IMMUTABLE;
        D3D11_SUBRESOURCE_DATA sd{ v.data(), 0, 0 };

        m_Device->CreateBuffer(&bd, &sd, m_GridVB.GetAddressOf());
    }

    void CreateBoxMesh()
    {
        Vector3 p[8] =
        {
            {-0.5f, 0.0f, -0.5f}, {+0.5f, 0.0f, -0.5f},
            {+0.5f, 1.0f, -0.5f}, {-0.5f, 1.0f, -0.5f},
            {-0.5f, 0.0f, +0.5f}, {+0.5f, 0.0f, +0.5f},
            {+0.5f, 1.0f, +0.5f}, {-0.5f, 1.0f, +0.5f},
        };

        VertexPT v24[24] =
        {
            {p[0], {0,1}}, {p[1], {1,1}}, {p[2], {1,0}}, {p[3], {0,0}},
            {p[1], {0,1}}, {p[5], {1,1}}, {p[6], {1,0}}, {p[2], {0,0}},
            {p[5], {0,1}}, {p[4], {1,1}}, {p[7], {1,0}}, {p[6], {0,0}},
            {p[4], {0,1}}, {p[0], {1,1}}, {p[3], {1,0}}, {p[7], {0,0}},
            {p[3], {0,1}}, {p[2], {1,1}}, {p[6], {1,0}}, {p[7], {0,0}},
            {p[4], {0,0}}, {p[5], {1,0}}, {p[1], {1,1}}, {p[0], {0,1}},
        };

        uint16_t idx[] =
        {
            0,1,2, 0,2,3,
            4,5,6, 4,6,7,
            8,9,10, 8,10,11,
            12,13,14, 12,14,15,
            16,17,18, 16,18,19,
            20,21,22, 20,22,23
        };
        m_BoxIndexCount = _countof(idx);

        D3D11_BUFFER_DESC vbd{};
        vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        vbd.ByteWidth = sizeof(v24);
        vbd.Usage = D3D11_USAGE_IMMUTABLE;
        D3D11_SUBRESOURCE_DATA vsd{ v24, 0, 0 };
        m_Device->CreateBuffer(&vbd, &vsd, m_BoxVB.GetAddressOf());

        D3D11_BUFFER_DESC ibd{};
        ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
        ibd.ByteWidth = sizeof(idx);
        ibd.Usage = D3D11_USAGE_IMMUTABLE;
        D3D11_SUBRESOURCE_DATA isd{ idx, 0, 0 };

        m_Device->CreateBuffer(&ibd, &isd, m_BoxIB.GetAddressOf());
    }

    void CreateGrassBoxMesh()
    {
        Vector3 p[8] =
        {
            {-0.5f, 0.0f, -0.5f}, {+0.5f, 0.0f, -0.5f},
            {+0.5f, 1.0f, -0.5f}, {-0.5f, 1.0f, -0.5f},
            {-0.5f, 0.0f, +0.5f}, {+0.5f, 0.0f, +0.5f},
            {+0.5f, 1.0f, +0.5f}, {-0.5f, 1.0f, +0.5f},
        };

        VertexPT v24[24] =
        {
            {p[0], {0,1}}, {p[1], {1,1}}, {p[2], {1,0}}, {p[3], {0,0}},
            {p[1], {0,1}}, {p[5], {1,1}}, {p[6], {1,0}}, {p[2], {0,0}},
            {p[5], {0,1}}, {p[4], {1,1}}, {p[7], {1,0}}, {p[6], {0,0}},
            {p[4], {0,1}}, {p[0], {1,1}}, {p[3], {1,0}}, {p[7], {0,0}},
            {p[3], {0,1}}, {p[2], {1,1}}, {p[6], {1,0}}, {p[7], {0,0}},
            {p[4], {0,0}}, {p[5], {1,0}}, {p[1], {1,1}}, {p[0], {0,1}},
        };

        uint16_t idx[] =
        {
            0,1,2, 0,2,3,
            4,5,6, 4,6,7,
            8,9,10, 8,10,11,
            12,13,14, 12,14,15,
            16,17,18, 16,18,19,
            20,21,22, 20,22,23
        };
        m_BoxIndexCount = _countof(idx);

        D3D11_BUFFER_DESC vbd{};
        vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        vbd.ByteWidth = sizeof(v24);
        vbd.Usage = D3D11_USAGE_IMMUTABLE;
        D3D11_SUBRESOURCE_DATA vsd{ v24, 0, 0 };
        m_Device->CreateBuffer(&vbd, &vsd, m_BoxVB.GetAddressOf());

        D3D11_BUFFER_DESC ibd{};
        ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
        ibd.ByteWidth = sizeof(idx);
        ibd.Usage = D3D11_USAGE_IMMUTABLE;
        D3D11_SUBRESOURCE_DATA isd{ idx, 0, 0 };

        m_Device->CreateBuffer(&ibd, &isd, m_BoxIB.GetAddressOf());
    }

    void LoadBoxTexture()
    {
        CreateWICTextureFromFile(
            m_Device.Get(), m_Context.Get(),
            L"BoxTexture.png", nullptr, m_TexSRV.GetAddressOf());

        D3D11_SAMPLER_DESC sd{};
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        m_Device->CreateSamplerState(&sd, m_Sampler.GetAddressOf());
    }

    void LoadGrassBoxTexture()
    {
        // DDS CubeMap 로드
        HRESULT hr = CreateDDSTextureFromFile(
            m_Device.Get(),
            L"Field_micro04.dds",
            nullptr,
            m_TexSRVGrass.GetAddressOf()
        );

        if (FAILED(hr))
            OutputDebugString(L"Failed to load Field_micro04.dds\n");

        // Cube 샘플러: CLAMP 대신 WRAP을 써도 무방
        D3D11_SAMPLER_DESC sd{};
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
        sd.MaxLOD = D3D11_FLOAT32_MAX;

        m_Device->CreateSamplerState(&sd, m_SamplerGrass.GetAddressOf());

    }

    void UpdateAndDraw()
    {
        float clear[4] = { 0.08f, 0.09f, 0.11f, 1.0f };
        m_Context->OMSetRenderTargets(1, m_RTV.GetAddressOf(), m_DSV.Get());
        m_Context->ClearRenderTargetView(m_RTV.Get(), clear);
        m_Context->ClearDepthStencilView(m_DSV.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

        //“화면 크기(m_Width × m_Height) 영역 전체를 렌더링 대상으로 지정
		// 매 프레임마다 뷰포트 설정을 할 필요는 없지만, 여기서는 명확히 하기 위해 매 프레임 설정
        D3D11_VIEWPORT vp{ 0,0,(FLOAT)m_Width,(FLOAT)m_Height,0,1 };
        m_Context->RSSetViewports(1, &vp);

        //순서는 지켜져야한다.
        // ---- Skybox ----
        RenderSkybox();

        // ---- Grid ----
        m_Context->IASetInputLayout(m_InputLayoutColor.Get());
        m_Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);

        UINT stride = sizeof(VertexPC), offset = 0;
        m_Context->IASetVertexBuffers(0, 1, m_GridVB.GetAddressOf(), &stride, &offset);
        m_Context->VSSetShader(m_VSColor.Get(), nullptr, 0);
        m_Context->PSSetShader(m_PSColor.Get(), nullptr, 0);
        
		MapAndSetCB(Matrix::Identity, m_View * m_Proj); 
        
        m_Context->Draw(m_GridVertexCount, 0);

        // ---- Box ----
        m_Context->IASetInputLayout(m_InputLayoutTex.Get());
        m_Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        stride = sizeof(VertexPT); offset = 0;

        m_Context->IASetVertexBuffers(0, 1, m_BoxVB.GetAddressOf(), &stride, &offset);
        m_Context->IASetIndexBuffer(m_BoxIB.Get(), DXGI_FORMAT_R16_UINT, 0);
        
        m_Context->VSSetShader(m_VSTex.Get(), nullptr, 0);
        m_Context->PSSetShader(m_PSTex.Get(), nullptr, 0);
        
        m_Context->PSSetShaderResources(0, 1, m_TexSRV.GetAddressOf());
        m_Context->PSSetSamplers(0, 1, m_Sampler.GetAddressOf());

        //Grass 로딩
        //m_Context->PSSetShaderResources(0, 1, m_TexSRVGrass.GetAddressOf());
        //m_Context->PSSetSamplers(0, 1, m_SamplerGrass.GetAddressOf());
        
        MapAndSetCB(m_BoxWorld, m_View * m_Proj);
        
        m_Context->DrawIndexed(m_BoxIndexCount, 0, 0);

        m_SwapChain->Present(1, 0);
    }

    void RenderSkybox()
    {
        if (!m_SkySRV) OutputDebugString(L"[Skybox] SRV NULL\n");
        if (!m_PSSky) OutputDebugString(L"[Skybox] PixelShader null\n");
        if (!m_VSSky) OutputDebugString(L"[Skybox] VertexShader null\n");
        if (!m_InputLayoutSky) OutputDebugString(L"[Skybox] InputLayout null\n");

       
        // ------------------------------------------
        // 1. 깊이/래스터라이저 상태 설정
        // ------------------------------------------
        m_Context->OMSetDepthStencilState(m_SkyDSS.Get(), 0);
        m_Context->RSSetState(m_SkyRS.Get());

        // ------------------------------------------
        // 2. 카메라 변환 (위치 제외)
        // ------------------------------------------
        Matrix viewNoTrans = m_View;
        viewNoTrans._41 = 0.0f;
        viewNoTrans._42 = 0.0f;
        viewNoTrans._43 = 0.0f;

        Matrix skyVP = viewNoTrans * m_Proj;

        // ------------------------------------------
        // 3. 상수 버퍼 업데이트 (월드는 단위행렬)
        // ------------------------------------------
        D3D11_MAPPED_SUBRESOURCE mapped{};
        m_Context->Map(m_CBVS.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        auto* cb = reinterpret_cast<CBVS*>(mapped.pData);
        cb->gWorld = Matrix::Identity.Transpose();
        cb->gViewProj = skyVP.Transpose(); // vp : view projection
        m_Context->Unmap(m_CBVS.Get(), 0);
        m_Context->VSSetConstantBuffers(0, 1, m_CBVS.GetAddressOf());

        // ------------------------------------------
        // 4. 입력 어셈블러 설정
        // ------------------------------------------
        UINT stride = sizeof(VertexP);
        UINT offset = 0;
        m_Context->IASetInputLayout(m_InputLayoutSky.Get());
        m_Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_Context->IASetVertexBuffers(0, 1, m_SkyVB.GetAddressOf(), &stride, &offset);
        m_Context->IASetIndexBuffer(m_SkyIB.Get(), DXGI_FORMAT_R16_UINT, 0);

        // ------------------------------------------
        // 5. 셰이더 및 리소스 바인딩
        // ------------------------------------------
        m_Context->VSSetShader(m_VSSky.Get(), nullptr, 0);
        m_Context->PSSetShader(m_PSSky.Get(), nullptr, 0);

        ID3D11ShaderResourceView* srv = m_SkySRV.Get();
        ID3D11SamplerState* samp = m_SkySampler.Get();
        m_Context->PSSetShaderResources(0, 1, &srv);
        m_Context->PSSetSamplers(0, 1, &samp);

        // ------------------------------------------
        // 6. 실제 드로우 호출
        // ------------------------------------------
        m_Context->DrawIndexed(m_SkyIndexCount, 0, 0);

        // ------------------------------------------
        // 7. 상태 복원
        // ------------------------------------------
        m_Context->OMSetDepthStencilState(nullptr, 0);
        m_Context->RSSetState(nullptr);
    }


    void MapAndSetCB(const Matrix& world, const Matrix& viewProj)
    {
        D3D11_MAPPED_SUBRESOURCE ms{};
        m_Context->Map(m_CBVS.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms);

        auto* cb = reinterpret_cast<CBVS*>(ms.pData);
        cb->gWorld = world.Transpose();
        cb->gViewProj = viewProj.Transpose();
        
        m_Context->Unmap(m_CBVS.Get(), 0);
        
        m_Context->VSSetConstantBuffers(0, 1, m_CBVS.GetAddressOf());
    }

    void ScreenRay(int mx, int my, Vector3& outOrigin, Vector3& outDir)
    {
        float x = (2.0f * mx / float(m_Width)) - 1.0f;
        float y = 1.0f - (2.0f * my / float(m_Height));

        Matrix invVP = (m_View * m_Proj).Invert();
        Vector3 nearW = Vector3::Transform(Vector3(x, y, 0.0f), invVP);
        Vector3 farW = Vector3::Transform(Vector3(x, y, 1.0f), invVP);

        outOrigin = nearW;
        outDir = (farW - nearW); outDir.Normalize();
    }

    bool RayHitGround(const Vector3& ro, const Vector3& rd, Vector3& outHit)
    {
        if (fabsf(rd.y) < 1e-6f) return false;
        float t = -ro.y / rd.y;
        if (t < 0) return false;
        outHit = ro + rd * t;
        return true;
    }

    Vector3 SnapToCellCenter(const Vector3& p)
    {
        float s = m_CellSize;
        float cx = floorf(p.x / s) * s + s * 0.5f;
        float cz = floorf(p.z / s) * s + s * 0.5f;

        float half = m_HalfCells * s;
        cx = std::clamp(cx, -half + s * 0.5f, half - s * 0.5f);
        cz = std::clamp(cz, -half + s * 0.5f, half - s * 0.5f);
        return { cx, 0.0f, cz };
    }

    void OnClick(int mx, int my)
    {
        Vector3 ro, rd; ScreenRay(mx, my, ro, rd);
        Vector3 hit;
        if (RayHitGround(ro, rd, hit))
        {
            Vector3 c = SnapToCellCenter(hit);
            Matrix S = Matrix::CreateScale(m_CellSize, 1.0f, m_CellSize);
            Matrix T = Matrix::CreateTranslation(c);
            m_BoxWorld = S * T;
        }
    }

    void UpdateView()
    {
        // 1️. 각도 및 거리 범위 제한
        m_CamPitch = std::clamp(m_CamPitch, XMConvertToRadians(-85.0f), XMConvertToRadians(85.0f));
        m_CamRadius = std::clamp(m_CamRadius, 2.0f, 200.0f);

        // 2. Yaw 값 정규화 (회전 누적 방지)
        if (m_CamYaw > XM_2PI)  m_CamYaw -= XM_2PI;
        if (m_CamYaw < 0.0f)    m_CamYaw += XM_2PI;

        // 3️. 구면 좌표 -> 데카르트 좌표 변환
        const float cosPitch = cosf(m_CamPitch);
        const float sinPitch = sinf(m_CamPitch);
        const float cosYaw = cosf(m_CamYaw);
        const float sinYaw = sinf(m_CamYaw);

        const float x = m_CamRadius * cosPitch * cosYaw;
        const float z = m_CamRadius * cosPitch * sinYaw;
        const float y = m_CamRadius * sinPitch;

        Vector3 camPos(x, y, z);
        Vector3 target(0.0f, 0.0f, 0.0f);

        // 4️. Up 벡터 뒤집힘 방지
        Vector3 up = (fabsf(m_CamPitch) > XMConvertToRadians(89.5f)) ? Vector3(0, 0, 1) : Vector3(0, 1, 0);

        // 5️. 뷰 행렬 생성
        m_View = Matrix::CreateLookAt(camPos, target, up);

        // 6️. 디버그용 윈도우 타이틀 업데이트
        wchar_t title[128];
        swprintf_s(title, L"DX11 Skybox + Grid + Box  |  CamPos: (%.2f, %.2f, %.2f)", x, y, z);
        SetWindowTextW(m_hWnd, title);
    }


    void Resize(UINT w, UINT h)
    {
        if (!m_Device) return;
        m_Width = w; m_Height = h;
        m_Context->OMSetRenderTargets(0, nullptr, nullptr);
        m_RTV.Reset(); m_DSV.Reset(); m_DSVTex.Reset();
        m_SwapChain->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0);
        CreateRTVDSV();
        m_Proj = Matrix::CreatePerspectiveFieldOfView(
            XMConvertToRadians(60.0f),
            float(w) / float(h), 0.1f, 1000.0f);
    }
};

static App* g_App = nullptr;

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_SIZE:
        if (g_App && wParam != SIZE_MINIMIZED)
        {
            g_App->Resize(LOWORD(lParam), HIWORD(lParam));
        }
        break;

    case WM_LBUTTONDOWN:
        if (g_App)
        {
            int mx = GET_X_LPARAM(lParam);
            int my = GET_Y_LPARAM(lParam);
            g_App->OnClick(mx, my);
        }
        break;

    case WM_RBUTTONDOWN:
        if (g_App)
        {
            g_App->m_RBtnDown = true;
            SetCapture(hWnd);
        }
        break;

    case WM_RBUTTONUP:
        if (g_App)
        {
            g_App->m_RBtnDown = false;
            ReleaseCapture();
        }
        break;

    case WM_MOUSEMOVE:
        if (g_App && g_App->m_RBtnDown)
        {
            int mx = GET_X_LPARAM(lParam);
            int my = GET_Y_LPARAM(lParam);
            float dx = float(mx - g_App->m_LastMouse.x) * 0.005f;
            float dy = float(my - g_App->m_LastMouse.y) * 0.005f;
            g_App->m_CamYaw += dx;
            g_App->m_CamPitch -= dy;
            g_App->m_LastMouse.x = mx;
            g_App->m_LastMouse.y = my;
            g_App->UpdateView();
        }
        break;

    case WM_MOUSEWHEEL:
        if (g_App)
        {
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            g_App->m_CamRadius *= (delta > 0) ? 0.9f : 1.1f;
            g_App->UpdateView();

        }
        break;

    case WM_MBUTTONDOWN:  //Wheel 클릭
        if (g_App)
        {
            g_App->m_LastMouse.x = GET_X_LPARAM(lParam);
            g_App->m_LastMouse.y = GET_Y_LPARAM(lParam);
            g_App->OnClick(g_App->m_LastMouse.x, g_App->m_LastMouse.y);
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;

    default: break;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

int WINAPI wWinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR lpCmdLine,
    _In_ int nCmdShow
)
{
    WNDCLASSEX wc{ sizeof(WNDCLASSEX) };
    wc.hInstance = hInstance;
    wc.lpszClassName = L"DX11_SkyboxGridBox";
    wc.lpfnWndProc = WndProc;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassEx(&wc);

    RECT rc{ 0,0,1280,720 };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hWnd = CreateWindowW(wc.lpszClassName, L"DX11 Skybox + Grid + Box",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left, rc.bottom - rc.top,
        nullptr, nullptr, hInstance, nullptr);

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    App app; g_App = &app;
    if (!app.Init(hWnd)) return -1;

    MSG msg{};
    while (msg.message != WM_QUIT)
    {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else
        {
            app.UpdateAndDraw();
        }
    }
    return 0;
}