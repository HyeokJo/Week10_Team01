#pragma pack_matrix(row_major)

// EViewMode 순서와 일치해야 한다.
#define VIEWMODE_LIT          0
#define VIEWMODE_UNLIT        1
#define VIEWMODE_WORLDNORMAL  3

// EShadingModel 순서와 일치해야 한다.
#define SHADING_MODEL_GOURAUD  0
#define SHADING_MODEL_PHONG    1

// 기본값: Lit, Blinn-Phong, Phong
#ifndef VIEWMODE
#define VIEWMODE VIEWMODE_LIT
#endif

#ifndef LIGHTING_MODEL
#define LIGHTING_MODEL LIGHTING_MODEL_BLINNPHONG
#endif

#ifndef SHADING_MODEL
#define SHADING_MODEL SHADING_MODEL_PHONG
#endif

#ifndef HAS_NORMAL_MAP
#define HAS_NORMAL_MAP 0
#endif
#define USE_NORMAL_MAP (SHADING_MODEL == SHADING_MODEL_PHONG && HAS_NORMAL_MAP)

#ifndef RECEIVE_SHADOW
#define RECEIVE_SHADOW 1
#endif 

#define GOURAUD (VIEWMODE == VIEWMODE_LIT && SHADING_MODEL == SHADING_MODEL_GOURAUD)

struct FVSInput
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float4 Tangent : TANGENT;
    float4 Color : COLOR; // 지금은 쓰지 않지만 나중에 쓰게 될 수 있음
    float2 UV : TEXCOORD;
};

struct FPSInput
{
    float4 Position : SV_POSITION;
    float3 Normal : NORMAL;
    float2 UV : TEXCOORD0;
    float3 WorldPosition : TEXCOORD1;
#if USE_NORMAL_MAP
    float3 Tangent : TANGENT;
    int BitangentSign : BITANGENT_SIGN;
#endif
#if GOURAUD
    float3 DiffuseLight : TEXCOORD2;
    float3 SpecularLight : TEXCOORD3;
#endif
};

cbuffer ViewConstants : register(b0)
{
    matrix ViewProjection;
    float3 CameraPosition;
    float ViewPadding;
};

cbuffer StaticMeshMaterialParams : register(b1)
{
    float4 BaseColor;
    float2 UVScrollOffset;
    uint bOpaque;
    float Shininess;
    float3 SpecularColor;
    uint bFlipNormalGreen;
};

cbuffer PerObjectConstants : register(b2)
{
    matrix World;
    matrix NormalMatrix; // World의 역전치
};

Texture2D AlbedoTexture : register(t0);

#if USE_NORMAL_MAP
Texture2D NormalTexture : register(t1);
#endif

SamplerState AlbedoSampler : register(s0);

#include "LightingCommon.hlsli"

cbuffer DecalCompositeConstants : register(b7)
{
    uint bEnableDecalBaseColor;
    uint3 DecalCompositePadding;
};
Texture2D<float4> DecalBaseColorTexture : register(t13);

FPSInput mainVS(FVSInput Input)
{
    FPSInput Output;

    float4 WorldPosition = mul(float4(Input.Position, 1.0f), World);

    Output.Position = mul(WorldPosition, ViewProjection);
    Output.WorldPosition = WorldPosition.xyz;
    Output.Normal = mul(Input.Normal, (float3x3) NormalMatrix);
#if USE_NORMAL_MAP
    Output.Tangent = mul(Input.Tangent.xyz, (float3x3) World);
    Output.BitangentSign = Input.Tangent.w;
#endif
    Output.UV = Input.UV;
#if GOURAUD
    float3 N = normalize(Output.Normal);
    float3 V = normalize(CameraPosition - Output.WorldPosition);
    // TODO
    ComputeLighting(N, Output.WorldPosition, V, Shininess, Output.DiffuseLight, Output.SpecularLight);
    
#endif
    return Output;
}

float4 mainPS(FPSInput Input) : SV_TARGET
{
    float4 FinalColor = float4(0.0f, 0.0f, 0.0f, 1.0f);

    float4 TextureColor = AlbedoTexture.Sample(AlbedoSampler, Input.UV + UVScrollOffset);
    float4 SurfaceColor = TextureColor * BaseColor;

    float3 Albedo = SurfaceColor.rgb;
    float Alpha = bOpaque ? 1.0f : SurfaceColor.a;

    if (bEnableDecalBaseColor != 0u)
    {
        float4 DecalColor = DecalBaseColorTexture.Load(int3(int2(Input.Position.xy), 0));
        Albedo = DecalColor.rgb + Albedo * (1.0f - DecalColor.a);
    }

    float3 Kd = Albedo;
    float3 Ka = Kd; // 나중에 Ka 를 개별로 설정할 수 있음
    float3 Ks = SpecularColor;
    
    float3 Normal = normalize(Input.Normal);
    
#if USE_NORMAL_MAP
    // 그람 슈미트 직교화
    float3 Tangent = normalize(Input.Tangent - dot(Input.Tangent, Normal) * Normal);
    float3 BiTangent = cross(Normal, Tangent) * Input.BitangentSign;
    float3x3 TBN = float3x3(Tangent, BiTangent, Normal);
    // Texture Normal 매핑
    float3 NormalSample = NormalTexture.Sample(AlbedoSampler, Input.UV + UVScrollOffset).xyz;
    float3 TextureNormal = normalize(NormalSample * 2.0f - 1.0f);
    TextureNormal.y = bFlipNormalGreen ? -TextureNormal.y : TextureNormal.y;
    // TBN 행렬을 이용하여 월드 공간으로 변환
    float3 WorldNormal = mul(TextureNormal, TBN);
#else
    float3 WorldNormal = Normal;
#endif
   
#if VIEWMODE == VIEWMODE_UNLIT
    FinalColor = float4(Albedo, Alpha);
#elif VIEWMODE == VIEWMODE_WORLDNORMAL
    // 색이 아니라 데이터이므로 Gamma Encode Pass에서 인코딩하지 않는다.
    FinalColor = float4(normalize(WorldNormal) * 0.5f + 0.5f, 1.0f);
#else // Lit
    if (bVisualizeLightCount != 0u)
    {
        uint2 PixelCoord = uint2(Input.Position.xy);
        uint2 TileCoord = min(PixelCoord / 16u, uint2(TileCountX - 1u, TileCountY - 1u));
        uint TileIndex = TileCoord.y * TileCountX + TileCoord.x;
        
        uint PointListStart = TileIndex * PointTileStride;
        uint SpotListStart = TileIndex * SpotTileStride;
        
        uint PointCount = min(PointTileList[PointListStart], 64u);
        uint SpotCount = min(SpotTileList[SpotListStart], 64u);
        
        float3 Color = GetLightCountColor(PointCount + SpotCount);
        
        uint2 LocalCoord = PixelCoord % 16u;

        if (LocalCoord.x == 0u || LocalCoord.y == 0u)
        {
            Color *= 0.2f;
        }

        return float4(Color, 1.0f);
    }
    
    if (bVisualizeCascades != 0u)
    {
        // Cascade 0: Red, Cascade 1: Green, Cascade 2: Blue, Cascade 3: Yellow, Beyond: Gray
        float3 CascadeColors[5] = {
            float3(1.0f, 0.25f, 0.25f), // 0: Red
            float3(0.25f, 1.0f, 0.25f), // 1: Green
            float3(0.25f, 0.45f, 1.0f), // 2: Blue
            float3(1.0f, 1.0f, 0.25f),  // 3: Yellow
            float3(0.35f, 0.35f, 0.35f) // 4: Beyond
        };

        int CascadeIdx;
        int NextCascadeIdx;
        float BlendFactor;
        GetCascadeBlendInfo(Input.WorldPosition, CascadeIdx, NextCascadeIdx, BlendFactor);

#if USE_NORMAL_MAP
        float3 ShadowNormal = Normal;
#else
        float3 ShadowNormal = WorldNormal;
#endif
        float Shadow = ComputeDirectionalShadowFactor(Input.WorldPosition, ShadowNormal, Input.Position.xy);

        float3 BaseColor = CascadeColors[min(CascadeIdx, 4)];
        if (BlendFactor > 0.0f && CascadeIdx < NumCascades - 1)
        {
            float3 NextColor = CascadeColors[min(NextCascadeIdx, 4)];
            BaseColor = lerp(BaseColor, NextColor, BlendFactor);
        }

        float3 Color = BaseColor * (0.35f + 0.65f * Shadow);
        return float4(Color, 1.0f);
    }
    
    float3 DiffuseLight;
    float3 SpecularLight;
#if SHADING_MODEL == SHADING_MODEL_GOURAUD
    DiffuseLight = Input.DiffuseLight;
    SpecularLight = Input.SpecularLight;
#else // Phong
    float3 N = normalize(WorldNormal);
    float3 V = normalize(CameraPosition - Input.WorldPosition);
    
#if USE_NORMAL_MAP
    ComputeTiledLighting(N, Normal, Input.WorldPosition, V, Shininess, Input.Position.xy, DiffuseLight, SpecularLight, LIGHTING_MODEL, true, true);
#else
    ComputeTiledLighting(N, Input.WorldPosition, V, Shininess, Input.Position.xy, DiffuseLight, SpecularLight, LIGHTING_MODEL, true, false);
#endif
    
#endif
    FinalColor = float4(Ka * AmbientLight.Color + Kd * DiffuseLight + Ks * SpecularLight, Alpha);
#endif
    return FinalColor;
}
