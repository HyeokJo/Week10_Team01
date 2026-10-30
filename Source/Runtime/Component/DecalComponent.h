#pragma once

#include "Component/SceneComponent.h"

class FScene;
class FDecalSceneProxy;

class UDecalComponent : public USceneComponent
{
	DECLARE_CLASS(UDecalComponent, USceneComponent)
	REFLECT_START(UDecalComponent)
		PROPERTY(DecalSize)
		PROPERTY(TexturePath)
		PROPERTY(Opacity)
	REFLECT_END()

public:
	UDecalComponent() = default;
	~UDecalComponent() override = default;

	FDecalSceneProxy* GetSceneProxy() const { return SceneProxy; }

	void MarkSceneProxyDirty();

	void OnTransformDirty() override;
	void PostEditChangeProperty(const FProperty& Property) override;
	void SetHiddenInGame(bool bInHidden) override;

	using Super::Serialize;
	void Serialize(FStructuredArchive::FRecord Record) override;

	const FVector& GetDecalSize() const { return DecalSize; }
	const FString& GetTexturePath() const { return TexturePath; }
	const float GetOpacity() const { return Opacity; }

	FBox CalcLocalBounds() const override;

private:
	friend class FScene;

	void ValidateParameters();

	FDecalSceneProxy* SceneProxy = nullptr;

	FVector DecalSize = FVector(1.f, 1.f, 1.f);
	FString TexturePath;
	float Opacity = 1.f;
};