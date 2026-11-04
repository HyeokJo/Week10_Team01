#include "EnginePCH.h"
#include "SkeletalMesh.h"

#include "Asset/AssetManager.h"

UMaterial* USkeletalMesh::GetMaterial(uint32 SlotIndex) const
{
	// UStaticMesh::GetMaterial과 같은 규칙
	if (SlotIndex < Materials.size() && Materials[SlotIndex])
	{
		return Materials[SlotIndex];
	}
	return UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial");
}
