#pragma once

#include "Asset/Asset.h"
#include "Render/Buffer.h"
#include "Render/StaticMeshData.h"
#include "Render/MeshRenderData.h"
#include "Render/MeshRenderResource.h"

class FPrimitiveSceneProxy;

struct FStaticMeshLODResource
{
	FStaticMeshData Data;
	TUniquePtr<FVertexBuffer> VertexBuffer;
	TUniquePtr<FIndexBuffer> IndexBuffer;
};

// 스태틱 메시는 에셋이 곧 렌더 자원이다 (VB/IB를 에셋이 갖고, 컴포넌트 여럿이 같이 그린다).
// UAsset이 첫 번째 부모여야 UObject 포인터 변환·삭제가 기존과 같다
class UStaticMesh : public UAsset, public FMeshRenderResource
{
	DECLARE_CLASS(UStaticMesh, UAsset)
public:
	UStaticMesh() = default;
	virtual ~UStaticMesh() override;

	FStaticMeshData MeshData;

	// Renderer가 실제로 Bind할 런타임 재질 객체
	TArray<UMaterial*> Materials;

	TUniquePtr<FVertexBuffer> VertexBuffer;
	TUniquePtr<FIndexBuffer> IndexBuffer;

	const FStaticMeshData& GetMeshData() const { return MeshData; }
	UMaterial* GetMaterial(uint32 SlotIndex) const;

public:
    TArray<FStaticMeshLODResource> AdditionalLODs;
    std::array<float, 3> ScreenThresholds{ 0.30f, 0.10f, 0.03f };
    FString SourceObjPath;

    const FStaticMeshData& GetMeshData(uint32 LODIndex) const
    {
        return LODIndex == 0 ? MeshData : AdditionalLODs[LODIndex - 1].Data;
    }

    // ---- FMeshRenderResource ----
    virtual uint32 GetLODCount() const override
    {
        return 1u + static_cast<uint32>(AdditionalLODs.Num());
    }

    virtual FVertexBuffer* GetVertexBuffer(uint32 LODIndex) const override
    {
        return LODIndex == 0 ? VertexBuffer.get() : AdditionalLODs[LODIndex - 1].VertexBuffer.get();
    }

    virtual FIndexBuffer* GetIndexBuffer(uint32 LODIndex) const override
    {
        return LODIndex == 0 ? IndexBuffer.get() : AdditionalLODs[LODIndex - 1].IndexBuffer.get();
    }

    virtual const TArray<FStaticMeshSection>& GetSections(uint32 LODIndex) const override
    {
        return GetMeshData(LODIndex).Sections;
    }

    virtual uint32 GetNumMaterialSlots() const override
    {
        return static_cast<uint32>(MeshData.MaterialSlots.Num());
    }

    virtual UMaterial* GetDefaultMaterial(uint32 SlotIndex) const override { return GetMaterial(SlotIndex); }
    virtual float GetScreenThreshold(uint32 Index) const override { return Index < ScreenThresholds.size() ? ScreenThresholds[Index] : 0.0f; }
    virtual const FStaticMeshData* GetPickingData() const override { return &MeshData; }

    // 정점·LOD가 바뀐 뒤 부른다. 바운드와 LOD 구를 다시 계산하고 프록시에 알린다
    void RebuildRenderData();
};
