#include "EnginePCH.h"
#include "DecalComponent.h"
#include "Render/Material.h"
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

