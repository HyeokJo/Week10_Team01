#pragma once

#include "Buffer.h"
#include "PipelineState.h"
#include "Render/Renderer.h"
#include "Math/Color.h"
class UDecalComponent;
class FTexture2D;

struct FDecalConstants
{
    FMatrix InverseViewProjection;
    FMatrix WorldToDecal;

    FVector DecalSize;
    float Opacity;

    FLinearColor BaseColor;

    float ProjectionEndFade;
    FVector Padding;
};
static_assert(sizeof(FDecalConstants) == 176);

class FDecalRenderer
{
public:
    bool Init();
    bool OnRender(FTexture2D* DepthTexture, const FMatrix& ViewProjection, const UDecalComponent& Decal);
private:
    FPipelineState PipelineState;
    FShaderProgram* Shader = nullptr;
    TUniquePtr<FConstantBuffer> ConstantBuffer;
};