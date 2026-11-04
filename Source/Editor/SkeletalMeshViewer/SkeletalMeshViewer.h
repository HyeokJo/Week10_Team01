#pragma once

#include "GameFramework/Actor.h"
#include "Component/SkeletalMeshDebugComponent.h"

class UWorld;
class USkeletalMesh;

// 뷰어 전용 액터. 레벨의 ASkeletalMeshActor와 달리 뼈를 그리는 디버그 컴포넌트를 쓴다
class ASkeletalMeshPreviewActor : public AActor
{
	DECLARE_CLASS(ASkeletalMeshPreviewActor, AActor)
public:
	ASkeletalMeshPreviewActor();
	virtual ~ASkeletalMeshPreviewActor() = default;

	USkeletalMeshDebugComponent* GetMeshComponent() const { return MeshComponent; }

private:
	USkeletalMeshDebugComponent* MeshComponent = nullptr;
};

// 스켈레탈 메시 뷰어의 프리뷰 월드를 소유한다
// 월드는 처음 열 때 만들고, 다른 메시를 열면 같은 월드에서 메시만 바꾼다
class FSkeletalMeshViewer
{
public:
	~FSkeletalMeshViewer();

	bool Open(USkeletalMesh* Mesh);
	void Close();
	bool IsOpen() const { return PreviewWorld != nullptr; }

	// 트랜스폼·렌더 상태 변경을 프록시에 반영한다. 게임 로직은 돌지 않는다 (EditorPreview)
	void Tick(float DeltaTime);

	UWorld* GetWorld() const { return PreviewWorld; }
	USkeletalMeshDebugComponent* GetMeshComponent() const { return MeshComponent; }

private:
	void CreatePreviewWorld();

	UWorld* PreviewWorld = nullptr;
	USkeletalMeshDebugComponent* MeshComponent = nullptr;
};
