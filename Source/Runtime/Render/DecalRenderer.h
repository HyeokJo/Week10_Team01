#pragma once

#include "Math/EngineMath.h"
#include "Render/Buffer.h"
#include "Render/PipelineState.h"

class FDecalSceneProxy;
struct FRenderPacket;

struct FDecalConstants
{
	FMatrix World;
	FMatrix ViewProjection;
	FMatrix WorldToDecal;

	FVector DecalSize;
	float Opacity;
};

static_assert(sizeof(FDecalConstants) == 208);

class FDecalRenderer
{
public:
	bool Init();

	void OnRender(const FRenderPacket& Receiver, const FDecalSceneProxy& Decal, const FMatrix& ViewProjection);

private:
	FPipelineState PipelineState;
	TUniquePtr<FConstantBuffer> ConstantBuffer;
	bool bReady = false;
};