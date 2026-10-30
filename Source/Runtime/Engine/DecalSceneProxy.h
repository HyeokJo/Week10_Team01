#pragma once

#include "Core/EngineString.h"
#include "Math/EngineMath.h"
#include "Math/Frustum.h"

class FScene;
class UDecalComponent;

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
	bool IsHiddenInGame() const { return bHiddenInGame; }

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
	bool bHiddenInGame = false;
};