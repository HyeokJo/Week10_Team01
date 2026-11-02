#pragma once

#include "Component/SceneComponent.h"
#include "Math/Color.h"

class FScene;
class FDecalSceneProxy;
class UMaterial;
class FLineBatcher;

class UDecalComponent : public USceneComponent
{
	DECLARE_CLASS(UDecalComponent, USceneComponent)
	REFLECT_START(UDecalComponent)
		PROPERTY(DecalSize)
		PROPERTY(TexturePath)
		PROPERTY(NormalTexturePath)
		PROPERTY(bFlipNormalGreen)
		PROPERTY(Opacity)
		PROPERTY(DecalColor)
		PROPERTY(SortOrder)
		PROPERTY(FadeScreenSize)
		PROPERTY(FadeInStartDelay)
		PROPERTY(FadeInDuration)
		PROPERTY(FadeStartDelay)
		PROPERTY(FadeDuration)
		PROPERTY(bDestroyOwnerAfterFade)
		PROPERTY(FadeOutDuration)
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
	void SetDecalSize(const FVector& InSize);
	void SetOpacity(float InOpacity);
	void FadeIn();
	void FadeOut();
	void SetRenderAsUnlit(bool bEnable) { bRenderAsUnlit = bEnable; MarkSceneProxyDirty(); }
	bool IsRenderAsUnlit() const { return bRenderAsUnlit; }
	UMaterial* GetDecalMaterial() const { return DecalMaterial; }
	void SetDecalMaterial(UMaterial* InMaterial) { DecalMaterial = InMaterial; MarkSceneProxyDirty(); }
	void SetProjectionEndFade(float InFade) { ProjectionEndFade = FMath::Clamp(InFade, 0.f, 0.99f); MarkSceneProxyDirty(); }
	float GetProjectionEndFade() const { return ProjectionEndFade; }
	void DrawDebug(FLineBatcher* LineBatcher) const;

	using Super::Serialize;
	void Serialize(json& Handle, bool bIsLoading) override;
	void Serialize(FStructuredArchive::FRecord Record) override;

	const FVector& GetDecalSize() const { return DecalSize; }
	const FString& GetTexturePath() const { return TexturePath; }
	void SetTexturePath(const FString& InPath) { TexturePath = InPath; MarkSceneProxyDirty(); }
	const FString& GetNormalTexturePath() const { return NormalTexturePath; }
	bool IsNormalGreenFlipped() const { return bFlipNormalGreen; }
	const float GetOpacity() const { return Opacity; }
	const FLinearColor& GetDecalColor() const { return DecalColor; }
	float GetRenderOpacity() const { return Opacity * GetFadeAlpha() * ManualFadeOpacity; }
	int32 GetSortOrder() const { return SortOrder; }
	float GetFadeScreenSize() const { return FadeScreenSize; }
	void SetFadeScreenSize(float InFadeScreenSize)
	{
		InFadeScreenSize = FMath::Max(InFadeScreenSize, 0.f);
		if (FadeScreenSize == InFadeScreenSize)
			return;
		FadeScreenSize = InFadeScreenSize;
		MarkSceneProxyDirty();
	}
	void SetSortOrder(int32 InSortOrder)
	{
		if (SortOrder == InSortOrder)
			return;
		SortOrder = InSortOrder;
		MarkSceneProxyDirty();
	}

	FBox CalcLocalBounds() const override;

private:
	friend class FScene;

	void ValidateParameters();
	float GetFadeAlpha() const;
	void StartManualFade(float TargetOpacity, float Duration);

	FDecalSceneProxy* SceneProxy = nullptr;

	FVector DecalSize = FVector(1.f, 1.f, 1.f);
	FString TexturePath;
	FString NormalTexturePath;
	bool bFlipNormalGreen = false;
	float Opacity = 1.f;
	FLinearColor DecalColor = FLinearColor(1.f, 1.f, 1.f, 1.f);
	int32 SortOrder = 0;
	// Approximate projected diameter / view height. Zero disables screen-size fading.
	float FadeScreenSize = 0.f;

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
	UMaterial* DecalMaterial = nullptr;
	float ProjectionEndFade = 0.f;
	bool bRenderAsUnlit = false;
	float FadeOutDuration = 0.3f;
	float ManualFadeOpacity = 1.f;
	float ManualFadeStartOpacity = 1.f;
	float ManualFadeTargetOpacity = 1.f;
	float ManualFadeElapsed = 0.f;
	float ManualFadeDuration = 0.f;
	bool bManualFadePlaying = false;
};
