#include "EnginePCH.h"
#include "DecalComponent.h"
#include "Engine/Scene.h"
#include "Engine/DecalSceneProxy.h"

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
}
