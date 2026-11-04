#pragma once

#include "Asset/Asset.h"
#include "Render/Buffer.h"
#include "Render/SkeletalMeshData.h"

class UMaterial;

// FBX에서 Cook한 스켈레탈 메시 에셋. 여러 컴포넌트가 같이 읽기만 한다
// 포즈·스키닝 결과·정점 버퍼는 컴포넌트마다 다르므로 여기에 두지 않는다 (FSkeletalMeshObject가 갖는다)
class USkeletalMesh : public UAsset
{
	DECLARE_CLASS(USkeletalMesh, UAsset)
public:
	USkeletalMesh() = default;
	virtual ~USkeletalMesh() override = default;

	// 바인드 포즈 정점 + 뼈 영향 + 스켈레톤
	FSkeletalMeshData MeshData;

	// MeshData.MaterialSlots와 같은 순서. Renderer가 실제로 Bind할 머티리얼
	TArray<UMaterial*> Materials;

	// 삼각형 구성은 포즈와 무관하므로 모든 컴포넌트가 이 하나를 같이 쓴다 (static)
	TUniquePtr<FIndexBuffer> IndexBuffer;

	// 리임포트로 내용이 바뀌면 올라간다. 포인터가 그대로라 뼈 목록을 캐시한 쪽이 이 값으로 변화를 안다
	uint32 Revision = 0;

	const FSkeletalMeshData& GetMeshData() const { return MeshData; }
	const FReferenceSkeleton& GetSkeleton() const { return MeshData.Skeleton; }

	// 슬롯에 머티리얼이 없으면 DefaultMaterial
	UMaterial* GetMaterial(uint32 SlotIndex) const;
};
