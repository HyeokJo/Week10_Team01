#pragma once

#include "Buffer.h"
#include "PipelineState.h"

#include "Math/Color.h"
class FDecalSceneProxy;
class FTexture2D;
struct FViewportSettings;

struct FScreenSpaceDecalConstants
{
    FMatrix InverseViewProjection;
    FMatrix WorldToDecal;

    FVector DecalSize;
    float Opacity;

    FLinearColor BaseColor;

    float ProjectionEndFade;
    FVector Padding;
};
static_assert(sizeof(FScreenSpaceDecalConstants) == 176);

class FScreenSpaceDecalRenderer
{
public:
    bool Init();
    bool OnRender(FTexture2D* DepthTexture, const FMatrix& ViewProjection, const FDecalSceneProxy& Decal, const FViewportSettings& Viewport, float ScreenFade);
private:
    FPipelineState PipelineState;
    FShaderProgram* Shader = nullptr;
    TUniquePtr<FConstantBuffer> ConstantBuffer;
};