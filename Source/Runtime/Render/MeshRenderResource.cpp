#include "EnginePCH.h"
#include "MeshRenderResource.h"
#include "Engine/PrimitiveSceneProxy.h"
#include <algorithm>

FMeshRenderResource::~FMeshRenderResource()
{
	// 자원이 먼저 사라지면 프록시가 들고 있는 포인터를 비운다
	for (FPrimitiveSceneProxy* Proxy : RenderProxies)
		Proxy->OnMeshDestroyed();
}

size_t FMeshRenderResource::FMaterialSlotsHash::operator()(const std::vector<UMaterial*>& Slots) const
{
	size_t Hash = 0;
	for (const UMaterial* Material : Slots)
		Hash ^= std::hash<const UMaterial*>{}(Material) + 0x9e3779b9u + (Hash << 6) + (Hash >> 2);
	return Hash;
}

void FMeshRenderResource::SetRenderData(const FBox& Bounds, const FLODSphere& Sphere)
{
	RenderBounds = Bounds;
	LocalLODSphere = Sphere;
	RenderStates.clear();
	for (FPrimitiveSceneProxy* Proxy : RenderProxies)
		Proxy->OnMeshRenderDataChanged();
}

void FMeshRenderResource::SetRenderBounds(const FBox& Bounds, const FLODSphere& Sphere)
{
	RenderBounds = Bounds;
	LocalLODSphere = Sphere;
	for (FPrimitiveSceneProxy* Proxy : RenderProxies)
		Proxy->OnMeshBoundsChanged();
}

TSharedPtr<const FMeshRenderState> FMeshRenderResource::GetRenderState(const std::vector<UMaterial*>& Materials)
{
	auto& WeakState = RenderStates[Materials];
	if (auto State = WeakState.lock()) return State;
	auto State = MakeShared<FMeshRenderState>();
	State->LODCount = static_cast<uint8>(std::min(GetLODCount(), 4u));
	for (uint32 I = 0; I < 3; ++I)
	{
		const float Threshold = GetScreenThreshold(I);
		State->LODThresholdSq[I] = Threshold * Threshold;
	}
	for (uint32 LOD = 0; LOD < State->LODCount; ++LOD)
	{
		auto& Range = State->LODs[LOD];
		Range.FirstSection = State->Sections.Num();
		for (const FStaticMeshSection& Section : GetSections(LOD))
		{
			UMaterial* Material = Section.MaterialSlotIndex < Materials.size()
				? Materials[Section.MaterialSlotIndex] : GetDefaultMaterial(Section.MaterialSlotIndex);
			if (Material) State->Sections.Add({ Material, Section.StartIndex, Section.IndexCount });
		}
		Range.NumSections = State->Sections.Num() - Range.FirstSection;
	}
	WeakState = State;
	return State;
}

void FMeshRenderResource::RegisterProxy(FPrimitiveSceneProxy* Proxy)
{
	RenderProxies.Add(Proxy);
}

void FMeshRenderResource::UnregisterProxy(FPrimitiveSceneProxy* Proxy)
{
	const auto It = std::find(RenderProxies.begin(), RenderProxies.end(), Proxy);
	if (It != RenderProxies.end())
		RenderProxies.RemoveAtSwap(static_cast<uint32>(It - RenderProxies.begin()));
}
