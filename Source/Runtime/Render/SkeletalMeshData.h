#pragma once

#include "Render/StaticMeshData.h"   // FStaticMeshSection, FStaticMaterialSlot 재사용
#include "Math/Matrix.h"
#include "Containers/Map.h"


constexpr int32 MAX_BONE_INFLUENCES = 4;

// 뼈 하나
struct FBoneInfo
{
	FString Name;
	int32 ParentIndex = -1;

	// 부모 기준. 뷰어는 이 값을 바꾼다
	FMatrix LocalBindPose = FMatrix::Identity;

	// 컴포넌트 기준. 디버그 라인·바인드 검증용
	FMatrix GlobalBindPose = FMatrix::Identity;

	// GlobalBindPose.Inverse(). 스킨 행렬 = InverseBindPose * CurrentGlobal
	FMatrix InverseBindPose = FMatrix::Identity;
};

struct FReferenceSkeleton
{
	// 부모가 항상 앞
	TArray<FBoneInfo> Bones;

	TMap<FString, int32> NameToIndex;

	int32 Num() const { return Bones.Num(); }
	int32 FindBoneIndex(const FString& Name) const
	{
		const int32* Found = NameToIndex.Find(Name);
		return Found ? *Found : -1;
	}
};

// 정점 하나의 뼈 영향. Vertices와 같은 순서의 병렬 배열이다 (CPU 스키닝에서만 쓴다)
struct FBoneInfluence
{
	int32 BoneIndex[MAX_BONE_INFLUENCES] = { 0, 0, 0, 0 };
	float Weight[MAX_BONE_INFLUENCES] = { 0.f, 0.f, 0.f, 0.f };
};

struct FSkeletalMeshData
{
	FReferenceSkeleton Skeleton;

	TArray<FVertexPNCT> Vertices;          // 바인드 포즈 정점
	TArray<FBoneInfluence> Influences;     // Vertices.Num()과 같은 개수
	TArray<uint32> Indices;
	TArray<FStaticMeshSection> Sections;
	TArray<FStaticMaterialSlot> MaterialSlots;
	FBox AABB;
};