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
};
static_assert(sizeof(FDecalConstants) == 160);

class FDecalRenderer
{
public:
    bool Init();
    void OnRender(FTexture2D* DepthTexture, const FMatrix& ViewProjection, const UDecalComponent& Decal);
private:
    FPipelineState PipelineState;
    FShaderProgram* Shader = nullptr;
    TUniquePtr<FConstantBuffer> ConstantBuffer;
};