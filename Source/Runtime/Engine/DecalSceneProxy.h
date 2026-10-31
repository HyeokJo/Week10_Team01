#pragma once

#include "Core/EngineString.h"
#include "Math/EngineMath.h"
#include "Math/Frustum.h"
#include "Math/Color.h"

class FScene;
class UDecalComponent;
class UTexture2D;

class FDecalSceneProxy
{
public:
	explicit FDecalSceneProxy(UDecalComponent* InComponent) : Component(InComponent) {}

	void UpdateFromComponent();

	FScene* GetScene() const { return Scene; }

	const FMatrix& GetDecalToWorld() const { return DecalToWorld; }
	const FMatrix& GetWorldToDecal() const { return WorldToDecal; }
	const FVector& GetDecalSize() const { return DecalSize; }
	const FBox& GetBounds() const { return Bounds; }
	const FString& GetTexturePath() const { return TexturePath; }

	float GetOpacity() const { return Opacity; }
	const FLinearColor& GetDecalColor() const { return DecalColor; }
	int32 GetSortOrder() const { return SortOrder; }
	float GetFadeScreenSize() const { return FadeScreenSize; }
	bool IsHiddenInGame() const { return bHiddenInGame; }

	UTexture2D* GetTexture() const { return Texture; }

private:
	friend class FScene;

	FScene* Scene = nullptr;
	UDecalComponent* Component = nullptr;
	bool bQueuedForUpdate = false;

	FMatrix DecalToWorld;
	FMatrix WorldToDecal;
	FVector DecalSize = FVector(1.0f, 1.0f, 1.0f);
	FBox Bounds{};
	FString TexturePath;
	float Opacity = 1.0f;
	FLinearColor DecalColor = FLinearColor(1.f, 1.f, 1.f, 1.f);
	int32 SortOrder = 0;
	float FadeScreenSize = 0.f;
	bool bHiddenInGame = false;

	UTexture2D* Texture = nullptr;
};
