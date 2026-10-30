#pragma once

#include "SceneComponent.h"
#include "Render/Material.h"
class UDecalComponent : public USceneComponent
{
	DECLARE_CLASS(UDecalComponent, USceneComponent)
	REFLECT_START(UDecalComponent)
		PROPERTY(DecalSize)
		PROPERTY(Opacity)
	REFLECT_END()
public:
	UDecalComponent() = default;
	virtual ~UDecalComponent() override = default;

	const FVector& GetDecalSize() const { return DecalSize; }
	void SetDecalSize(const FVector& InSize);

	float GetOpacity() const { return Opacity; }
	void SetOpacity(float InOpacity);

	UMaterial* GetDecalMaterial() const { return DecalMaterial; }
	void SetDecalMaterial(UMaterial* InMaterial) { DecalMaterial = InMaterial; }

	virtual FBox CalcLocalBounds() const override;

	virtual void PostEditChangeProperty(const FProperty& Property) override;

	virtual void Serialize(json& Handle, bool bIsLoading) override;

	using Super::Serialize;
	virtual void Serialize(FStructuredArchive::FRecord Record) override;
protected:
	FVector DecalSize = FVector(100.0f, 100.0f, 100.0f);
	float Opacity = 1.0f;
	UMaterial* DecalMaterial = nullptr;
};