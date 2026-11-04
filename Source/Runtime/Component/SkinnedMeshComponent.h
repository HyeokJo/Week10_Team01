#pragma once

#include "MeshComponent.h"
#include "Render/SkeletalMesh.h"   // PROPERTY(SkeletalMesh)는 완전한 타입이 필요하다

class FSkeletalMeshObject;

// 스켈레탈 메시를 "그리는" 쪽
// 뼈마다 Component Space 행렬을 받아 FSkeletalMeshObject에 넘기면, 그 객체가 CPU 스키닝을 하고 정점 버퍼를 덮어쓴다
// 뼈 행렬을 "만드는" 일(Local 포즈 -> Component Space)은 USkeletalMeshComponent가 한다
class USkinnedMeshComponent : public UMeshComponent
{
	DECLARE_CLASS(USkinnedMeshComponent, UMeshComponent)

	REFLECT_START(USkinnedMeshComponent)
		PROPERTY(SkeletalMesh)
	REFLECT_END()
public:
	USkinnedMeshComponent();
	virtual ~USkinnedMeshComponent();   // TUniquePtr<FSkeletalMeshObject> 때문에 .cpp에서 정의한다

	// 메시가 바뀌면 덮어쓰기를 비우고 OnSkeletalMeshChanged를 부른다
	void SetSkeletalMesh(USkeletalMesh* InSkeletalMesh);
	USkeletalMesh* GetSkeletalMesh() const { return SkeletalMesh; }
	// 메시 내용이 리임포트로 바뀐 뒤 부른다. 포즈와 렌더 자원을 새 데이터로 다시 만든다
	void OnMeshReimported(int32 PreviousSlotCount);

	// 렌더러는 이 자원만 본다. UStaticMesh를 거치지 않는다
	virtual FMeshRenderResource* GetRenderResource() const override;
	virtual void SubmitToRenderQueue(FRenderQueue& Queue) override;

	// 슬롯 구성과 기본 머티리얼은 SkeletalMesh 에셋이 정한다
	virtual int32 GetNumMaterials() const override;
	virtual FString GetMaterialSlotName(int32 SlotIndex) const override;
	virtual UMaterial* GetDefaultMaterial(int32 SlotIndex) const override;

	// 피킹: 자원이 CPU 삼각형을 주지 않으므로 World가 이 함수를 부른다. 스키닝된 정점으로 검사한다
	virtual bool LineTraceComponentLocal(const FRay& LocalRay, float& OutT) override;
	// 바운드: 현재 포즈의 AABB. 렌더 자원이 없으면 에셋(바인드 포즈) AABB
	virtual FBox CalcLocalBounds() const override;

	// 뼈마다 Component Space 행렬 (부모가 항상 앞). 디버그 라인이 읽는다
	const TArray<FMatrix>& GetComponentSpaceTransforms() const { return ComponentSpaceTransforms; }
	int32 GetNumBones() const { return ComponentSpaceTransforms.Num(); }

	// 디테일 패널이 SkeletalMesh 포인터를 직접 바꾼 뒤 불린다
	virtual void PostEditChangeProperty(const FProperty& Property) override;

	// 로드는 포인터만 채우므로 끝난 뒤 렌더 자원을 만든다
	using Super::Serialize;
	virtual void Serialize(json& Handle, bool bIsLoading) override;
	virtual void Serialize(FStructuredArchive::FRecord Record) override;

protected:
	// SkeletalMesh 포인터가 바뀐 뒤 (Set, 디테일 패널, 로드) 공통으로 부른다
	// 렌더 자원 다시 만들기 -> 포즈 갱신 -> 렌더 상태 갱신. 파생 클래스는 자기 포즈 배열을 먼저 채운다
	virtual void OnSkeletalMeshChanged();

	// ComponentSpaceTransforms를 채운다. 여기서는 레퍼런스(바인드) 포즈 그대로
	virtual void RefreshBoneTransforms();

	// 포즈가 바뀌었을 때 부른다: RefreshBoneTransforms -> MeshObject->Update(스키닝)
	void RefreshPose();

	USkeletalMesh* SkeletalMesh = nullptr;

	// 이 컴포넌트 전용 렌더 자원. 정점 버퍼만 자기 것이고 인덱스·섹션·머티리얼은 에셋 것을 빌린다
	TUniquePtr<FSkeletalMeshObject> MeshObject;

	// 뼈마다 현재 포즈의 Component Space 행렬
	TArray<FMatrix> ComponentSpaceTransforms;
};
