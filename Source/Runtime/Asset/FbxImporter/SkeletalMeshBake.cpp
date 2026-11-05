#include "EnginePCH.h"
#include "SkeletalMeshBake.h"

#include "Asset/BakeStream.h"
#include "Render/SkeletalMeshData.h"

static_assert(std::is_trivially_copyable_v<FBoneInfluence>);
static_assert(sizeof(FBoneInfluence) == 32);
static_assert(sizeof(FMatrix) == 64);

using namespace BakeStream;

namespace
{
	// FMatrix는 operator=를 직접 정의해서 WritePod를 쓸 수 없다. 숫자 16개를 그대로 옮긴다
	void WriteMatrix(std::ofstream& Out, const FMatrix& Matrix)
	{
		Out.write(reinterpret_cast<const char*>(Matrix.M), sizeof(Matrix.M));
	}

	bool ReadMatrix(std::ifstream& In, FMatrix& Matrix)
	{
		In.read(reinterpret_cast<char*>(Matrix.M), sizeof(Matrix.M));
		return static_cast<bool>(In);
	}

	bool Validate(const FSkeletalMeshData& Data, FString& OutError)
	{
		const uint32 VertexCount = static_cast<uint32>(Data.Vertices.Num());
		const uint32 IndexCount = static_cast<uint32>(Data.Indices.Num());
		const int32 BoneCount = Data.Skeleton.Num();

		if (VertexCount == 0 || IndexCount == 0 || IndexCount % 3 != 0 || BoneCount == 0)
		{
			OutError = std::format("invalid mesh (vertices {}, indices {}, bones {})", VertexCount, IndexCount, BoneCount);
			return false;
		}
		if (static_cast<uint32>(Data.Influences.Num()) != VertexCount)
		{
			OutError = std::format("influences {} != vertices {}", Data.Influences.Num(), VertexCount);
			return false;
		}

		for (uint32 Index : Data.Indices)
		{
			if (Index >= VertexCount)
			{
				OutError = std::format("index {} out of range (vertices {})", Index, VertexCount);
				return false;
			}
		}

		for (const FStaticMeshSection& Section : Data.Sections)
		{
			if (Section.StartIndex + Section.IndexCount > IndexCount ||
				Section.MaterialSlotIndex >= static_cast<uint32>(Data.MaterialSlots.Num()))
			{
				OutError = std::format("invalid section (start {}, count {}, slot {})", Section.StartIndex, Section.IndexCount, Section.MaterialSlotIndex);
				return false;
			}
		}

		// 포즈 계산이 "부모가 항상 앞 번호"를 믿고 한 번에 돈다. 어기면 아직 계산 안 된 행렬을 읽는다
		for (int32 i = 0; i < BoneCount; ++i)
		{
			const int32 Parent = Data.Skeleton.Bones[i].ParentIndex;
			if (Parent < -1 || Parent >= i)
			{
				OutError = std::format("bone {} has invalid parent {}", i, Parent);
				return false;
			}
		}

		for (const FBoneInfluence& Influence : Data.Influences)
		{
			for (int32 i = 0; i < MAX_BONE_INFLUENCES; ++i)
			{
				if (Influence.BoneIndex[i] < 0 || Influence.BoneIndex[i] >= BoneCount)
				{
					OutError = std::format("bone index {} out of range (bones {})", Influence.BoneIndex[i], BoneCount);
					return false;
				}
			}
		}
		return true;
	}
}

void FSkeletalMeshBake::WriteBody(std::ofstream& Out, const FSkeletalMeshData& Data)
{
	WritePod<uint32>(Out, static_cast<uint32>(Data.Skeleton.Bones.Num()));
	for (const FBoneInfo& Bone : Data.Skeleton.Bones)
	{
		WriteString(Out, Bone.Name);
		WritePod<int32>(Out, Bone.ParentIndex);
		WriteMatrix(Out, Bone.LocalBindPose);
		WriteMatrix(Out, Bone.GlobalBindPose);
		WriteMatrix(Out, Bone.InverseBindPose);
	}

	WriteArray<FVertexPNCT>(Out, Data.Vertices);
	WriteArray<FBoneInfluence>(Out, Data.Influences);
	WriteArray<uint32>(Out, Data.Indices);
	WriteArray<FStaticMeshSection>(Out, Data.Sections);
	WriteMaterialSlots(Out, Data.MaterialSlots);
	WritePod<FBox>(Out, Data.AABB);
}

bool FSkeletalMeshBake::ReadBody(std::ifstream& In, FSkeletalMeshData& Data, FString& OutError)
{
	OutError = "truncated data";

	uint32 BoneCount = 0;
	if (!ReadPod<uint32>(In, BoneCount))
	{
		return false;
	}

	for (uint32 i = 0; i < BoneCount; ++i)
	{
		FBoneInfo Bone;
		if (!ReadString(In, Bone.Name) ||
			!ReadPod<int32>(In, Bone.ParentIndex) ||
			!ReadMatrix(In, Bone.LocalBindPose) ||
			!ReadMatrix(In, Bone.GlobalBindPose) ||
			!ReadMatrix(In, Bone.InverseBindPose))
		{
			return false;
		}
		Data.Skeleton.NameToIndex.Add(Bone.Name, static_cast<int32>(i));
		Data.Skeleton.Bones.Add(std::move(Bone));
	}

	if (!ReadArray<FVertexPNCT>(In, Data.Vertices) ||
		!ReadArray<FBoneInfluence>(In, Data.Influences) ||
		!ReadArray<uint32>(In, Data.Indices) ||
		!ReadArray<FStaticMeshSection>(In, Data.Sections) ||
		!ReadMaterialSlots(In, Data.MaterialSlots) ||
		!ReadPod<FBox>(In, Data.AABB))
	{
		return false;
	}

	OutError.clear();
	return Validate(Data, OutError);
}
