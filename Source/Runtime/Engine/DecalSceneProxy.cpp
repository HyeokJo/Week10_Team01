#include "EnginePCH.h"
#include "Component/DecalComponent.h"
#include "DecalSceneProxy.h"
#include "Asset/AssetManager.h"
#include "Render/Material.h"

void FDecalSceneProxy::UpdateFromComponent()
{
	DecalToWorld = Component->GetWorldMatrix();
	WorldToDecal = DecalToWorld.Inverse();
	ProjectionDirection = FVector(DecalToWorld.M[0][0], DecalToWorld.M[0][1], DecalToWorld.M[0][2]).GetSafeNormal();
	AngleFadeCosines = FVector2(std::cos(FMath::DegreesToRadians(Component->GetAngleFadeEnd())),
		std::cos(FMath::DegreesToRadians(Component->GetAngleFadeStart())));

	DecalSize = Component->GetDecalSize();
	UMaterial* Material = Component->GetDecalMaterial();
	const FString& NewTexturePath = Component->GetTexturePath();

	if (TexturePath != NewTexturePath || !Texture)
	{
		TexturePath = NewTexturePath;
		Texture = TexturePath.empty() ? nullptr : UAssetManager::Get().LoadTexture(TexturePath);
	}
	if (TexturePath.empty())
		Texture = Material && Material->Textures.Num() > 0 ? Material->Textures[0] : nullptr;
	const FString& NewNormalTexturePath = Component->GetNormalTexturePath();
	if (NormalTexturePath != NewNormalTexturePath || (!NormalTexturePath.empty() && !NormalTexture))
	{
		NormalTexturePath = NewNormalTexturePath;
		NormalTexture = NormalTexturePath.empty() ? nullptr : UAssetManager::Get().LoadTexture(NormalTexturePath, true, false);
	}
	bFlipNormalGreen = Component->IsNormalGreenFlipped();
	if (NormalTexturePath.empty())
	{
		NormalTexture = Material && Material->Textures.Num() > 1 ? Material->Textures[1] : nullptr;
		if (Material) bFlipNormalGreen = Material->bFlipNormalGreen;
	}
	Opacity = Component->GetRenderOpacity();
	DecalColor = Component->GetDecalColor();
	if (Material)
	{
		DecalColor.R *= Material->BaseColor.R;
		DecalColor.G *= Material->BaseColor.G;
		DecalColor.B *= Material->BaseColor.B;
		DecalColor.A *= Material->BaseColor.A;
	}
	SamplerState = Material ? Material->SamplerState : ESamplerState::LinearClamp;
	bRenderAsUnlit = Component->IsRenderAsUnlit();
	ProjectionEndFade = Component->GetProjectionEndFade();
	SortOrder = Component->GetSortOrder();
	FadeScreenSize = Component->GetFadeScreenSize();
	bHiddenInGame = Component->IsHiddenInGame();

	const FBox LocalBounds{ -DecalSize, DecalSize };
	Bounds = LocalBounds.GetWorldAABB(DecalToWorld);
}
