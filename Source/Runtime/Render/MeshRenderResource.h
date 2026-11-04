#pragma once

#include "Render/Buffer.h"
#include "Render/MeshRenderData.h"
#include "Render/StaticMeshData.h"
#include <unordered_map>
#include <vector>

class UMaterial;
class FPrimitiveSceneProxy;

// 렌더러가 "그릴 수 있는 메시"로 아는 전부.
// 렌더러(패킷, 묶음, 그림자, 오클루전, 아웃라인)는 메시 종류를 모르고 이 인터페이스만 쓴다.
//
// - UStaticMesh        : 에셋 자체가 자원이다. 여러 컴포넌트가 하나를 같이 쓴다
// - FSkeletalMeshObject : 컴포넌트마다 하나. 정점 버퍼만 자기 것(스키닝 결과), 나머지는 에셋 것을 빌린다
//
// UObject가 아니다. UStaticMesh는 UAsset과 함께 다중 상속하고, FSkeletalMeshObject는 컴포넌트가 소유한다.
class FMeshRenderResource
{
public:
	FMeshRenderResource() : SortID(NextSortID++) {}
	virtual ~FMeshRenderResource();

	FMeshRenderResource(const FMeshRenderResource&) = delete;
	FMeshRenderResource& operator=(const FMeshRenderResource&) = delete;

	// ---- 파생 클래스가 답하는 것 ----
	virtual uint32 GetLODCount() const = 0;
	virtual FVertexBuffer* GetVertexBuffer(uint32 LODIndex) const = 0;
	virtual FIndexBuffer* GetIndexBuffer(uint32 LODIndex) const = 0;
	virtual const TArray<FStaticMeshSection>& GetSections(uint32 LODIndex) const = 0;

	// 머티리얼 슬롯 개수와 슬롯별 기본 머티리얼 (컴포넌트의 덮어쓰기가 없을 때 쓴다)
	virtual uint32 GetNumMaterialSlots() const = 0;
	virtual UMaterial* GetDefaultMaterial(uint32 SlotIndex) const = 0;

	// LOD 전환 화면 크기. LOD가 하나뿐이면 쓰이지 않는다
	virtual float GetScreenThreshold(uint32 Index) const { return 0.0f; }

	// 피킹에 쓸 CPU 삼각형. nullptr이면 World가 컴포넌트의 LineTraceComponentLocal로 넘어간다
	// (스켈레탈은 정점이 매번 바뀌어 BVH가 없으므로 nullptr)
	virtual const FStaticMeshData* GetPickingData() const { return nullptr; }

	// ---- 공통 구현 ----
	const FBox& GetRenderBounds() const { return RenderBounds; }
	const FLODSphere& GetLocalLODSphere() const { return LocalLODSphere; }

	// (자원, 머티리얼 조합)마다 섹션 목록을 한 번만 만들어 공유한다
	TSharedPtr<const FMeshRenderState> GetRenderState(const std::vector<UMaterial*>& Materials);

	// 이 자원을 그리는 프록시 목록. 자원이 바뀌거나 사라지면 알려 준다
	void RegisterProxy(FPrimitiveSceneProxy* Proxy);
	void UnregisterProxy(FPrimitiveSceneProxy* Proxy);

	// 정렬 키에 들어가는 번호. 같은 번호끼리 모이면 VB/IB 바인딩이 줄어든다
	const uint16 SortID;

protected:
	// 바운드를 바꾸고, 섹션 캐시를 비우고, 프록시에게 알린다. 정점·섹션·LOD 구성이 바뀐 뒤 부른다
	// (스키닝처럼 정점 "값"만 바뀌고 구성은 그대로면 부르지 않는다)
	void SetRenderData(const FBox& Bounds, const FLODSphere& Sphere);

private:
	static inline uint16 NextSortID = 0;

	struct FMaterialSlotsHash
	{
		size_t operator()(const std::vector<UMaterial*>& Slots) const;
	};

	FLODSphere LocalLODSphere;
	FBox RenderBounds;
	std::unordered_map<std::vector<UMaterial*>, TWeakPtr<const FMeshRenderState>, FMaterialSlotsHash> RenderStates;
	TArray<FPrimitiveSceneProxy*> RenderProxies;
};
