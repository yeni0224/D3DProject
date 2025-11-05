// BasicColor.hlsl
// Grid line rendering (POSITION + COLOR)
#pragma pack_matrix(column_major)   // C++에서 Transpose()로 올린 행렬과 호환

cbuffer CBVS : register(b0)
{
    float4x4 gWorld;
    float4x4 gViewProj;
};

struct VSInput
{
    float3 pos : POSITION;
    float3 col : COLOR;
};

struct PSInput
{
    float4 pos : SV_POSITION;
    float3 col : COLOR;
};

PSInput VSMain(VSInput vin)
{
    PSInput v;
    float4 wpos = mul(float4(vin.pos, 1.0f), gWorld); // model→world
    v.pos = mul(wpos, gViewProj); // world→clip
    v.col = vin.col;
    return v;
}

float4 PSMain(PSInput pin) : SV_Target
{
    return float4(pin.col, 1.0f);
}
