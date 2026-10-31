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
	Opacity = Component->GetRenderOpacity();
	DecalColor = Component->GetDecalColor();
	SortOrder = Component->GetSortOrder();
	FadeScreenSize = Component->GetFadeScreenSize();
	bHiddenInGame = Component->IsHiddenInGame();

	const FBox LocalBounds{ -DecalSize, DecalSize };
	Bounds = LocalBounds.GetWorldAABB(DecalToWorld);
}
