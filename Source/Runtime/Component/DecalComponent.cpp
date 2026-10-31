#include "EnginePCH.h"
#include "DecalComponent.h"
#include "Render/Material.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "Engine/Scene.h"
#include "Render/LineBatcher.h"
UDecalComponent::UDecalComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UDecalComponent::BeginPlay()
{
	Super::BeginPlay();
	FadeOpacity = 0.0f;
	FadeIn();
}

void UDecalComponent::TickComponent(float DeltaTime)
{
	Super::TickComponent(DeltaTime);
	if (!bIsFading)
	{
		return;
	}
	FadeElapsed += DeltaTime;
	const float Alpha = FMath::Clamp(FadeElapsed / FadeDuration, 0.0f, 1.0f);

	FadeOpacity = FadeStartOpacity + (FadeTargetOpacity - FadeStartOpacity) * Alpha;

	if (Alpha >= 1.0f)
	{
		FadeOpacity = FadeTargetOpacity;
		bIsFading = false;
		SetFadeTickEnabled(false);
	}
}

void UDecalComponent::FadeIn()
{
	StartFade(1.0f, FadeInDuration);
}

void UDecalComponent::FadeOut()
{
	StartFade(0.0f, FadeOutDuration);
}

void UDecalComponent::SetDecalSize(const FVector& InSize)
{
	DecalSize.X = FMath::Max(InSize.X, 0.01f);
	DecalSize.Y = FMath::Max(InSize.Y, 0.01f);
	DecalSize.Z = FMath::Max(InSize.Z, 0.01f);
	MarkDecalBoundsDirty();
}

void UDecalComponent::SetOpacity(float InOpacity)
{
	Opacity = FMath::Clamp(InOpacity, 0.0f, 1.0f);
}

FBox UDecalComponent::CalcLocalBounds() const
{
	return FBox{ -DecalSize, DecalSize };
}

void UDecalComponent::OnTransformDirty()
{
	Super::OnTransformDirty();
	MarkDecalBoundsDirty();
}

void UDecalComponent::PostEditChangeProperty(const FProperty& Property)
{
	Super::PostEditChangeProperty(Property);
	SetDecalSize(DecalSize);
	SetOpacity(Opacity);
	FadeInDuration = FMath::Max(FadeInDuration, 0.0f);
	FadeOutDuration = FMath::Max(FadeOutDuration, 0.0f);
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

void UDecalComponent::Serialize(json& Handle, bool bIsLoading)
{
	Super::Serialize(Handle, bIsLoading);
	if (bIsLoading)
	{
		if (Handle.contains("DecalMaterial"))
		{
			if (Handle["DecalMaterial"].is_null())
			{
				DecalMaterial = nullptr;
			}
			else if (Handle["DecalMaterial"].is_object())
			{
				DecalMaterial = UMaterial::LoadMaterial(Handle["DecalMaterial"]);
			}
		}
	}
	else
	{
		Handle["DecalMaterial"] = DecalMaterial ? UMaterial::SaveMaterial(DecalMaterial) : json(nullptr);
	}
}

void UDecalComponent::Serialize(FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record);

	UMaterial* Loaded = DecalMaterial;
	UMaterial::SerializeMaterialReference(Record.EnterField("DecalMaterial"), Loaded);
	
	if (Record.GetUnderlyingArchive().IsLoading())
	{
		DecalMaterial = Loaded;
	}
}

void UDecalComponent::StartFade(float TargetOpacity, float Duration)
{
	FadeElapsed = 0.0f;
	FadeStartOpacity = FadeOpacity;
	FadeTargetOpacity = FMath::Clamp(TargetOpacity, 0.0f, 1.0f);
	FadeDuration = FMath::Max(Duration, 0.0f);

	if (FadeDuration <= 0.0f)
	{
		FadeOpacity = FadeTargetOpacity;
		bIsFading = false;
		SetFadeTickEnabled(false);
		return;
	}
	bIsFading = true;
	SetFadeTickEnabled(true);
}

void UDecalComponent::SetFadeTickEnabled(bool bEnable)
{
	PrimaryComponentTick.bStartWithTickEnabled = bEnable;
	if (PrimaryComponentTick.IsTickFunctionRegistered())
	{
		PrimaryComponentTick.SetTickFunctionEnable(bEnable);
	}
}

void UDecalComponent::MarkDecalBoundsDirty()
{
	AActor* Owner = GetOwner();
	UWorld* World = Owner ? Owner->GetWorld() : nullptr;
	if (World)
	{
		World->GetScene().MarkDecalDirty(this);
	}
}

