#pragma pack_matrix(row_major)

cbuffer DecalConstants : register(b0)
{
    matrix World;
    matrix ViewProjection;
    matrix WorldToDecal;
    
    float3 DecalSize;
    float Opacity;
    float4 DecalColor;
};

Texture2D DecalTexture : register(t0);
SamplerState DecalSampler : register(s0);

struct VSInput
{
    float3 Position : POSITION;
};

struct PSInput
{
    float4 Position : SV_POSITION;
    float3 DecalPosition : TEXCOORD0;
};

PSInput mainVS(VSInput input)
{
    PSInput output;
    
    float4 worldPosition = mul(float4(input.Position, 1.0f), World);
    
    output.Position = mul(worldPosition, ViewProjection);
    
    float3 localPosition = mul(worldPosition, WorldToDecal).xyz;
    
    output.DecalPosition = localPosition / DecalSize;
    
    return output;
}

float4 mainPS(PSInput input) : SV_TARGET
{
    clip(1.0f - abs(input.DecalPosition));
    
    float2 DecalUV = input.DecalPosition.yz * float2(0.5f, -0.5f) + 0.5f;
    
    float4 Color = DecalTexture.Sample(DecalSampler, DecalUV) * DecalColor;
    
    Color.a *= Opacity;
    
    return Color;
}