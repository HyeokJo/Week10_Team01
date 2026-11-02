#include "EnginePCH.h"
#include "Component/DecalComponent.h"
#include "DecalSceneProxy.h"
#include "Asset/AssetManager.h"

void FDecalSceneProxy::UpdateFromComponent()
{
	DecalToWorld = Component->GetWorldMatrix();
	WorldToDecal = DecalToWorld.Inverse();

	DecalSize = Component->GetDecalSize();
	const FString& NewTexturePath = Component->GetTexturePath();

	if (TexturePath != NewTexturePath || !Texture)
	{
		TexturePath = NewTexturePath;
		Texture = TexturePath.empty() ? nullptr : UAssetManager::Get().LoadTexture(TexturePath);
	}
	const FString& NewNormalTexturePath = Component->GetNormalTexturePath();
	if (NormalTexturePath != NewNormalTexturePath || (!NormalTexturePath.empty() && !NormalTexture))
	{
		NormalTexturePath = NewNormalTexturePath;
		NormalTexture = NormalTexturePath.empty() ? nullptr : UAssetManager::Get().LoadTexture(NormalTexturePath, true, false);
	}
	bFlipNormalGreen = Component->IsNormalGreenFlipped();
	Opacity = Component->GetRenderOpacity();
	DecalColor = Component->GetDecalColor();
	SortOrder = Component->GetSortOrder();
	FadeScreenSize = Component->GetFadeScreenSize();
	bHiddenInGame = Component->IsHiddenInGame();

	const FBox LocalBounds{ -DecalSize, DecalSize };
	Bounds = LocalBounds.GetWorldAABB(DecalToWorld);
}
