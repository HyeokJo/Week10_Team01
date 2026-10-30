#include "EnginePCH.h"
#include "DecalComponent.h"

void UDecalComponent::SetDecalSize(const FVector& InSize)
{
	DecalSize.X = FMath::Max(InSize.X, 0.01f);
	DecalSize.Y = FMath::Max(InSize.Y, 0.01f);
	DecalSize.Z = FMath::Max(InSize.Z, 0.01f);
}

void UDecalComponent::SetOpacity(float InOpacity)
{
	Opacity = FMath::Clamp(InOpacity, 0.0f, 1.0f);
}

FBox UDecalComponent::CalcLocalBounds() const
{
	return FBox{ -DecalSize, DecalSize };
}

void UDecalComponent::PostEditChangeProperty(const FProperty& Property)
{
	Super::PostEditChangeProperty(Property);
	SetDecalSize(DecalSize);
	SetOpacity(Opacity);
}
