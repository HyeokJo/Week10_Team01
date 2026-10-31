#include "EnginePCH.h"
#include "DecalComponent.h"
#include "Engine/Scene.h"
#include "Engine/DecalSceneProxy.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

UDecalComponent::UDecalComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UDecalComponent::BeginPlay()
{
	Super::BeginPlay();
	ValidateParameters();
	ElapsedTime = 0.f;
	bFadePlaying = true;
	MarkSceneProxyDirty();
}

void UDecalComponent::EndPlay()
{
	bFadePlaying = false;
	ElapsedTime = 0.f;
	MarkSceneProxyDirty();
	Super::EndPlay();
}

void UDecalComponent::TickComponent(float DeltaTime)
{
	Super::TickComponent(DeltaTime);
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
	if (Record.GetUnderlyingArchive().IsLoading())
	{
		ValidateParameters();
		ElapsedTime = 0.f;
		bFadePlaying = false;
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
	DecalColor.R = FMath::Max(DecalColor.R, 0.f);
	DecalColor.G = FMath::Max(DecalColor.G, 0.f);
	DecalColor.B = FMath::Max(DecalColor.B, 0.f);
	DecalColor.A = FMath::Clamp(DecalColor.A, 0.f, 1.f);
	FadeInStartDelay = FMath::Max(FadeInStartDelay, 0.f);
	FadeInDuration = FMath::Max(FadeInDuration, 0.f);
	FadeStartDelay = FMath::Max(FadeStartDelay, 0.f);
	FadeDuration = FMath::Max(FadeDuration, 0.f);
}
