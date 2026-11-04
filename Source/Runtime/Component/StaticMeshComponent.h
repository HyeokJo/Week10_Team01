#pragma once

#include "MeshComponent.h"

class UStaticMeshComponent : public UMeshComponent
{
	DECLARE_CLASS(UStaticMeshComponent, UMeshComponent)

	REFLECT_START(UStaticMeshComponent)
		PROPERTY(StaticMesh)
	REFLECT_END()
public:
	UStaticMeshComponent();
	virtual ~UStaticMeshComponent();

	virtual void BeginPlay();
	virtual void TickComponent(float DeltaTime);

	virtual const FStaticMeshData* GetMeshData() const override { return StaticMesh ? &StaticMesh->GetMeshData() : nullptr; }

	// 메시가 바뀌면 슬롯 구성이 달라지므로 덮어쓰기를 비운다
	void SetStaticMesh(UStaticMesh* InStaticMesh);
	UStaticMesh* GetStaticMesh() const { return StaticMesh; }
	// 메시 내용이 리임포트로 바뀐 뒤 부른다
	void OnMeshReimported(int32 PreviousSlotCount);
	virtual FMeshRenderResource* GetRenderResource() const override { return StaticMesh; }

	virtual int32 GetNumMaterials() const override;
	virtual FString GetMaterialSlotName(int32 SlotIndex) const override;
	virtual UMaterial* GetDefaultMaterial(int32 SlotIndex) const override;

	void SubmitToRenderQueue(FRenderQueue& Queue) override;
	void SubmitToRenderQueue(FRenderQueue& Queue, uint32 LODIndex);

private:
	UStaticMesh* StaticMesh = nullptr;
	// Mesh의 Section별 Material과 Texture를 보존해 각각의 렌더 패킷으로 제출한다.
	//virtual void SubmitToRenderQueue(TQueue<FRenderPacket>& RenderQueue) override;
};
