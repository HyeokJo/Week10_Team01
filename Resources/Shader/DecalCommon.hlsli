#ifndef DECAL_COMMON_HLSLI
#define DECAL_COMMON_HLSLI

float3 DecodeNormal(float3 Sample, bool bFlipGreen)
{
    float3 N = normalize(Sample * 2.0f - 1.0f);
    N.y = bFlipGreen ? -N.y : N.y;
    return N;
}

float3 BlendReorientedNormals(float3 Base, float3 Detail)
{
    float3 T = Base + float3(0.0f, 0.0f, 1.0f);
    if (T.z < 1e-5f)
        return float3(Detail.x, -Detail.y, -Detail.z);
    float3 U = Detail * float3(-1.0f, -1.0f, 1.0f);
    return normalize(T * (dot(T, U) / T.z) - U);
}

float3 ApplyDecalNormalSlope(float3 BaseNormal, float3 GeoNormal, float4 SlopeAndCoverage)
{
    if (SlopeAndCoverage.a <= 1e-5f)
        return BaseNormal;
    // The buffer contains alpha-premultiplied world-space slopes, not RGB-encoded normals.
    float3 Slope = SlopeAndCoverage.xyz / SlopeAndCoverage.a;
    Slope -= GeoNormal * dot(Slope, GeoNormal);
    float LengthSq = dot(Slope, Slope);
    if (LengthSq <= 1e-12f)
        return BaseNormal;
    float3 T = Slope * rsqrt(LengthSq);
    float3 B = cross(GeoNormal, T);
    float3 BaseTS = float3(dot(BaseNormal, T), dot(BaseNormal, B), dot(BaseNormal, GeoNormal));
    float3 DetailTS = normalize(float3(sqrt(LengthSq), 0.0f, 1.0f));
    float3 Combined = BlendReorientedNormals(BaseTS, DetailTS);
    float3 Reoriented = Combined.x * T + Combined.y * B + Combined.z * GeoNormal;
    return normalize(lerp(BaseNormal, Reoriented, saturate(SlopeAndCoverage.a)));
}

float ComputeDecalAngleFade(float3 NormalWS, float3 ProjectionDirection, float2 FadeCosines)
{
    float LengthSq = dot(NormalWS, NormalWS);
    // No reliable normal means there is no front-facing receiver to establish.
    if (LengthSq <= 1e-30f)
        return 0.0f;
    // The receiver normal must oppose the direction the decal projects into the surface.
    float Alignment = -dot(NormalWS, ProjectionDirection) * rsqrt(LengthSq);
    if (Alignment <= 0.0f)
        return 0.0f;
    return smoothstep(FadeCosines.x, FadeCosines.y, Alignment);
}

#endif
