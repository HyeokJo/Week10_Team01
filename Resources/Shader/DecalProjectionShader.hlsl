#pragma pack_matrix(row_major)
#include "DecalCommon.hlsli"

#ifndef DECAL_WRITE_NORMAL
#define DECAL_WRITE_NORMAL 1
#endif

Texture2D<float> SceneDepth : register(t0);
Texture2D<float4> DecalTexture : register(t1);
Texture2D<uint2> DecalReceiverMask : register(t2);
Texture2D<float4> DecalNormalTexture : register(t4);
SamplerState DecalSampler : register(s0);

cbuffer DecalViewConstants : register(b0)
{
    matrix InverseViewProjection;
    float2 ViewportOrigin;
    float2 InverseViewportSize;
};

cbuffer DecalBatchConstants : register(b1)
{
    uint InstanceOffset;
    uint3 BatchPadding;
};

struct FDecalInstance
{
    matrix WorldToDecal;
    float3 DecalSize;
    float Opacity;
    float4 BaseColor;
    float4 ScreenRect;
    float3 ProjectionDirection;
    float ProjectionEndFade;
    float2 AngleFadeCosines;
    uint bHasNormalMap;
    uint bFlipNormalGreen;
};
StructuredBuffer<FDecalInstance> DecalInstances : register(t14);

struct PSInput
{
    float4 Position : SV_POSITION;
    nointerpolation uint InstanceIndex : TEXCOORD0;
};

PSInput mainVS(uint VertexID : SV_VertexID, uint InstanceID : SV_InstanceID)
{
    static const float2 Corners[6] = {
        float2(0, 0), float2(1, 0), float2(0, 1),
        float2(0, 1), float2(1, 0), float2(1, 1)
    };
    PSInput Output;
    Output.InstanceIndex = InstanceOffset + InstanceID;
    float4 Rect = DecalInstances[Output.InstanceIndex].ScreenRect;
    float2 ScreenUV = lerp(Rect.xy, Rect.zw, Corners[VertexID]);
    Output.Position = float4(ScreenUV * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return Output;
}

float4 ReconstructWorldPosition(int2 Pixel, float Depth)
{
    float2 ScreenUV = (float2(Pixel) + 0.5f - ViewportOrigin) * InverseViewportSize;
    float2 NDC = ScreenUV * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f);
    return mul(float4(NDC, Depth, 1.0f), InverseViewProjection);
}

float3 ReconstructSurfaceNormal(int2 Pixel, float Depth, float3 PositionWS)
{
    int2 MinPixel = int2(ViewportOrigin);
    int2 MaxPixel = MinPixel + int2(round(1.0f / InverseViewportSize)) - 1;
    int2 Left = max(Pixel - int2(1, 0), MinPixel);
    int2 Right = min(Pixel + int2(1, 0), MaxPixel);
    int2 Up = max(Pixel - int2(0, 1), MinPixel);
    int2 Down = min(Pixel + int2(0, 1), MaxPixel);
    float DepthL = SceneDepth.Load(int3(Left, 0));
    float DepthR = SceneDepth.Load(int3(Right, 0));
    float DepthU = SceneDepth.Load(int3(Up, 0));
    float DepthD = SceneDepth.Load(int3(Down, 0));

    // Choose the closer depth on each axis to avoid crossing object silhouettes.
    float ErrorL = DepthL < 1.0f && Left.x != Pixel.x ? abs(DepthL - Depth) : 1e20f;
    float ErrorR = DepthR < 1.0f && Right.x != Pixel.x ? abs(DepthR - Depth) : 1e20f;
    float ErrorU = DepthU < 1.0f && Up.y != Pixel.y ? abs(DepthU - Depth) : 1e20f;
    float ErrorD = DepthD < 1.0f && Down.y != Pixel.y ? abs(DepthD - Depth) : 1e20f;
    if (min(ErrorL, ErrorR) >= 1e20f || min(ErrorU, ErrorD) >= 1e20f)
        return float3(0, 0, 0);

    float4 NeighborX = ReconstructWorldPosition(ErrorL <= ErrorR ? Left : Right, ErrorL <= ErrorR ? DepthL : DepthR);
    float4 NeighborY = ReconstructWorldPosition(ErrorU <= ErrorD ? Up : Down, ErrorU <= ErrorD ? DepthU : DepthD);
    if (abs(NeighborX.w) < 1e-6f || abs(NeighborY.w) < 1e-6f)
        return float3(0, 0, 0);
    return cross(NeighborX.xyz / NeighborX.w - PositionWS, NeighborY.xyz / NeighborY.w - PositionWS);
}

float3 ComputeProjectedNormalSlope(float3 Detail, float3 SurfaceNormal, FDecalInstance Decal)
{
    float NormalLengthSq = dot(SurfaceNormal, SurfaceNormal);
    if (NormalLengthSq <= 1e-30f)
        return float3(0, 0, 0);
    float3 N = SurfaceNormal * rsqrt(NormalLengthSq);
    float3 GradientU = float3(Decal.WorldToDecal[0][1], Decal.WorldToDecal[1][1], Decal.WorldToDecal[2][1]);
    float3 GradientV = -float3(Decal.WorldToDecal[0][2], Decal.WorldToDecal[1][2], Decal.WorldToDecal[2][2]);
    float3 T = cross(GradientV, N);
    float Det = dot(GradientU, T);
    if (abs(Det) <= max(1e-20f, sqrt(dot(GradientU, GradientU) * dot(GradientV, GradientV)) * 1e-5f))
        return float3(0, 0, 0);
    T *= sign(Det) * rsqrt(dot(T, T));
    float3 B = cross(N, T) * sign(Det);
    return (T * Detail.x + B * Detail.y) / max(Detail.z, 1e-3f);
}

struct PSOutput
{
    float4 Color : SV_TARGET0;
#if DECAL_WRITE_NORMAL
    float4 NormalSlope : SV_TARGET1;
#endif
};

PSOutput mainPS(PSInput Input)
{
    int2 Pixel = int2(Input.Position.xy);
    float Depth = SceneDepth.Load(int3(Pixel, 0));
    if (Depth >= 1.0f)
        discard;
    uint ReceiverStencil = DecalReceiverMask.Load(int3(Pixel, 0)).g;
    if ((ReceiverStencil & 0x80u) == 0u)
        discard;

    float4 WorldPosition = ReconstructWorldPosition(Pixel, Depth);
    if (abs(WorldPosition.w) < 1e-6f)
        discard;
    WorldPosition /= WorldPosition.w;
    FDecalInstance Decal = DecalInstances[Input.InstanceIndex];
    float3 LocalPosition = mul(WorldPosition, Decal.WorldToDecal).xyz;
    float2 DecalUV = float2(LocalPosition.y / (Decal.DecalSize.y * 2.0f) + 0.5f,
        0.5f - LocalPosition.z / (Decal.DecalSize.z * 2.0f));
    float2 DecalUVDX = ddx(DecalUV);
    float2 DecalUVDY = ddy(DecalUV);
    if (any(abs(LocalPosition) > Decal.DecalSize))
        discard;

    float3 NormalWS = ReconstructSurfaceNormal(Pixel, Depth, WorldPosition.xyz);
    float AngleFade = ComputeDecalAngleFade(NormalWS, Decal.ProjectionDirection, Decal.AngleFadeCosines);
    if (AngleFade <= 0.0f)
        discard;

    float4 Color = DecalTexture.SampleGrad(DecalSampler, DecalUV, DecalUVDX, DecalUVDY);
    float RangeT = saturate(LocalPosition.x / (Decal.DecalSize.x * 2.0f) + 0.5f);
    float RangeFade = 1.0f;
    if (Decal.ProjectionEndFade > 0.0001f)
    {
        float FadeStart = 1.0f - Decal.ProjectionEndFade;
        RangeFade = 1.0f - smoothstep(FadeStart, 1.0f, RangeT);
    }
    Color.rgb *= Decal.BaseColor.rgb;
    Color.a *= saturate(Decal.Opacity) * saturate(Decal.BaseColor.a) * RangeFade * AngleFade;
    if (Color.a <= 0.001f)
        discard;
    PSOutput Output;
    Output.Color = Color;
#if DECAL_WRITE_NORMAL
    float3 Slope = float3(0, 0, 0);
    if (Decal.bHasNormalMap != 0u)
    {
        float3 Detail = DecodeNormal(DecalNormalTexture.SampleGrad(DecalSampler, DecalUV, DecalUVDX, DecalUVDY).xyz,
            Decal.bFlipNormalGreen != 0u);
        Slope = ComputeProjectedNormalSlope(Detail, NormalWS, Decal);
    }
    // A color-only layer also attenuates normal detail beneath it in the same sorted order.
    Output.NormalSlope = float4(Slope, Color.a);
#endif
    return Output;
}
