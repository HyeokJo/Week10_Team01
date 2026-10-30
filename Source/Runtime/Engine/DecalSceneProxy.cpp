#include "EnginePCH.h"
#include "Component/DecalComponent.h"
#include "DecalSceneProxy.h"

void FDecalSceneProxy::UpdateFromComponent()
{
	DecalToWorld = Component->GetWorldMatrix();
	WorldToDecal = DecalToWorld.Inverse();

	DecalSize = Component->GetDecalSize();
	TexturePath = Component->GetTexturePath();
	Opacity = Component->GetOpacity();
	bHiddenInGame = Component->IsHiddenInGame();

	const FBox LocalBounds{ -DecalSize, DecalSize };
	Bounds = LocalBounds.GetWorldAABB(DecalToWorld);
}
