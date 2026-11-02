#pragma once

#include "Math/EngineMath.h"
#include "Math/Color.h"
#include "Render/Buffer.h"
#include "Render/PipelineState.h"

class FDecalSceneProxy;
struct FRenderPacket;
struct FSceneView;

struct FDecalConstants
{
	FMatrix World;
	FMatrix ViewProjection;
	FMatrix WorldToDecal;
	FMatrix NormalMatrix;

	FVector DecalSize;
	float Opacity;
	FLinearColor DecalColor;
	FVector CameraPosition;
	float Shininess;
	FVector SpecularColor;
	uint32 bLit;
	uint32 LightingModel;
	uint32 bReceiveShadow;
	uint32 bHasReceiverNormalMap;
	uint32 bHasDecalNormalMap;
	FVector2 ReceiverUVOffset;
	uint32 bFlipReceiverNormalGreen;
	uint32 bFlipDecalNormalGreen;
};

static_assert(sizeof(FDecalConstants) == 352);

class FDecalRenderer
{
public:
	bool Init();

	static float CalculateScreenFade(const FDecalSceneProxy& Decal, const FMatrix& ViewProjection, bool bOrthographic);
	void OnRender(const FRenderPacket& Receiver, const FDecalSceneProxy& Decal, const FSceneView& SceneView, float ScreenFade);

private:
	FPipelineState PipelineState;
	TUniquePtr<FConstantBuffer> ConstantBuffer;
	bool bReady = false;
};
