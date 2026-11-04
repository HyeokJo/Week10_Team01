#include "EnginePCH.h"
#include "SkeletalMeshComponent.h"

USkeletalMeshComponent::USkeletalMeshComponent()
{
}

USkeletalMeshComponent::~USkeletalMeshComponent()
{
}

void USkeletalMeshComponent::OnSkeletalMeshChanged()
{
	// Local 포즈를 레퍼런스 포즈로 채운 뒤 부모가 렌더 자원을 만들고 RefreshPose를 부른다
	BoneSpaceTransforms.Reset();
	if (SkeletalMesh)
	{
		const TArray<FBoneInfo>& Bones = SkeletalMesh->GetSkeleton().Bones;
		BoneSpaceTransforms.Reserve(Bones.Num());
		for (const FBoneInfo& Bone : Bones)
		{
			BoneSpaceTransforms.Add(Bone.LocalBindPose);
		}
	}

	Super::OnSkeletalMeshChanged();
}

FMatrix USkeletalMeshComponent::GetBoneLocalTransform(int32 BoneIndex) const
{
	return BoneSpaceTransforms.IsValidIndex(BoneIndex) ? BoneSpaceTransforms[BoneIndex] : FMatrix::Identity;
}

void USkeletalMeshComponent::SetBoneLocalTransform(int32 BoneIndex, const FMatrix& InLocal)
{
	if (!BoneSpaceTransforms.IsValidIndex(BoneIndex)) return;

	BoneSpaceTransforms[BoneIndex] = InLocal;
	RefreshPose();   // 자식 뼈까지 다시 계산 -> 스키닝 -> 정점 버퍼
}

void USkeletalMeshComponent::ResetToRefPose()
{
	if (!SkeletalMesh) return;

	const TArray<FBoneInfo>& Bones = SkeletalMesh->GetSkeleton().Bones;
	for (int32 i = 0; i < Bones.Num() && i < BoneSpaceTransforms.Num(); i++)
	{
		BoneSpaceTransforms[i] = Bones[i].LocalBindPose;
	}
	RefreshPose();
}

int32 USkeletalMeshComponent::FindBoneIndex(const FString& BoneName) const
{
	return SkeletalMesh ? SkeletalMesh->GetSkeleton().FindBoneIndex(BoneName) : -1;
}

FMatrix USkeletalMeshComponent::GetBoneComponentSpaceTransform(int32 BoneIndex) const
{
	return ComponentSpaceTransforms.IsValidIndex(BoneIndex) ? ComponentSpaceTransforms[BoneIndex] : FMatrix::Identity;
}

FMatrix USkeletalMeshComponent::GetBoneWorldTransform(int32 BoneIndex) const
{
	// 행 벡터: 뼈 기준 -> Component Space -> World
	return GetBoneComponentSpaceTransform(BoneIndex) * GetWorldMatrix();
}

void USkeletalMeshComponent::RefreshBoneTransforms()
{
	ComponentSpaceTransforms.Reset();
	if (!SkeletalMesh) return;

	// SceneComponent와 같은 식: Global = Local × ParentGlobal
	// 부모가 항상 앞 번호라 한 번 앞에서부터 돌면 부모 값이 이미 계산되어 있다
	const TArray<FBoneInfo>& Bones = SkeletalMesh->GetSkeleton().Bones;
	ComponentSpaceTransforms.SetNum(Bones.Num());
	for (int32 i = 0; i < Bones.Num(); i++)
	{
		const FMatrix& Local = BoneSpaceTransforms.IsValidIndex(i) ? BoneSpaceTransforms[i] : Bones[i].LocalBindPose;
		const int32 Parent = Bones[i].ParentIndex;
		ComponentSpaceTransforms[i] = Parent < 0 ? Local : Local * ComponentSpaceTransforms[Parent];
	}
}
