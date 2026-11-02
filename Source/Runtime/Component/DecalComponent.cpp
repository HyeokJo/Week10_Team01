#include "EnginePCH.h"
#include "DecalComponent.h"
#include "Engine/Scene.h"
#include "Engine/DecalSceneProxy.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Render/Material.h"
#include "Render/LineBatcher.h"

UDecalComponent::UDecalComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UDecalComponent::BeginPlay()
{
	Super::BeginPlay();
	ValidateParameters();
	ElapsedTime = 0.f;
	ManualFadeOpacity = 1.f;
	bManualFadePlaying = false;
	bFadePlaying = true;
	MarkSceneProxyDirty();
}

void UDecalComponent::EndPlay()
{
	bFadePlaying = false;
	bManualFadePlaying = false;
	ElapsedTime = 0.f;
	MarkSceneProxyDirty();
	Super::EndPlay();
}

void UDecalComponent::TickComponent(float DeltaTime)
{
	Super::TickComponent(DeltaTime);
	if (bManualFadePlaying)
	{
		ManualFadeElapsed += FMath::Max(DeltaTime, 0.f);
		const float Alpha = FMath::Clamp(ManualFadeElapsed / ManualFadeDuration, 0.f, 1.f);
		ManualFadeOpacity = ManualFadeStartOpacity + (ManualFadeTargetOpacity - ManualFadeStartOpacity) * Alpha;
		bManualFadePlaying = Alpha < 1.f;
		MarkSceneProxyDirty();
	}
	if (!bFadePlaying)
		return;

	const float PreviousAlpha = GetFadeAlpha();
	ElapsedTime += FMath::Max(DeltaTime, 0.f);
	if (PreviousAlpha != GetFadeAlpha())
		MarkSceneProxyDirty();

	const bool bFadeOutEnabled = FadeStartDelay > 0.f || FadeDuration > 0.f;
	if (bDestroyOwnerAfterFade && bFadeOutEnabled && ElapsedTime >= FadeStartDelay + FadeDuration)
	{
		AActor* Owner = GetOwner();
		UWorld* World = Owner ? Owner->GetWorld() : nullptr;
		if (World && World->GetWorldType() != EWorldType::Editor)
			World->RequestDestroyActor(Owner);
	}
}

float UDecalComponent::GetFadeAlpha() const
{
	if (!bFadePlaying)
		return 1.f;

	float FadeInAlpha = 1.f;
	if (ElapsedTime < FadeInStartDelay)
		FadeInAlpha = 0.f;
	else if (FadeInDuration > 0.f)
		FadeInAlpha = FMath::Clamp((ElapsedTime - FadeInStartDelay) / FadeInDuration, 0.f, 1.f);

	float FadeOutAlpha = 1.f;
	if (FadeStartDelay > 0.f || FadeDuration > 0.f)
	{
		if (ElapsedTime >= FadeStartDelay)
		{
			FadeOutAlpha = FadeDuration > 0.f
				? FMath::Clamp(1.f - (ElapsedTime - FadeStartDelay) / FadeDuration, 0.f, 1.f)
				: 0.f;
		}
	}

	return FMath::Min(FadeInAlpha, FadeOutAlpha);
}

void UDecalComponent::MarkSceneProxyDirty()
{
	if (SceneProxy)
	{
		SceneProxy->GetScene()->MarkDecalDirty(SceneProxy);
	}
}

void UDecalComponent::OnTransformDirty()
{
	Super::OnTransformDirty();
	MarkSceneProxyDirty();
}

void UDecalComponent::PostEditChangeProperty(const FProperty& Property)
{
	Super::PostEditChangeProperty(Property);

	ValidateParameters();
	MarkSceneProxyDirty();
}

void UDecalComponent::SetHiddenInGame(bool bInHidden)
{
	Super::SetHiddenInGame(bInHidden);
	MarkSceneProxyDirty();
}

void UDecalComponent::Serialize(FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record);
	if (auto Slot = Record.TryEnterField("DecalMaterial", true))
		UMaterial::SerializeMaterialReference(Slot.GetValue(), DecalMaterial);
	if (Record.GetUnderlyingArchive().IsLoading())
	{
		ValidateParameters();
		ElapsedTime = 0.f;
		bFadePlaying = false;
		ManualFadeOpacity = 1.f;
		bManualFadePlaying = false;
		MarkSceneProxyDirty();
	}
}

FBox UDecalComponent::CalcLocalBounds() const
{
	return FBox(-DecalSize, DecalSize);
}

void UDecalComponent::ValidateParameters()
{
	DecalSize.X = FMath::Max(DecalSize.X, 0.01f);
	DecalSize.Y = FMath::Max(DecalSize.Y, 0.01f);
	DecalSize.Z = FMath::Max(DecalSize.Z, 0.01f);
	Opacity = FMath::Clamp(Opacity, 0.0f, 1.0f);
	FadeScreenSize = FMath::Max(FadeScreenSize, 0.f);
	AngleFadeStart = FMath::Clamp(AngleFadeStart, 0.f, 89.9f);
	AngleFadeEnd = FMath::Clamp(AngleFadeEnd, AngleFadeStart + 0.1f, 90.f);
	DecalColor.R = FMath::Max(DecalColor.R, 0.f);
	DecalColor.G = FMath::Max(DecalColor.G, 0.f);
	DecalColor.B = FMath::Max(DecalColor.B, 0.f);
	DecalColor.A = FMath::Clamp(DecalColor.A, 0.f, 1.f);
	FadeInStartDelay = FMath::Max(FadeInStartDelay, 0.f);
	FadeInDuration = FMath::Max(FadeInDuration, 0.f);
	FadeStartDelay = FMath::Max(FadeStartDelay, 0.f);
	FadeDuration = FMath::Max(FadeDuration, 0.f);
	FadeOutDuration = FMath::Max(FadeOutDuration, 0.f);
}

void UDecalComponent::Serialize(json& Handle, bool bIsLoading)
{
	Super::Serialize(Handle, bIsLoading);
	if (bIsLoading)
	{
		if (Handle.contains("DecalMaterial"))
			DecalMaterial = Handle["DecalMaterial"].is_object() ? UMaterial::LoadMaterial(Handle["DecalMaterial"]) : nullptr;
		ValidateParameters();
		MarkSceneProxyDirty();
	}
	else
		Handle["DecalMaterial"] = DecalMaterial ? UMaterial::SaveMaterial(DecalMaterial) : json(nullptr);
}

void UDecalComponent::SetDecalSize(const FVector& InSize)
{
	DecalSize = InSize;
	ValidateParameters();
	MarkSceneProxyDirty();
}

void UDecalComponent::SetOpacity(float InOpacity)
{
	Opacity = FMath::Clamp(InOpacity, 0.f, 1.f);
	MarkSceneProxyDirty();
}

void UDecalComponent::SetAngleFade(float InStartDegrees, float InEndDegrees)
{
	AngleFadeStart = InStartDegrees;
	AngleFadeEnd = InEndDegrees;
	ValidateParameters();
	MarkSceneProxyDirty();
}

void UDecalComponent::FadeIn() { StartManualFade(1.f, FadeInDuration); }
void UDecalComponent::FadeOut() { StartManualFade(0.f, FadeOutDuration); }

void UDecalComponent::StartManualFade(float TargetOpacity, float Duration)
{
	ManualFadeStartOpacity = ManualFadeOpacity;
	ManualFadeTargetOpacity = TargetOpacity;
	ManualFadeElapsed = 0.f;
	ManualFadeDuration = FMath::Max(Duration, 0.f);
	bManualFadePlaying = ManualFadeDuration > 0.f;
	if (!bManualFadePlaying)
		ManualFadeOpacity = TargetOpacity;
	MarkSceneProxyDirty();
}

void UDecalComponent::DrawDebug(FLineBatcher* LineBatcher) const
{
	if (!LineBatcher)
	{
		return;
	}
	const FVector4 Color(0.1f, 0.85f, 1.0f, 1.0f);
	LineBatcher->AddOrientedBox(CalcLocalBounds(), GetWorldMatrix(), Color);
	const FMatrix World = GetWorldMatrix();
	const FVector Forward = FVector(World[0][0], World[0][1], World[0][2]).GetSafeNormal();
	const float HalfLength = DecalSize.X * std::fabs(GetWorldScale3D().X);
	const FVector Start = GetWorldLocation() - Forward * HalfLength;
	const float Length = HalfLength * 2.0f;
	if (Length > 0.01f)
	{
		LineBatcher->AddArrow(Start, Forward, 2, 0.1f, std::min(2 * 0.1f, 12.0f), Color);
	}
}
