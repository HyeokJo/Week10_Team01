#include "EnginePCH.h"
#include "SkinnedMeshComponent.h"

#include "Asset/AssetManager.h"
#include "Render/SkeletalMeshObject.h"
#include "Engine/PrimitiveSceneProxy.h"
#include "Collision/Ray.h"
#include "UObject/Property.h"

USkinnedMeshComponent::USkinnedMeshComponent()
{
}

USkinnedMeshComponent::~USkinnedMeshComponent()
{
	// MeshObject는 TUniquePtr라 자동으로 지워진다. 지워질 때 등록된 프록시에게 알린다 (~FMeshRenderResource)
}

void USkinnedMeshComponent::SetSkeletalMesh(USkeletalMesh* InSkeletalMesh)
{
	if (SkeletalMesh == InSkeletalMesh)
		return;

	SkeletalMesh = InSkeletalMesh;
	ClearOverrideMaterials();   // 슬롯 구성이 달라진다
	OnSkeletalMeshChanged();
}

void USkinnedMeshComponent::OnSkeletalMeshChanged()
{
	// 렌더 자원 다시 만들기. 옛 자원이 지워지면 프록시의 Mesh 포인터는 비워진다
	MeshObject.reset();
	ComponentSpaceTransforms.Reset();
	if (SkeletalMesh)
	{
		MeshObject = MakeUnique<FSkeletalMeshObject>(SkeletalMesh);
		if (!MeshObject->IsValid())
		{
			HTR_LOG(Warning, "SkinnedMeshComponent: '{}' has no render data", SkeletalMesh->GetPath());
			MeshObject.reset();
		}
	}

	// 포즈 계산 -> 스키닝
	RefreshPose();

	// 프록시가 새 자원·섹션·바운드를 다시 읽게 한다
	MarkRenderStateDirty();
	OnTransformDirty();
}

FMeshRenderResource* USkinnedMeshComponent::GetRenderResource() const
{
	return MeshObject.get();
}

void USkinnedMeshComponent::SubmitToRenderQueue(FRenderQueue& Queue)
{
	// 보통은 World의 빠른 경로가 프록시 캐시로 그린다. 프록시 캐시가 없을 때만 여기로 온다
	const FPrimitiveSceneProxy* Proxy = GetSceneProxy();
	if (!MeshObject || !Proxy) return;
	const FMeshRenderState* State = Proxy->GetRenderState();
	if (!State) return;

	const FCachedMeshLOD& LOD = State->LODs[0];
	for (uint32 I = 0; I < LOD.NumSections; ++I)
	{
		const FCachedMeshSection& Section = State->Sections[LOD.FirstSection + I];
		FRenderPacket& Packet = Queue.AddDefaulted_GetRef();
		Packet.Proxy = Proxy;
		Packet.Mesh = MeshObject.get();
		Packet.Material = Section.Material;
		Packet.StartIndex = Section.StartIndex;
		Packet.IndexCount = Section.IndexCount;
		Packet.LODIndex = 0;
	}
}

int32 USkinnedMeshComponent::GetNumMaterials() const
{
	return SkeletalMesh ? static_cast<int32>(SkeletalMesh->GetMeshData().MaterialSlots.Num()) : 0;
}

FString USkinnedMeshComponent::GetMaterialSlotName(int32 SlotIndex) const
{
	if (!SkeletalMesh || SlotIndex < 0 || SlotIndex >= GetNumMaterials())
		return FString();
	return SkeletalMesh->GetMeshData().MaterialSlots[SlotIndex].Name;
}

UMaterial* USkinnedMeshComponent::GetDefaultMaterial(int32 SlotIndex) const
{
	return SkeletalMesh ? SkeletalMesh->GetMaterial(static_cast<uint32>(SlotIndex)) : nullptr;
}

bool USkinnedMeshComponent::LineTraceComponentLocal(const FRay& LocalRay, float& OutT)
{
	if (!MeshObject || !SkeletalMesh) return false;

	// 정점이 포즈마다 바뀌어 BVH를 미리 만들 수 없다. 클릭할 때만 불리므로 삼각형을 전부 검사한다
	const TArray<FVertexPNCT>& Vertices = MeshObject->GetSkinnedVertices();
	const TArray<uint32>& Indices = SkeletalMesh->GetMeshData().Indices;

	bool bHit = false;
	for (int32 i = 0; i + 2 < Indices.Num(); i += 3)
	{
		float T = 0.0f;
		if (RayIntersectsTriangle(LocalRay, Vertices[Indices[i]].Position, Vertices[Indices[i + 1]].Position,
								  Vertices[Indices[i + 2]].Position, T) && T < OutT)
		{
			OutT = T;
			bHit = true;
		}
	}
	return bHit;
}

FBox USkinnedMeshComponent::CalcLocalBounds() const
{
	return SkeletalMesh ? SkeletalMesh->GetMeshData().AABB : Super::CalcLocalBounds();
}

void USkinnedMeshComponent::PostEditChangeProperty(const FProperty& Property)
{
	Super::PostEditChangeProperty(Property);

	// 포인터는 이미 바뀌어 있다. 렌더 자원만 맞춰 준다
	if (Property.Name == "SkeletalMesh")
	{
		ClearOverrideMaterials();
		OnSkeletalMeshChanged();
	}
}

void USkinnedMeshComponent::Serialize(json& Handle, bool bIsLoading)
{
	Super::Serialize(Handle, bIsLoading);   // SkeletalMesh 포인터 + 덮어쓰기 머티리얼
	if (bIsLoading)
	{
		OnSkeletalMeshChanged();   // 덮어쓰기는 지우지 않는다 (방금 읽었다)
	}
}

void USkinnedMeshComponent::Serialize(FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record);
	if (Record.GetUnderlyingArchive().IsLoading())
	{
		OnSkeletalMeshChanged();
	}
}

void USkinnedMeshComponent::RefreshBoneTransforms()
{
	// 포즈를 만들 줄 모르므로 레퍼런스(바인드) 포즈 그대로. 스킨 행렬이 전부 단위 행렬이 된다
	ComponentSpaceTransforms.Reset();
	if (!SkeletalMesh) return;

	const TArray<FBoneInfo>& Bones = SkeletalMesh->GetSkeleton().Bones;
	ComponentSpaceTransforms.Reserve(Bones.Num());
	for (const FBoneInfo& Bone : Bones)
	{
		ComponentSpaceTransforms.Add(Bone.GlobalBindPose);
	}
}

void USkinnedMeshComponent::RefreshPose()
{
	RefreshBoneTransforms();
	if (MeshObject)
	{
		MeshObject->Update(ComponentSpaceTransforms);
	}
}
