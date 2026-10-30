#pragma once

#include "RenderingInfo.h"

class FTexture2D;

struct FSceneRenderTargets
{
	TUniquePtr<FTexture2D> SceneColor; // R16G16B16A16_FLOAT, Linear
	TUniquePtr<FTexture2D> ViewColor; // R8G8B8A8_UNORM
	TUniquePtr<FTexture2D> Depth; // R24G8_TYPELESS
	TUniquePtr<FTexture2D> FxaaTemp;
	FRenderingInfo SceneInfo; // SceneColor + Depth
	FRenderingInfo ViewInfo; // ViewColor + Depth
	uint32 Width = 0;
	uint32 Height = 0;

	void Resize(uint32 InWidth, uint32 InHeight);
	void Release();
};
