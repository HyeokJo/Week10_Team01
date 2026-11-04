#include "EnginePCH.h"
#include "SkeletalMeshDebugComponent.h"

#include "Render/LineBatcher.h"
#include "Render/SkeletalMesh.h"

USkeletalMeshDebugComponent::USkeletalMeshDebugComponent()
{
}

USkeletalMeshDebugComponent::~USkeletalMeshDebugComponent()
{
}

void USkeletalMeshDebugComponent::DrawDebug(FLineBatcher* LineBatcher) const
{
	if (!LineBatcher || !SkeletalMesh || !ShouldDrawDebug()) return;

	const TArray<FBoneInfo>& Bones = SkeletalMesh->GetSkeleton().Bones;
	if (ComponentSpaceTransforms.Num() != Bones.Num()) return;

	// 뼈는 메시 안쪽에 있으므로 깊이 테스트 없이 항상 위에 그린다 (AddForegroundLine)
	const FMatrix ComponentToWorld = GetWorldMatrix();
	const FVector4 BoneColor(1.0f, 0.85f, 0.2f, 1.0f);

	for (int32 i = 0; i < Bones.Num(); i++)
	{
		// 뼈 기준 -> Component Space -> World (행 벡터라 왼쪽부터)
		const FMatrix BoneToWorld = ComponentSpaceTransforms[i] * ComponentToWorld;
		const FVector Joint = BoneToWorld.TransformPosition(FVector(0.f, 0.f, 0.f));

		// 부모 관절 -> 자식 관절
		const int32 Parent = Bones[i].ParentIndex;
		if (bDrawBones && Parent >= 0)
		{
			const FMatrix ParentToWorld = ComponentSpaceTransforms[Parent] * ComponentToWorld;
			LineBatcher->AddForegroundLine(ParentToWorld.TransformPosition(FVector(0.f, 0.f, 0.f)), Joint, BoneColor);
		}

		// 관절의 X/Y/Z 축 (빨강/초록/파랑). 뼈 행렬의 축 방향을 그대로 보여 준다
		if (bDrawBoneAxes)
		{
			const float L = BoneAxisLength;
			LineBatcher->AddForegroundLine(Joint, BoneToWorld.TransformPosition(FVector(L, 0.f, 0.f)), FVector4(1.f, 0.f, 0.f, 1.f));
			LineBatcher->AddForegroundLine(Joint, BoneToWorld.TransformPosition(FVector(0.f, L, 0.f)), FVector4(0.f, 1.f, 0.f, 1.f));
			LineBatcher->AddForegroundLine(Joint, BoneToWorld.TransformPosition(FVector(0.f, 0.f, L)), FVector4(0.f, 0.f, 1.f, 1.f));
		}
	}
}
