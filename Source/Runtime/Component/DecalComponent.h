#pragma once

#include "SceneComponent.h"

class UDecalComponent : public USceneComponent
{
	DECLARE_CLASS(UDecalComponent, USceneComponent)
	REFLECT_START(UDecalComponent)
		PROPERTY(DecalSize)
		PROPERTY(Opacity)
		PROPERTY(TexturePath)
	REFLECT_END()
public:
	UDecalComponent() = default;
	virtual ~UDecalComponent() override = default;

	const FVector& GetDecalSize() const { return DecalSize; }
	void SetDecalSize(const FVector& InSize);

	float GetOpacity() const { return Opacity; }
	void SetOpacity(float InOpacity);

	const FString& GetTexturePath() const { return TexturePath; }
	void SetTexturePath(const FString& InPath) { TexturePath = InPath; }

	virtual void PostEditChangeProperty(const FProperty& Property) override;
protected:
	FVector DecalSize = FVector(100.0f, 100.0f, 100.0f);
	float Opacity = 1.0f;
	FString TexturePath;
};