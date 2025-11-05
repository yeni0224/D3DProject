// BasicTex.hlsl
// Textured box (POSITION + TEXCOORD)
#pragma pack_matrix(column_major)   // C++에서 Transpose()로 올린 행렬과 호환

cbuffer CBVS : register(b0)
{
    float4x4 gWorld;
    float4x4 gViewProj;
};

Texture2D gTex : register(t0);
SamplerState gSamp : register(s0);

struct VSInput
{
    float3 pos : POSITION;
    float2 uv : TEXCOORD0; // 명시적으로 0 붙임
};

struct PSInput
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

PSInput VSMain(VSInput vin)
{
    PSInput v;
    float4 wpos = mul(float4(vin.pos, 1.0f), gWorld); // model→world
    v.pos = mul(wpos, gViewProj); // world→clip
    v.uv = vin.uv;
    return v;
}

float4 PSMain(PSInput pin) : SV_Target
{
    // 텍스처 샘플링 (필요시 sRGB/Gamma는 런타임 상태로 처리)
    return gTex.Sample(gSamp, pin.uv);
}
