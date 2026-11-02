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
    uint bHasReceiverNormalMap;
    uint bHasDecalNormalMap;
    float2 ReceiverUVOffset;
    uint bFlipReceiverNormalGreen;
    uint bFlipDecalNormalGreen;
    float3 ProjectionDirection;
    float AngleFadeCosEnd;
    float AngleFadeCosStart;
    float3 AngleFadePadding;
};

#define USE_NORMAL_MAP 1
#include "LightingCommon.hlsli"
#include "DecalCommon.hlsli"

Texture2D DecalTexture : register(t0);
Texture2D ReceiverNormalTexture : register(t1);
Texture2D DecalNormalTexture : register(t4);
SamplerState DecalSampler : register(s0);
SamplerState ReceiverSampler : register(s3);

struct VSInput
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float4 Tangent : TANGENT;
    float4 Color : COLOR;
    float2 UV : TEXCOORD;
};

struct PSInput
{
    float4 Position : SV_POSITION;
    float3 DecalPosition : TEXCOORD0;
    float3 WorldPosition : TEXCOORD1;
    float3 WorldNormal : TEXCOORD2;
    float4 WorldTangent : TEXCOORD3;
    float2 ReceiverUV : TEXCOORD4;
};

float3 DecodeNormal(float3 Sample, bool bFlipGreen)
{
    float3 N = normalize(Sample * 2.0f - 1.0f);
    N.y = bFlipGreen ? -N.y : N.y;
    return N;
}

float3 BlendReorientedNormals(float3 Base, float3 Detail)
{
    // Both normals use the projected surface frame; a flat detail is identity.
    float3 T = Base + float3(0.0f, 0.0f, 1.0f);
    if (T.z < 1e-5f)
        return float3(Detail.x, -Detail.y, -Detail.z);
    float3 U = Detail * float3(-1.0f, -1.0f, 1.0f);
    return normalize(T * (dot(T, U) / T.z) - U);
}

float3 ApplyProjectedNormal(float3 BaseNormal, float3 GeoNormal, float3 Detail,
    float3 PositionDX, float3 PositionDY, float2 UVDX, float2 UVDY)
{
    float Det = UVDX.x * UVDY.y - UVDX.y * UVDY.x;
    float GradientScale = sqrt(dot(UVDX, UVDX) * dot(UVDY, UVDY));
    // Edge-on projection has no usable UV frame; keep the receiving normal.
    if (abs(Det) <= max(1e-20f, GradientScale * 1e-5f))
        return BaseNormal;

    float3 T = (PositionDX * UVDY.y - PositionDY * UVDX.y) / Det;
    float3 B = (PositionDY * UVDX.x - PositionDX * UVDY.x) / Det;
    T -= GeoNormal * dot(T, GeoNormal);
    float LengthSq = dot(T, T);
    if (LengthSq <= 1e-12f)
        return BaseNormal;
    T *= rsqrt(LengthSq);
    float Handedness = dot(cross(GeoNormal, T), B) < 0.0f ? -1.0f : 1.0f;
    B = cross(GeoNormal, T) * Handedness;

    float3 BaseTS = float3(dot(BaseNormal, T), dot(BaseNormal, B), dot(BaseNormal, GeoNormal));
    float3 Combined = BlendReorientedNormals(BaseTS, Detail);
    return normalize(Combined.x * T + Combined.y * B + Combined.z * GeoNormal);
}

PSInput mainVS(VSInput input)
{
    PSInput output;
    
    float4 worldPosition = mul(float4(input.Position, 1.0f), World);
    
    output.Position = mul(worldPosition, ViewProjection);
    output.WorldPosition = worldPosition.xyz;
    output.WorldNormal = mul(input.Normal, (float3x3)NormalMatrix);
    output.WorldTangent = float4(mul(input.Tangent.xyz, (float3x3)World), input.Tangent.w);
    output.ReceiverUV = input.UV + ReceiverUVOffset;
    
    float3 localPosition = mul(worldPosition, WorldToDecal).xyz;
    
    output.DecalPosition = localPosition / DecalSize;
    
    return output;
}

float4 mainPS(PSInput input) : SV_TARGET
{
    float2 DecalUV = input.DecalPosition.yz * float2(0.5f, -0.5f) + 0.5f;
    // Evaluate derivatives before discard so volume edges retain valid UV gradients.
    float2 DecalUVDX = ddx(DecalUV);
    float2 DecalUVDY = ddy(DecalUV);
    float2 ReceiverUVDX = ddx(input.ReceiverUV);
    float2 ReceiverUVDY = ddy(input.ReceiverUV);
    float3 PositionDX = ddx(input.WorldPosition);
    float3 PositionDY = ddy(input.WorldPosition);
    clip(1.0f - abs(input.DecalPosition));
    
    float4 Color = DecalTexture.SampleGrad(DecalSampler, DecalUV, DecalUVDX, DecalUVDY) * DecalColor;
    
    // Geometric receiver normal controls coverage; normal maps only affect lighting.
    float AngleFade = ComputeDecalAngleFade(input.WorldNormal, ProjectionDirection,
        float2(AngleFadeCosEnd, AngleFadeCosStart));
    Color.a *= Opacity * AngleFade;
    if (AngleFade <= 0.0f)
        discard;

    if (bLit != 0u)
    {
        float3 GeoNormal = normalize(input.WorldNormal);
        float3 N = GeoNormal;
        if (bHasReceiverNormalMap != 0u)
        {
            float3 T = input.WorldTangent.xyz - GeoNormal * dot(input.WorldTangent.xyz, GeoNormal);
            float LengthSq = dot(T, T);
            if (LengthSq > 1e-12f)
            {
                T *= rsqrt(LengthSq);
                float3 B = cross(GeoNormal, T) * input.WorldTangent.w;
                float3 NormalTS = DecodeNormal(ReceiverNormalTexture.SampleGrad(ReceiverSampler,
                    input.ReceiverUV, ReceiverUVDX, ReceiverUVDY).xyz, bFlipReceiverNormalGreen != 0u);
                N = normalize(NormalTS.x * T + NormalTS.y * B + NormalTS.z * GeoNormal);
            }
        }
        if (bHasDecalNormalMap != 0u)
        {
            float3 Detail = DecodeNormal(DecalNormalTexture.SampleGrad(DecalSampler,
                DecalUV, DecalUVDX, DecalUVDY).xyz, bFlipDecalNormalGreen != 0u);
            N = ApplyProjectedNormal(N, GeoNormal, Detail, PositionDX, PositionDY, DecalUVDX, DecalUVDY);
        }
        float3 V = normalize(CameraPosition - input.WorldPosition);
        float3 DiffuseLight;
        float3 SpecularLight;
        ComputeTiledLighting(N, GeoNormal, input.WorldPosition, V, Shininess, input.Position.xy,
            DiffuseLight, SpecularLight, LightingModel, bReceiveShadow != 0u,
            bHasReceiverNormalMap != 0u || bHasDecalNormalMap != 0u);
        Color.rgb = Color.rgb * (AmbientLight.Color + DiffuseLight) + SpecularColor * SpecularLight;
    }
    
    return Color;
}
