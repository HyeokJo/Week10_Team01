#pragma once

#include "SkinnedMeshComponent.h"

// 뼈 포즈를 "만드는" 쪽
// 뼈마다 부모 기준(Local) 행렬을 들고 있고, 바뀌면 Component Space로 다시 계산해 스키닝을 부른다
// 뷰어의 뼈 회전, 나중에 애니메이션이 이 Local 값을 바꾼다
class USkeletalMeshComponent : public USkinnedMeshComponent
{
	DECLARE_CLASS(USkeletalMeshComponent, USkinnedMeshComponent)
public:
	USkeletalMeshComponent();
	virtual ~USkeletalMeshComponent();

	// 뼈마다 Local 행렬 (부모 뼈 기준)
	const TArray<FMatrix>& GetBoneSpaceTransforms() const { return BoneSpaceTransforms; }
	FMatrix GetBoneLocalTransform(int32 BoneIndex) const;

	// 뼈 하나의 Local을 바꾸고 포즈를 다시 계산한다
	void SetBoneLocalTransform(int32 BoneIndex, const FMatrix& InLocal);

	// 모든 뼈를 레퍼런스 포즈로
	void ResetToRefPose();

	int32 FindBoneIndex(const FString& BoneName) const;
	FMatrix GetBoneComponentSpaceTransform(int32 BoneIndex) const;
	// Component Space × ComponentToWorld
	FMatrix GetBoneWorldTransform(int32 BoneIndex) const;

protected:
	// Local 포즈를 레퍼런스 포즈로 채운 뒤 부모 처리
	virtual void OnSkeletalMeshChanged() override;

	// BoneSpaceTransforms -> ComponentSpaceTransforms (부모가 앞이라 한 번 돌면 된다)
	virtual void RefreshBoneTransforms() override;

	TArray<FMatrix> BoneSpaceTransforms;
};
