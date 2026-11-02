#ifndef DECAL_COMMON_HLSLI
#define DECAL_COMMON_HLSLI

float ComputeDecalAngleFade(float3 NormalWS, float3 ProjectionDirection, float2 FadeCosines)
{
    float LengthSq = dot(NormalWS, NormalWS);
    // No reliable depth tangent at a viewport edge or on a subpixel surface.
    if (LengthSq <= 1e-30f)
        return 1.0f;
    float Alignment = abs(dot(NormalWS, ProjectionDirection)) * rsqrt(LengthSq);
    return smoothstep(FadeCosines.x, FadeCosines.y, Alignment);
}

#endif
