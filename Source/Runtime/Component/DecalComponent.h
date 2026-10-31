#pragma once

#include "Component/SceneComponent.h"
#include "Math/Color.h"

class FScene;
class FDecalSceneProxy;

class UDecalComponent : public USceneComponent
{
	DECLARE_CLASS(UDecalComponent, USceneComponent)
	REFLECT_START(UDecalComponent)
		PROPERTY(DecalSize)
		PROPERTY(TexturePath)
		PROPERTY(Opacity)
		PROPERTY(DecalColor)
		PROPERTY(FadeInStartDelay)
		PROPERTY(FadeInDuration)
		PROPERTY(FadeStartDelay)
		PROPERTY(FadeDuration)
		PROPERTY(bDestroyOwnerAfterFade)
	REFLECT_END()

public:
	UDecalComponent();
	~UDecalComponent() override = default;

	void BeginPlay() override;
	void EndPlay() override;
	void TickComponent(float DeltaTime) override;

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
	const FLinearColor& GetDecalColor() const { return DecalColor; }
	float GetRenderOpacity() const { return Opacity * GetFadeAlpha(); }

	FBox CalcLocalBounds() const override;

private:
	friend class FScene;

	void ValidateParameters();
	float GetFadeAlpha() const;

	FDecalSceneProxy* SceneProxy = nullptr;

	FVector DecalSize = FVector(1.f, 1.f, 1.f);
	FString TexturePath;
	float Opacity = 1.f;
	FLinearColor DecalColor = FLinearColor(1.f, 1.f, 1.f, 1.f);

	// Both delays are measured from BeginPlay. Zero duration makes an instant transition.
	float FadeInStartDelay = 0.f;
	float FadeInDuration = 0.f;
	// Fade-out is disabled when both its delay and duration are zero.
	float FadeStartDelay = 0.f;
	float FadeDuration = 0.f;
	bool bDestroyOwnerAfterFade = true;

	// Runtime state is not reflected or saved into a scene.
	float ElapsedTime = 0.f;
	bool bFadePlaying = false;
};