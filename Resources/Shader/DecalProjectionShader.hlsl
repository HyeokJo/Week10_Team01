#pragma pack_matrix(row_major)

Texture2D<float> SceneDepth : register(t0);
Texture2D<float4> DecalTexture : register(t1);

SamplerState DecalSampler : register(s0);

cbuffer DecalConstants : register(b0)
{
    matrix InverseViewProjection;
    matrix WorldToDecal;

    float3 DecalSize;
    float Opacity;
    float4 BaseColor;
};

struct PSInput
{
    float4 Position : SV_POSITION;
    float2 UV : TEXCOORD0;
};

PSInput mainVS(uint VertexID : SV_VertexID)
{
    PSInput Output;
    float2 UV = float2((VertexID << 1) & 2, VertexID & 2);
    Output.Position = float4(UV * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    Output.UV = UV;
    return Output;
}

float4 mainPS(PSInput Input) : SV_TARGET
{
    float Depth = SceneDepth.Load(int3(Input.Position.xy, 0));

    if (Depth >= 1.0f)
        discard;

    float2 NDC = Input.UV * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f);

    float4 WorldPosition = mul(float4(NDC, Depth, 1.0f), InverseViewProjection);

    if (abs(WorldPosition.w) < 1e-6f)
        discard;

    WorldPosition /= WorldPosition.w;

    float4 LocalPosition = mul(WorldPosition, WorldToDecal);

    if (any(abs(LocalPosition.xyz) > DecalSize))
        discard;

    float2 DecalUV = float2(LocalPosition.y / (DecalSize.y * 2.0f) + 0.5f, 0.5f - LocalPosition.z / (DecalSize.z * 2.0f));

    float4 Color = DecalTexture.Sample(DecalSampler, DecalUV);
    Color.rgb *= BaseColor.rgb;
    Color.a *= saturate(Opacity) * saturate(BaseColor.a);

    if (Color.a <= 0.001f)
        discard;

    return Color;
}