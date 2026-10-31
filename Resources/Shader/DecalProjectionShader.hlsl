#pragma pack_matrix(row_major)

Texture2D<float> SceneDepth : register(t0);
Texture2D<float4> DecalTexture : register(t1);
Texture2D<uint2> DecalReceiverMask : register(t2);
SamplerState DecalSampler : register(s0);

cbuffer DecalConstants : register(b0)
{
    matrix InverseViewProjection;
    matrix WorldToDecal;

    float3 DecalSize;
    float Opacity;
    float4 BaseColor;
    
    float ProjectionEndFade;
    float3 Padding;
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
    
    uint ReceiverStencil = DecalReceiverMask.Load(int3(Input.Position.xy, 0)).g;

    if ((ReceiverStencil & 0x80u) == 0u)
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
    float RangeT = saturate(LocalPosition.x / (DecalSize.x * 2.0f) + 0.5f);
    float RangeFade = 1.0f;
    if(ProjectionEndFade > 0.0001f)
    {
        float FadeStart = 1.0f - ProjectionEndFade;
        RangeFade = 1.0f - smoothstep(FadeStart, 1.0f, RangeT);
    }
    Color.rgb *= BaseColor.rgb;
    Color.a *= saturate(Opacity) * saturate(BaseColor.a) * RangeFade;

    if (Color.a <= 0.001f)
        discard;

    return Color;
}