#pragma once

#include "SceneComponent.h"
#include "Render/Material.h"
class UDecalComponent : public USceneComponent
{
	DECLARE_CLASS(UDecalComponent, USceneComponent)
	REFLECT_START(UDecalComponent)
		PROPERTY(DecalSize)
		PROPERTY(Opacity)
		PROPERTY(FadeInDuration)
		PROPERTY(FadeOutDuration)
	REFLECT_END()
public:
	UDecalComponent();
	virtual ~UDecalComponent() override = default;

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime) override;

	void FadeIn();
	void FadeOut();

	const FVector& GetDecalSize() const { return DecalSize; }
	void SetDecalSize(const FVector& InSize);

	float GetOpacity() const { return Opacity * FadeOpacity; }
	void SetOpacity(float InOpacity);

	UMaterial* GetDecalMaterial() const { return DecalMaterial; }
	void SetDecalMaterial(UMaterial* InMaterial) { DecalMaterial = InMaterial; }

	virtual FBox CalcLocalBounds() const override;
	
	virtual void OnTransformDirty() override;

	virtual void PostEditChangeProperty(const FProperty& Property) override;

	virtual void Serialize(json& Handle, bool bIsLoading) override;

	using Super::Serialize;
	virtual void Serialize(FStructuredArchive::FRecord Record) override;
protected:
	FVector DecalSize = FVector(100.0f, 100.0f, 100.0f);
	float Opacity = 1.0f;
	
	float FadeInDuration = 0.3f;
	float FadeOutDuration = 0.3f;
	float FadeOpacity = 1.0f;
	float FadeStartOpacity = 1.0f;
	float FadeTargetOpacity = 1.0f;
	float FadeElapsed = 0.0f;
	float FadeDuration = 0.0f;
	bool bIsFading = false;

	UMaterial* DecalMaterial = nullptr;
private:
	void StartFade(float TargetOpacity, float Duration);
	void SetFadeTickEnabled(bool bEnable);
	void MarkDecalBoundsDirty();
};