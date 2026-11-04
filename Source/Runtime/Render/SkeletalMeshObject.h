#pragma once

#include "Render/MeshRenderResource.h"
#include "Render/Vertex.h"
#include "Math/Matrix.h"

class USkeletalMesh;

// 스켈레탈 메시 컴포넌트 하나가 화면에 그리는 자원 (언리얼의 FSkeletalMeshObject 자리).
//
// - 정점 버퍼  : 이 객체 것 (dynamic). Update가 CPU 스키닝 결과로 매번 덮어쓴다
// - 인덱스 버퍼, 섹션, 머티리얼 : USkeletalMesh 에셋 것을 빌린다 (모든 컴포넌트가 같다)
//
// 지금은 CPU 스키닝 하나뿐이다. GPU 스키닝을 넣으면 이 클래스를 CPU/GPU 파생으로 나눈다.
class FSkeletalMeshObject : public FMeshRenderResource
{
public:
	explicit FSkeletalMeshObject(USkeletalMesh* InSkeletalMesh);
	virtual ~FSkeletalMeshObject() override = default;

	// 에셋에 정점·인덱스 버퍼가 있고 정점 버퍼를 만들었으면 true
	bool IsValid() const { return SkeletalMesh != nullptr && VertexBuffer != nullptr; }

	// 뼈마다 Component Space 행렬 -> 스킨 행렬 -> 정점 블렌딩 -> 정점 버퍼 업로드
	void Update(const TArray<FMatrix>& ComponentSpaceTransforms);

	// 마지막 Update 결과 (Component Space). 피킹이 읽는다
	const TArray<FVertexPNCT>& GetSkinnedVertices() const { return SkinnedVertices; }

	// ---- FMeshRenderResource ----
	virtual uint32 GetLODCount() const override { return 1; }
	virtual FVertexBuffer* GetVertexBuffer(uint32 LODIndex) const override { return VertexBuffer.get(); }
	virtual FIndexBuffer* GetIndexBuffer(uint32 LODIndex) const override;
	virtual const TArray<FStaticMeshSection>& GetSections(uint32 LODIndex) const override;
	virtual uint32 GetNumMaterialSlots() const override;
	virtual UMaterial* GetDefaultMaterial(uint32 SlotIndex) const override;

private:
	USkeletalMesh* SkeletalMesh = nullptr;

	TUniquePtr<FVertexBuffer> VertexBuffer;

	// 스키닝 결과. 업로드 원본이자 피킹용 CPU 사본
	TArray<FVertexPNCT> SkinnedVertices;

	// 뼈마다 InverseBindPose * ComponentSpace. 바인드 포즈 정점을 현재 포즈로 옮기는 행렬
	TArray<FMatrix> SkinMatrices;
};
