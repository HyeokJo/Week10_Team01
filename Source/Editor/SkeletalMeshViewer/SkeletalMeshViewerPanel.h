#pragma once

#include "Editor/EditorUI/EditorPanel.h"
#include "Editor/SkeletalMeshViewer/SkeletalMeshViewer.h"

// 뷰어 창. 왼쪽은 렌더 결과, 오른쪽 위는 본 하이라키, 오른쪽 아래는 선택한 본의 디테일
// 닫기 버튼은 패널의 Open 상태만 바꾸고, 월드 정리는 UEditorEngine이 프레임 시작에 한다 (이번 프레임 그리기와 겹치지 않게)
class FSkeletalMeshViewerPanel : public IEditorPanel
{
public:
	bool Init() override;
	void Tick(float DeltaTime) override {}
	void OnRender() override;
	const char* GetPanelName() const override { return "Skeletal Mesh Viewer"; }

	void SetViewer(FSkeletalMeshViewer* InViewer) { Viewer = InViewer; }
	void RequestFocus() { bFocusRequested = true; }

	// 입력 처리는 ImGui NewFrame보다 먼저 돌아서, 아래 값은 지난 프레임에 기록한 값이다
	// 렌더 결과 영역 위에 커서가 있는가 (영역 위의 도구 버튼은 제외)
	bool IsViewHovered() const { return bViewHovered; }
	// 하이라키와 제목 표시줄을 포함한 창 전체 위에 커서가 있는가
	bool IsWindowHovered() const { return bWindowHovered; }
	bool IsWindowFocused() const { return bWindowFocused; }
	FVector2 GetViewSize() const { return { ViewSize.x, ViewSize.y }; }
	FVector2 GetLocalMousePosition() const;

private:
	void DrawView(const ImVec2& Size);
	void DrawGizmoControls();
	void DrawSideColumn(const ImVec2& Size);
	void DrawHierarchy(const ImVec2& Size);
	void DrawBoneNode(const USkeletalMeshDebugComponent& Component, int32 BoneIndex);
	void DrawBoneDetails(const ImVec2& Size);
	void SelectBone(int32 BoneIndex);
	// 뼈 데이터에는 부모 번호만 있어서, 위에서 아래로 그리려고 자식 목록을 만들어 둔다
	void RebuildBoneTree(const USkeletalMesh& Mesh);

	FSkeletalMeshViewer* Viewer = nullptr;
	ImVec2 ViewOrigin{ 0.0f, 0.0f };
	ImVec2 ViewSize{ 0.0f, 0.0f };
	float SideColumnWidth = 360.0f;
	float DetailsHeight = 290.0f;
	bool bViewHovered = false;
	bool bWindowHovered = false;
	bool bWindowFocused = false;
	bool bFocusRequested = false;

	const USkeletalMesh* BoneTreeMesh = nullptr;
	TArray<TArray<int32>> BoneChildren;
	TArray<int32> RootBones;

	// 뷰포트 클릭으로 선택이 바뀌면 하이라키를 그 뼈까지 펼치고 스크롤한다. 트리에서 직접 고른 경우는 움직이지 않는다
	int32 LastSeenSelectedBone = -1;
	bool bRevealSelectedBone = false;

	// 디테일 창이 편집 중인 값. 행렬에서 매 프레임 오일러 각을 다시 뽑으면 90도 근처에서 값이 튀어 드래그가 끊긴다
	// 그래서 값을 들고 있다가, 행렬이 밖에서(기즈모·리셋) 바뀌었을 때만 다시 분해한다
	int32 DetailBone = -1;
	FMatrix DetailLocal;
	FTransform DetailTransform;
};
