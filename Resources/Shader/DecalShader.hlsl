#pragma pack_matrix(row_major)

cbuffer DecalConstants : register(b0)
{
    matrix World;
    matrix ViewProjection;
    matrix WorldToDecal;
    matrix NormalMatrix;
    
    float3 DecalSize;
    float Opacity;
    float4 DecalColor;
    float3 CameraPosition;
    float Shininess;
    float3 SpecularColor;
    uint bLit;
    uint LightingModel;
    uint bReceiveShadow;
    float2 Padding;
};

#include "LightingCommon.hlsli"

Texture2D DecalTexture : register(t0);
SamplerState DecalSampler : register(s0);

struct VSInput
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
};

struct PSInput
{
    float4 Position : SV_POSITION;
    float3 DecalPosition : TEXCOORD0;
    float3 WorldPosition : TEXCOORD1;
    float3 WorldNormal : TEXCOORD2;
};

PSInput mainVS(VSInput input)
{
    PSInput output;
    
    float4 worldPosition = mul(float4(input.Position, 1.0f), World);
    
    output.Position = mul(worldPosition, ViewProjection);
    output.WorldPosition = worldPosition.xyz;
    output.WorldNormal = mul(input.Normal, (float3x3)NormalMatrix);
    
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

    if (bLit != 0u)
    {
        float3 N = normalize(input.WorldNormal);
        float3 V = normalize(CameraPosition - input.WorldPosition);
        float3 DiffuseLight;
        float3 SpecularLight;
        ComputeTiledLighting(N, input.WorldPosition, V, Shininess, input.Position.xy,
            DiffuseLight, SpecularLight, LightingModel, bReceiveShadow != 0u);
        Color.rgb = Color.rgb * (AmbientLight.Color + DiffuseLight) + SpecularColor * SpecularLight;
    }
    
    return Color;
}
