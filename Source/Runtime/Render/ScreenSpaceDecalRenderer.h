#pragma once

#include "Buffer.h"
#include "PipelineState.h"
#include "Containers/Array.h"

#include "Math/Color.h"
class FDecalSceneProxy;
class FTexture2D;
class UTexture2D;
struct FViewportSettings;

struct FScreenSpaceDecalViewConstants
{
    FMatrix InverseViewProjection;
    FVector2 ViewportOrigin;
    FVector2 InverseViewportSize;
    uint32 bHasReceiverNormals = 0;
    uint32 Padding[3] = {};
};
static_assert(sizeof(FScreenSpaceDecalViewConstants) == 96);

struct FScreenSpaceDecalInstance
{
    FMatrix WorldToDecal;
    FVector DecalSize;
    float Opacity;
    FLinearColor BaseColor;
    FVector4 ScreenRect;
    FVector ProjectionDirection;
    float ProjectionEndFade;
    FVector2 AngleFadeCosines;
    uint32 bHasNormalMap;
    uint32 bFlipNormalGreen;
};
static_assert(sizeof(FScreenSpaceDecalInstance) == 144);

class FScreenSpaceDecalRenderer
{
public:
    bool Init();
    void OnRender(FTexture2D* DepthTexture, const FMatrix& ViewProjection, const TArray<const FDecalSceneProxy*>& Decals,
        const FViewportSettings& Viewport, bool bOrthographic, bool bWriteNormals = false, FTexture2D* ReceiverNormals = nullptr);
private:
    struct FBatch
    {
        UTexture2D* Texture;
        UTexture2D* NormalTexture;
        ESamplerState Sampler;
        uint32 FirstInstance;
        uint32 InstanceCount;
    };

    bool EnsureInstanceCapacity(uint32 Count);
    FPipelineState PipelineState;
    FShaderProgram* Shader = nullptr;
    FShaderProgram* ColorOnlyShader = nullptr;
    TUniquePtr<FConstantBuffer> ViewConstantBuffer;
    TUniquePtr<FConstantBuffer> BatchConstantBuffer;
    ComPtr<ID3D11Buffer> InstanceBuffer;
    ComPtr<ID3D11ShaderResourceView> InstanceSRV;
    uint32 InstanceCapacity = 0;
    TArray<FScreenSpaceDecalInstance> Instances;
    TArray<FBatch> Batches;
};
