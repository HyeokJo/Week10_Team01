#pragma once

#include "GameFramework/Actor.h"
#include "Component/SkeletalMeshDebugComponent.h"
#include "Editor/Gizmo/Gizmo.h"
#include "Editor/LevelEditor/MultipleViewports/Adapter/MultipleViewportsAdapter.h"
#include "Render/SceneRenderTargets.h"

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
	USceneComponent* GetBoneProxy() const { return BoneProxy; }

private:
	USkeletalMeshDebugComponent* MeshComponent = nullptr;
	// 기즈모는 USceneComponent만 움직일 수 있는데 뼈는 컴포넌트가 아니다
	// 그래서 선택한 뼈 자리에 이 빈 컴포넌트를 두고 기즈모가 움직이게 한 뒤 결과를 뼈에 옮긴다. 부모가 없어 Relative가 곧 World다
	USceneComponent* BoneProxy = nullptr;
};

// 스켈레탈 메시 뷰어의 프리뷰 월드, 렌더 타깃, 카메라, 뼈 기즈모를 소유한다
// 월드는 처음 열 때 만들고, 다른 메시를 열면 같은 월드에서 메시만 바꾼다
// 그리는 일은 UEditorEngine이 레벨 뷰포트와 같은 함수로 한다
class FSkeletalMeshViewer
{
public:
	// Single 레이아웃의 View 하나만 쓴다
	static constexpr int32 ViewIndex = 0;

	~FSkeletalMeshViewer();

	bool Open(USkeletalMesh* Mesh);
	void Close();
	bool IsOpen() const { return PreviewWorld != nullptr; }

	// 트랜스폼·렌더 상태 변경을 프록시에 반영한다. 게임 로직은 돌지 않는다 (EditorPreview)
	void Tick(float DeltaTime);

	// 선택한 뼈에 기즈모를 맞추고, 드래그 결과를 뼈 포즈에 반영한다
	void UpdateBoneGizmo(const FVector2& LocalMouse, bool bViewHovered);

	// 카메라 방향은 두고 위치만 옮겨 선택한 뼈를 화면에 담는다. 선택이 없으면 메시 전체를 담는다
	void FocusOnSelectedBone();

	// 렌더 타깃과 View 크기를 맞춘다. 크기가 같으면 아무 일도 하지 않는다
	void SetViewSize(uint32 Width, uint32 Height);

	UWorld* GetWorld() const { return PreviewWorld; }
	USkeletalMeshDebugComponent* GetMeshComponent() const { return MeshComponent; }
	FMultipleViewportsAdapter& GetAdapter() { return Adapter; }
	const FSceneRenderTargets& GetTargets() const { return Targets; }
	FGizmo& GetGizmo() { return Gizmo; }

	// 행렬을 위치·회전·크기로 나눈다. 전단(shear)이 있으면 그 성분은 버려진다
	static FTransform DecomposeMatrix(const FMatrix& Matrix);

private:
	void CreatePreviewWorld();
	// 메시 정면(+X)에서 바운드 전체가 보이도록 카메라를 옮긴다
	void FrameMesh(const USkeletalMesh& Mesh);
	void FocusOnBounds(const USkeletalMesh& Mesh);
	// Local = 프록시 World × 부모 World⁻¹ (행 벡터. Global = Local × ParentGlobal을 거꾸로 푼 식)
	void ApplyProxyToBone(int32 BoneIndex);
	// 커서 아래의 뼈 번호. 없으면 -1이다
	int32 PickBone(const FVector2& LocalMouse) const;

	UWorld* PreviewWorld = nullptr;
	USkeletalMeshDebugComponent* MeshComponent = nullptr;
	USceneComponent* BoneProxy = nullptr;

	FMultipleViewportsAdapter Adapter;
	FSceneRenderTargets Targets;
	FGizmo Gizmo;
	int32 GizmoBone = -1;
	bool bClickCandidate = false;
	FVector2 ClickStartMouse;
};
