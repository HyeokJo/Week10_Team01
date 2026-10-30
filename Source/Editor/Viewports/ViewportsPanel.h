#pragma once

#include "Editor/EditorUI/EditorPanel.h"
#include "Editor/LevelEditor/MultipleViewports/Adapter/MultipleViewportsAdapterTypes.h"
#include "Render/RenderingInfo.h"
class FMultipleViewportsAdapter;
class FGizmo;

class FViewportsPanel : public IEditorPanel
{
public:
	// View 표시 모드를 편집할 Adapter를 연결한다. 엔진이 수명을 보장한다.
	void SetViewportAdapter(FMultipleViewportsAdapter* Value) { ViewportAdapter = Value; }
	// 상단 World/Local 버튼이 바꿀 기즈모를 연결한다. 없으면 버튼을 그리지 않는다.
	void SetGizmo(FGizmo* Value) { Gizmo = Value; }
	// 네 View의 렌더 타깃과 UI 제어 상태를 초기화한다.
	bool Init() override;
	// 프레임 입력에서 Splitter Drag와 View별 UI 요청을 수집한다.
	void Tick(float DeltaTime) override;
	// 각 View Texture와 Splitter·Layout·Preset 조작 UI를 ImGui 패널에 그린다.
	void OnRender() override;
	const char* GetPanelName() const override { return "Viewports"; }

	// Core가 계산한 View Rect에 맞춰 해당 Slot의 활성 상태와 렌더 타깃을 갱신한다.
	void SetView(int32 ViewIndex, const FRect& Rect, bool bActive);
	// 화면 표시용(sRGB) 렌더 정보. 후처리 이후의 FXAA·Overlay가 그린다.
	const FRenderingInfo& GetRenderingInfo(int32 ViewIndex) const;
	// 씬(Linear, float) 렌더 정보. 불투명·반투명·안개가 그린다.
	const FRenderingInfo& GetSceneRenderingInfo(int32 ViewIndex) const;
	// 씬용·표시용 렌더 정보를 같은 깊이 타깃으로 구성한다. PIE 창 타깃도 같은 구성을 쓴다.
	static void BuildViewRenderingInfos(FRenderingInfo& OutSceneInfo, FRenderingInfo& OutViewInfo,
		FTexture2D* SceneColor, FTexture2D* ViewColor, FTexture2D* Depth, uint32 Width, uint32 Height);
	// FXAA 결과를 임시로 받는 View별 텍스처. ColorTarget과 크기·포맷이 같다.
	FTexture2D* GetFxaaTarget(int32 ViewIndex) const;

	FVector2 GetContentSize() const { return {ContentSize.x, ContentSize.y}; }
	FVector2 GetLocalMousePosition() const;
	bool IsHovered() const;

	float ConsumeHorizontalDrag();
	float ConsumeVerticalDrag();
	// Core의 현재 Layout과 View별 Preset을 UI 표시 상태에 동기화한다.
	void SetControlState(ELayoutMode LayoutMode, int32 SingleViewIndex, const EMultipleViewportsCameraPreset CameraPresets[4]);
	// UI에서 발생한 Layout 변경 요청을 한 번 소비하도록 반환한다.
	bool ConsumeLayoutRequest(ELayoutMode& OutMode, int32& OutSingleViewIndex);
	// UI에서 발생한 View별 Camera Preset 요청을 한 번 소비하도록 반환한다.
	bool ConsumeCameraPresetRequest(int32& OutViewIndex, EMultipleViewportsCameraPreset& OutPreset);
	// 카메라 이동 속도가 바뀌었을 때 해당 View 왼쪽 아래에 잠시 값을 표시한다.
	void ShowCameraSpeed(int32 ViewIndex, float Speed);

private:
	FMultipleViewportsAdapter* ViewportAdapter = nullptr;
	FGizmo* Gizmo = nullptr;
	struct FViewSlot
	{
		// View 하나의 Rect·활성 상태·Color/Depth 타깃과 렌더 정보를 담는다.
		FRect Rect{};
		bool bActive = false;
		uint32 Width = 0;
		uint32 Height = 0;
		// 씬은 Linear로 SceneColorTarget(float)에 그리고, 인코딩한 결과를 ColorTarget(sRGB 8비트)에 담아 화면에 표시한다
		TUniquePtr<FTexture2D> SceneColorTarget;
		TUniquePtr<FTexture2D> ColorTarget;
		TUniquePtr<FTexture2D> DepthTarget;
		TUniquePtr<FTexture2D> FxaaTarget;
		FRenderingInfo SceneRenderingInfo{};	// SceneColorTarget + DepthTarget
		FRenderingInfo RenderingInfo{};			// ColorTarget + DepthTarget
	};

	// Rect 크기가 바뀐 Slot의 Color/Depth Texture와 렌더 정보를 다시 만든다.
	void ResizeSlot(FViewSlot& Slot, uint32 Width, uint32 Height);
	// 콘솔 stat 명령으로 켜진 항목을 해당 View의 좌상단에 겹쳐 그린다.
	void DrawStatOverlay(ImDrawList* DrawList, const ImVec2& ViewMin) const;
	// ShowCameraSpeed로 요청된 속도 표시가 아직 유효하면 그린다.
	void DrawCameraSpeedNotice(ImDrawList* DrawList) const;
	// View 오른쪽 위 카메라 메뉴. 프리셋 선택과 FOV·Near·Far·Transform 편집을 한 드롭다운에 담는다.
	void DrawCameraMenu(int32 ViewIndex, const ImVec2& ViewTopRight);
	// 카메라 메뉴 안의 투영·Transform 편집 칸. 바뀐 값만 Adapter에 반영한다.
	void DrawCameraProperties(int32 ViewIndex);

	FViewSlot Slots[4]{};
	ImVec2 ContentOrigin{};
	ImVec2 ContentSize{1.0f, 1.0f};
	bool bHovered = false;
	// View 위에 그린 버튼·콤보의 화면 영역. 이 위의 클릭은 뷰포트 입력(피킹·기즈모)으로 보지 않는다.
	struct FControlRect
	{
		ImVec2 Min;
		ImVec2 Max;
	};
	TArray<FControlRect> ControlRects;
	// 방금 그린 항목의 영역을 ControlRects에 넣는다.
	void RecordLastItemAsControl();
	float PendingHorizontalDrag = 0.0f;
	float PendingVerticalDrag = 0.0f;
	// 누른 뒤 커서가 Splitter 영역을 벗어나도 버튼을 놓을 때까지 드래그를 유지한다.
	bool bDraggingVerticalSplitter = false;
	bool bDraggingHorizontalSplitter = false;
	ELayoutMode CurrentLayoutMode = ELayoutMode::QuadSplit;
	int32 CurrentSingleViewIndex = 0;
	EMultipleViewportsCameraPreset CurrentCameraPresets[4]{};
	bool bHasLayoutRequest = false;
	ELayoutMode RequestedLayoutMode = ELayoutMode::QuadSplit;
	int32 RequestedSingleViewIndex = 0;
	int32 PendingCameraPresetViewIndex = InvalidViewIndex;
	EMultipleViewportsCameraPreset PendingCameraPreset = EMultipleViewportsCameraPreset::Perspective;
	int32 CameraSpeedViewIndex = InvalidViewIndex;
	float CameraSpeedValue = 0.0f;
	double CameraSpeedShownTime = 0.0;
};
