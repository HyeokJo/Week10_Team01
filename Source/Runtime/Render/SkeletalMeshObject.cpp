#include "EnginePCH.h"
#include "SkeletalMeshObject.h"

#include "Render/SkeletalMesh.h"
#include "Render/RenderCommand.h"

FSkeletalMeshObject::FSkeletalMeshObject(USkeletalMesh* InSkeletalMesh)
	: SkeletalMesh(InSkeletalMesh)
{
	if (SkeletalMesh == nullptr || SkeletalMesh->IndexBuffer == nullptr)
	{
		SkeletalMesh = nullptr;
		return;
	}

	const FSkeletalMeshData& Data = SkeletalMesh->GetMeshData();
	if (Data.Vertices.IsEmpty())
	{
		SkeletalMesh = nullptr;
		return;
	}

	// 처음에는 바인드 포즈 그대로 (Update 전에 그려져도 T-포즈가 보인다)
	SkinnedVertices = Data.Vertices;
	VertexBuffer = RenderCommand::CreateDynamicVertexBuffer(
		sizeof(FVertexPNCT) * static_cast<uint32>(SkinnedVertices.Num()), sizeof(FVertexPNCT));
	if (VertexBuffer)
	{
		RenderCommand::UpdateBufferData(VertexBuffer.get(), SkinnedVertices.GetData(),
			sizeof(FVertexPNCT) * static_cast<uint32>(SkinnedVertices.Num()));
	}

	// 바운드는 에셋(바인드 포즈) 것을 쓴다. 뼈를 크게 움직이면 실제 모양보다 작을 수 있다
	const FBox& Box = Data.AABB;
	FLODSphere Sphere;
	Sphere.Center = (Box.Min + Box.Max) * 0.5f;
	const FVector HalfExtent = (Box.Max - Box.Min) * 0.5f;
	Sphere.RadiusSquared = HalfExtent.Dot(HalfExtent);
	SetRenderData(Box, Sphere);
}

void FSkeletalMeshObject::Update(const TArray<FMatrix>& ComponentSpaceTransforms)
{
	if (!IsValid()) return;

	const FSkeletalMeshData& Data = SkeletalMesh->GetMeshData();
	const TArray<FBoneInfo>& Bones = Data.Skeleton.Bones;
	if (ComponentSpaceTransforms.Num() != Bones.Num()) return;

	// 스킨 행렬. 행 벡터라 왼쪽부터 적용된다: 바인드 정점 -> (InverseBindPose) 뼈 기준 -> (ComponentSpace) 현재 위치
	//    바인드 포즈에서는 ComponentSpace == GlobalBindPose라서 단위 행렬이 된다
	SkinMatrices.SetNum(Bones.Num());
	for (int32 i = 0; i < Bones.Num(); i++)
	{
		SkinMatrices[i] = Bones[i].InverseBindPose * ComponentSpaceTransforms[i];
	}

	// Linear Blend Skinning: 정점 하나당 최대 4개 뼈의 결과를 가중치로 섞는다
	for (int32 v = 0; v < Data.Vertices.Num(); v++)
	{
		const FVertexPNCT& Src = Data.Vertices[v];
		const FBoneInfluence& Influence = Data.Influences[v];

		FVector Position(0.f, 0.f, 0.f);
		FVector Normal(0.f, 0.f, 0.f);
		FVector Tangent(0.f, 0.f, 0.f);
		const FVector SrcTangent(Src.Tangent.X, Src.Tangent.Y, Src.Tangent.Z);

		for (int32 i = 0; i < MAX_BONE_INFLUENCES; i++)
		{
			const float Weight = Influence.Weight[i];
			if (Weight <= 0.0f) continue;

			const FMatrix& Skin = SkinMatrices[Influence.BoneIndex[i]];
			Position += Skin.TransformPosition(Src.Position) * Weight;
			// 방향은 이동을 빼고 돌린다. 균일 스케일이면 정규화로 충분하다 (비균일이면 역전치가 필요)
			Normal += Skin.TransformVector(Src.Normal) * Weight;
			Tangent += Skin.TransformVector(SrcTangent) * Weight;
		}

		FVertexPNCT& Dst = SkinnedVertices[v];
		Dst.Position = Position;
		Dst.Normal = Normal.GetSafeNormal();
		const FVector T = Tangent.GetSafeNormal();
		Dst.Tangent = FVector4(T.X, T.Y, T.Z, Src.Tangent.W);   // W(손잡이)는 그대로
	}

	// 정점 버퍼 통째로 덮어쓰기 (Map WRITE_DISCARD)
	RenderCommand::UpdateBufferData(VertexBuffer.get(), SkinnedVertices.GetData(),
		sizeof(FVertexPNCT) * static_cast<uint32>(SkinnedVertices.Num()));
}

FIndexBuffer* FSkeletalMeshObject::GetIndexBuffer(uint32 LODIndex) const
{
	return SkeletalMesh ? SkeletalMesh->IndexBuffer.get() : nullptr;
}

const TArray<FStaticMeshSection>& FSkeletalMeshObject::GetSections(uint32 LODIndex) const
{
	static const TArray<FStaticMeshSection> Empty{};
	return SkeletalMesh ? SkeletalMesh->GetMeshData().Sections : Empty;
}

uint32 FSkeletalMeshObject::GetNumMaterialSlots() const
{
	return SkeletalMesh ? static_cast<uint32>(SkeletalMesh->GetMeshData().MaterialSlots.Num()) : 0;
}

UMaterial* FSkeletalMeshObject::GetDefaultMaterial(uint32 SlotIndex) const
{
	return SkeletalMesh ? SkeletalMesh->GetMaterial(SlotIndex) : nullptr;
}
