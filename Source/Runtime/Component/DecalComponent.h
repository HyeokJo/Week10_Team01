#pragma once

#include "Component/SceneComponent.h"

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

	const FVector& GetDecalSize() const { return DecalSize; }
	const FString& GetTexturePath() const { return TexturePath; }
	const float GetOpacity() const { return Opacity; }

	FBox CalcLocalBounds() const override;

private:
	FVector DecalSize = FVector(1.f, 1.f, 1.f);
	FString TexturePath;
	float Opacity = 1.f;
};