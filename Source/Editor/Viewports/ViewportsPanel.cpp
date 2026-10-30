#include "EnginePCH.h"
#include "Editor/Viewports/ViewportsPanel.h"
#include "Editor/LevelEditor/MultipleViewports/Adapter/MultipleViewportsAdapter.h"
#include "Editor/Gizmo/Gizmo.h"

#include "Core/StatOverlay.h"
#include "Core/Stats/LightweightStats.h"
#include "Render/RenderCommand.h"
#include "Math/Color.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <format>

#include "Core/Stats/EditorStats.h"

namespace
{
constexpr ImU32 SplitterColor = IM_COL32(55, 55, 55, 230);
constexpr ImU32 SplitterHoverColor = IM_COL32(255, 192, 0, 255);
constexpr const char* CameraPresetLabels[] = {
	"Perspective", "Ortho (Current)", "Top", "Bottom", "Front", "Back", "Left", "Right"};

// Stat Overlay
constexpr float StatOverlayMargin = 8.0f;
constexpr float StatOverlayPadding = 6.0f;
constexpr ImU32 StatOverlayBackgroundColor = IM_COL32(0, 0, 0, 140);
constexpr ImU32 TitleColor = IM_COL32(255, 210, 60, 255);
constexpr ImU32 ValueColor = IM_COL32(235, 235, 235, 255);

// Camera Speed 표시
constexpr double CameraSpeedNoticeSeconds = 1.5;

// View 상단 컨트롤
constexpr float ViewControlMargin = 8.0f;
constexpr float CameraMenuPopupWidth = 320.0f;
constexpr float CameraPropertyLabelWidth = 120.0f;

// 가장 긴 항목이 잘리지 않는 콤보 폭 (글자 + 좌우 여백 + 펼침 화살표 버튼)
float CalcComboWidth(const char* const* Labels, const int Count)
{
	float MaxTextWidth = 0.0f;
	for (int Index = 0; Index < Count; ++Index)
		MaxTextWidth = std::max(MaxTextWidth, ImGui::CalcTextSize(Labels[Index]).x);
	return MaxTextWidth + ImGui::GetStyle().FramePadding.x * 2.0f + ImGui::GetFrameHeight();
}
}

// 네 View의 렌더 타깃을 최소 크기로 초기화한다.
bool FViewportsPanel::Init()
{
	for (FViewSlot& Slot : Slots)
		ResizeSlot(Slot, 1, 1);
	return true;
}

// 패널의 프레임 갱신 인터페이스이며 별도 계산은 하지 않는다.
void FViewportsPanel::Tick(float DeltaTime)
{
	(void)DeltaTime;
}

// Core Rect에 맞춰 View 활성 상태와 타깃 크기를 갱신한다.
void FViewportsPanel::SetView(const int32 ViewIndex, const FRect& Rect, const bool bActive)
{
	assert(ViewIndex >= 0 && ViewIndex < 4);
	FViewSlot& Slot = Slots[ViewIndex];
	Slot.Rect = Rect;
	Slot.bActive = bActive && Rect.Width > 0.0f && Rect.Height > 0.0f;
	if (!Slot.bActive)
		return;

	const uint32 Width = static_cast<uint32>(std::max(1.0f, Rect.Width));
	const uint32 Height = static_cast<uint32>(std::max(1.0f, Rect.Height));
	if (Width != Slot.Width || Height != Slot.Height)
		ResizeSlot(Slot, Width, Height);
}

// 인덱스를 검사해 해당 View의 렌더 정보를 반환한다.
const FRenderingInfo& FViewportsPanel::GetRenderingInfo(const int32 ViewIndex) const
{
	assert(ViewIndex >= 0 && ViewIndex < 4);
	return Slots[ViewIndex].RenderingInfo;
}

const FRenderingInfo& FViewportsPanel::GetSceneRenderingInfo(const int32 ViewIndex) const
{
	assert(ViewIndex >= 0 && ViewIndex < 4);
	return Slots[ViewIndex].SceneRenderingInfo;
}

FTexture2D* FViewportsPanel::GetFxaaTarget(const int32 ViewIndex) const
{
	assert(ViewIndex >= 0 && ViewIndex < 4);
	return Slots[ViewIndex].FxaaTarget.get();
}

// 마우스 위치에서 패널 원점을 빼 로컬 좌표로 바꾼다.
FVector2 FViewportsPanel::GetLocalMousePosition() const
{
	const ImVec2 Mouse = ImGui::GetMousePos();
	return {Mouse.x - ContentOrigin.x, Mouse.y - ContentOrigin.y};
}

// 엔진 입력 처리는 ImGui NewFrame보다 먼저 실행되므로, 지난 프레임에 기록한 View 위 컨트롤 영역을 현재 OS 커서로 검사한다.
// 컨트롤 위의 클릭이 뷰포트 피킹(빈 곳 클릭 = 선택 해제)이나 기즈모 조작으로 새지 않게 한다.
bool FViewportsPanel::IsHovered() const
{
	POINT Cursor{};
	if (::GetCursorPos(&Cursor))
	{
		const float CursorX = static_cast<float>(Cursor.x);
		const float CursorY = static_cast<float>(Cursor.y);
		for (const FControlRect& Rect : ControlRects)
		{
			if (CursorX >= Rect.Min.x && CursorX < Rect.Max.x && CursorY >= Rect.Min.y && CursorY < Rect.Max.y)
				return false;
		}
	}
	return bHovered;
}

void FViewportsPanel::RecordLastItemAsControl()
{
	ControlRects.Add({ImGui::GetItemRectMin(), ImGui::GetItemRectMax()});
}

// 누적 가로 Splitter 이동량을 반환하고 초기화한다.
float FViewportsPanel::ConsumeHorizontalDrag()
{
	const float Result = PendingHorizontalDrag;
	PendingHorizontalDrag = 0.0f;
	return Result;
}

// 누적 세로 Splitter 이동량을 반환하고 초기화한다.
float FViewportsPanel::ConsumeVerticalDrag()
{
	const float Result = PendingVerticalDrag;
	PendingVerticalDrag = 0.0f;
	return Result;
}

// Layout·Single 대상·Preset을 UI 표시와 동기화한다.
void FViewportsPanel::SetControlState(const ELayoutMode LayoutMode, const int32 SingleViewIndex, const EMultipleViewportsCameraPreset CameraPresets[4])
{
	CurrentLayoutMode = LayoutMode;
	CurrentSingleViewIndex = SingleViewIndex;
	for (int32 Index = 0; Index < 4; ++Index)
		CurrentCameraPresets[Index] = CameraPresets[Index];
}

// 대기 Layout 요청을 한 번 반환하고 플래그를 지운다.
bool FViewportsPanel::ConsumeLayoutRequest(ELayoutMode& OutMode, int32& OutSingleViewIndex)
{
	if (!bHasLayoutRequest)
		return false;
	OutMode = RequestedLayoutMode;
	OutSingleViewIndex = RequestedSingleViewIndex;
	bHasLayoutRequest = false;
	return true;
}

// 대기 Preset 요청을 한 번 반환하고 지운다.
bool FViewportsPanel::ConsumeCameraPresetRequest(int32& OutViewIndex, EMultipleViewportsCameraPreset& OutPreset)
{
	if (PendingCameraPresetViewIndex == InvalidViewIndex)
		return false;
	OutViewIndex = PendingCameraPresetViewIndex;
	OutPreset = PendingCameraPreset;
	PendingCameraPresetViewIndex = InvalidViewIndex;
	return true;
}

// 현재 프리셋 이름을 단 드롭다운을 View 오른쪽 위에 두고, 펼치면 프리셋과 카메라 속성을 보여 준다.
void FViewportsPanel::DrawCameraMenu(const int32 ViewIndex, const ImVec2& ViewTopRight)
{
	if (!ViewportAdapter)
		return;

	const EMultipleViewportsCameraPreset CurrentPreset = CurrentCameraPresets[ViewIndex];
	// 어떤 프리셋 이름이 와도 잘리지 않게 가장 긴 이름에 맞춘다.
	const float CameraMenuWidth = CalcComboWidth(CameraPresetLabels, IM_ARRAYSIZE(CameraPresetLabels));
	ImGui::SetCursorScreenPos({ViewTopRight.x - CameraMenuWidth - ViewControlMargin, ViewTopRight.y + ViewControlMargin});
	ImGui::SetNextItemWidth(CameraMenuWidth);
	// 콤보 팝업은 기본으로 버튼 폭을 따르므로, 속성 칸이 들어가도록 최소 폭을 넓힌다.
	ImGui::SetNextWindowSizeConstraints({CameraMenuPopupWidth, 0.0f}, {FLT_MAX, FLT_MAX});
	const bool bOpen = ImGui::BeginCombo("##CameraMenu", CameraPresetLabels[static_cast<int>(CurrentPreset)]);
	RecordLastItemAsControl();
	if (!bOpen)
		return;

	// 프리셋은 고르면 메뉴가 닫히고, 다음 프레임에 엔진이 요청을 Adapter에 반영한다.
	const auto PresetItem = [&](const EMultipleViewportsCameraPreset Preset)
	{
		if (ImGui::Selectable(CameraPresetLabels[static_cast<int>(Preset)], Preset == CurrentPreset))
		{
			PendingCameraPresetViewIndex = ViewIndex;
			PendingCameraPreset = Preset;
		}
	};
	ImGui::SeparatorText("Perspective");
	PresetItem(EMultipleViewportsCameraPreset::Perspective);
	ImGui::SeparatorText("Orthographic");
	for (int Preset = static_cast<int>(EMultipleViewportsCameraPreset::OrthographicView);
		Preset <= static_cast<int>(EMultipleViewportsCameraPreset::Right); ++Preset)
		PresetItem(static_cast<EMultipleViewportsCameraPreset>(Preset));

	ImGui::SeparatorText("View");
	DrawCameraProperties(ViewIndex);
	ImGui::EndCombo();
}

void FViewportsPanel::DrawCameraProperties(const int32 ViewIndex)
{
	FViewCamera Camera = ViewportAdapter->GetViewCamera(ViewIndex);
	const bool bOrthographic = Camera.Projection.Mode == EProjectionMode::Orthographic;
	bool bChanged = false;
	bool bRotationChanged = false;

	if (!ImGui::BeginTable("CameraProperties", 2))
		return;
	ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, CameraPropertyLabelWidth);
	ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
	const auto BeginRow = [](const char* Label)
	{
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(Label);
		ImGui::TableSetColumnIndex(1);
		ImGui::SetNextItemWidth(-1.0f);
	};

	// 직교는 FOV 대신 화면에 담기는 월드 폭을 바꾼다.
	if (bOrthographic)
	{
		BeginRow("Ortho Width");
		float OrthoWidth = Camera.Projection.OrthoWidth;
		if (ImGui::DragFloat("##OrthoWidth", &OrthoWidth, 0.1f) && std::isfinite(OrthoWidth))
		{
			Camera.Projection.OrthoWidth = FMath::Clamp(OrthoWidth, 0.01f, 1000000.0f);
			bChanged = true;
		}
	}
	else
	{
		BeginRow("Field of View");
		float Fov = Camera.Projection.FovDegrees;
		if (ImGui::DragFloat("##FOV", &Fov, 0.1f, 1.0f, 179.0f, "%.1f") && std::isfinite(Fov))
		{
			Camera.Projection.FovDegrees = FMath::Clamp(Fov, 1.0f, 179.0f);
			bChanged = true;
		}
	}

	// Near와 Far는 둘 다 맞아야(0 < Near < Far) 반영한다.
	BeginRow("Near View Plane");
	float Near = Camera.Projection.NearClip;
	bool bClipChanged = ImGui::InputFloat("##Near", &Near);
	BeginRow("Far View Plane");
	float Far = Camera.Projection.FarClip;
	bClipChanged |= ImGui::InputFloat("##Far", &Far);
	if (bClipChanged && std::isfinite(Near) && std::isfinite(Far) && Near > 0.0f && Far > Near)
	{
		Camera.Projection.NearClip = Near;
		Camera.Projection.FarClip = Far;
		bChanged = true;
	}

	BeginRow("Location");
	float Location[3] = {Camera.Transform.Location.X, Camera.Transform.Location.Y, Camera.Transform.Location.Z};
	if (ImGui::DragFloat3("##Location", Location, 0.1f) &&
		std::isfinite(Location[0]) && std::isfinite(Location[1]) && std::isfinite(Location[2]))
	{
		Camera.Transform.Location = {Location[0], Location[1], Location[2]};
		bChanged = true;
	}

	// 원근 카메라는 Roll 없이 Yaw·Pitch로만 조작하므로 Pitch를 제한하고 Roll을 0으로 둔다.
	BeginRow("Rotation");
	const FQuat CurrentRotation(Camera.Transform.Rotation.X, Camera.Transform.Rotation.Y, Camera.Transform.Rotation.Z, Camera.Transform.Rotation.W);
	FRotator Rotation = CurrentRotation.ToFRotator();
	float Euler[3] = {Rotation.Pitch, Rotation.Yaw, Rotation.Roll};
	if (ImGui::DragFloat3("##Rotation", Euler, 0.1f) &&
		std::isfinite(Euler[0]) && std::isfinite(Euler[1]) && std::isfinite(Euler[2]))
	{
		Rotation.Pitch = bOrthographic ? Euler[0] : FMath::Clamp(Euler[0], -89.0f, 89.0f);
		Rotation.Yaw = Euler[1];
		Rotation.Roll = bOrthographic ? Euler[2] : 0.0f;
		const FQuat NewRotation = Rotation.Quaternion();
		Camera.Transform.Rotation = {NewRotation.X, NewRotation.Y, NewRotation.Z, NewRotation.W};
		bChanged = true;
		bRotationChanged = true;
	}
	ImGui::SetItemTooltip("Pitch / Yaw / Roll");
	ImGui::EndTable();

	if (bChanged)
		ViewportAdapter->ApplyCameraProperties(ViewIndex, Camera, bRotationChanged);
}

// 표시할 View와 값을 기록하고 표시 시작 시각을 갱신한다.
void FViewportsPanel::ShowCameraSpeed(const int32 ViewIndex, const float Speed)
{
	CameraSpeedViewIndex = ViewIndex;
	CameraSpeedValue = Speed;
	CameraSpeedShownTime = ImGui::GetTime();
}

// View 왼쪽 아래에 반투명 배경과 함께 이동 속도를 그린다.
void FViewportsPanel::DrawCameraSpeedNotice(ImDrawList* DrawList) const
{
	if (CameraSpeedViewIndex < 0 || CameraSpeedViewIndex >= 4 || !Slots[CameraSpeedViewIndex].bActive)
		return;
	if (ImGui::GetTime() - CameraSpeedShownTime > CameraSpeedNoticeSeconds)
		return;

	const FRect& Rect = Slots[CameraSpeedViewIndex].Rect;
	const FString Text = std::format("Camera Speed  {:.2f}", CameraSpeedValue);
	const ImVec2 TextSize = ImGui::CalcTextSize(Text.c_str());
	const ImVec2 TextMin{
		ContentOrigin.x + Rect.X + StatOverlayMargin + StatOverlayPadding,
		ContentOrigin.y + Rect.Y + Rect.Height - StatOverlayMargin - StatOverlayPadding - TextSize.y};
	DrawList->AddRectFilled(
		{TextMin.x - StatOverlayPadding, TextMin.y - StatOverlayPadding},
		{TextMin.x + TextSize.x + StatOverlayPadding, TextMin.y + TextSize.y + StatOverlayPadding},
		StatOverlayBackgroundColor, 4.0f);
	DrawList->AddText(TextMin, ValueColor, Text.c_str());
}

// View Texture와 Splitter·Layout·Preset UI를 그리고 요청을 기록한다.
void FViewportsPanel::OnRender()
{
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0f, 0.0f});
	ImGui::Begin("Viewports", nullptr,
		ImGuiWindowFlags_NoScrollbar |
		ImGuiWindowFlags_NoScrollWithMouse |
		ImGuiWindowFlags_NoTitleBar);

	ContentOrigin = ImGui::GetCursorScreenPos();
	ContentSize = ImGui::GetContentRegionAvail();
	ContentSize.x = std::max(1.0f, ContentSize.x);
	ContentSize.y = std::max(1.0f, ContentSize.y);
	bHovered = ImGui::IsWindowHovered();
	ControlRects.Reset();

	// 전체 캔버스를 한 번 확보한 뒤 각 렌더 타깃을 창 DrawList에 직접 그린다.
	// Image 항목 네 개를 따로 배치하면 ImGui 레이아웃과 클리핑 상태가 삽입 순서에
	// 영향을 받아, Core가 올바른 사각형을 줘도 아래쪽 행이 잘릴 수 있다.
	ImGui::Dummy(ContentSize);
	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	DrawList->PushClipRect(ContentOrigin,
		{ContentOrigin.x + ContentSize.x, ContentOrigin.y + ContentSize.y}, true);
	for (int32 ViewIndex = 0; ViewIndex < 4; ++ViewIndex)
	{
		const FViewSlot& Slot = Slots[ViewIndex];
		if (!Slot.bActive || !Slot.ColorTarget)
			continue;

		const ImVec2 ViewMin{ContentOrigin.x + Slot.Rect.X, ContentOrigin.y + Slot.Rect.Y};
		const ImVec2 ViewMax{ViewMin.x + Slot.Rect.Width, ViewMin.y + Slot.Rect.Height};
		DrawList->AddImage(Slot.ColorTarget->GetSRV(), ViewMin, ViewMax);
	}
	DrawList->PopClipRect();

	if (Slots[1].bActive || Slots[2].bActive || Slots[3].bActive)
	{
		// View Rect 사이에 비워 둔 gutter의 중앙에 Splitter 버튼을 배치한다.
		const float SplitX = (Slots[0].Rect.X + Slots[0].Rect.Width + Slots[1].Rect.X) * 0.5f;
		const float SplitY = (Slots[0].Rect.Y + Slots[0].Rect.Height + Slots[2].Rect.Y) * 0.5f;

		const ImVec2 VerticalMin{ContentOrigin.x + SplitX - SplitterThickness * 0.5f, ContentOrigin.y};
		const ImVec2 VerticalMax{VerticalMin.x + SplitterThickness, ContentOrigin.y + ContentSize.y};
		const ImVec2 HorizontalMin{ContentOrigin.x, ContentOrigin.y + SplitY - SplitterThickness * 0.5f};
		const ImVec2 HorizontalMax{ContentOrigin.x + ContentSize.x, HorizontalMin.y + SplitterThickness};

		ImGui::SetCursorScreenPos(VerticalMin);
		ImGui::InvisibleButton("##MultipleViewportsHorizontalSplitter", {SplitterThickness, ContentSize.y});
		ImGui::SetCursorScreenPos(HorizontalMin);
		ImGui::InvisibleButton("##MultipleViewportsVerticalSplitter", {ContentSize.x, SplitterThickness});

		const bool bVerticalHovered = bHovered && ImGui::IsMouseHoveringRect(VerticalMin, VerticalMax);
		const bool bHorizontalHovered = bHovered && ImGui::IsMouseHoveringRect(HorizontalMin, HorizontalMax);
		if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
		{
			bDraggingVerticalSplitter = bVerticalHovered;
			bDraggingHorizontalSplitter = bHorizontalHovered;
		}
		if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
		{
			bDraggingVerticalSplitter = false;
			bDraggingHorizontalSplitter = false;
		}

		const bool bVerticalHighlighted = bDraggingVerticalSplitter || bVerticalHovered;
		const bool bHorizontalHighlighted = bDraggingHorizontalSplitter || bHorizontalHovered;
		DrawList->AddRectFilled(VerticalMin, VerticalMax,
			bVerticalHighlighted ? SplitterHoverColor : SplitterColor);
		DrawList->AddRectFilled(HorizontalMin, HorizontalMax,
			bHorizontalHighlighted ? SplitterHoverColor : SplitterColor);

		if (bDraggingVerticalSplitter)
			PendingHorizontalDrag += ImGui::GetIO().MouseDelta.x;
		if (bDraggingHorizontalSplitter)
			PendingVerticalDrag += ImGui::GetIO().MouseDelta.y;

		if (bVerticalHighlighted && bHorizontalHighlighted)
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
		else if (bVerticalHighlighted)
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
		else if (bHorizontalHighlighted)
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
	}
	else
	{
		bDraggingVerticalSplitter = false;
		bDraggingHorizontalSplitter = false;
	}

	for (int32 ViewIndex = 0; ViewIndex < 4; ++ViewIndex)
	{
		if (!Slots[ViewIndex].bActive)
			continue;
		ImGui::SetCursorScreenPos({
			ContentOrigin.x + Slots[ViewIndex].Rect.X + 8.0f,
			ContentOrigin.y + Slots[ViewIndex].Rect.Y + 8.0f});
		ImGui::PushID(100 + ViewIndex);
		// 왼쪽 컨트롤 줄 전체를 한 그룹으로 묶어 피킹을 막을 영역으로 기록한다.
		ImGui::BeginGroup();
		// 기즈모 좌표계는 모든 View가 공유한다. 크기 모드는 항상 Local 축을 쓴다.
		if (Gizmo)
		{
			const bool bWorldSpace = Gizmo->GetSpace() == EGizmoSpace::World;
			// 두 글자 중 긴 쪽에 맞춰 눌러도 버튼 폭이 바뀌지 않게 한다.
			const float GizmoSpaceButtonWidth = std::max(ImGui::CalcTextSize("World").x, ImGui::CalcTextSize("Local").x)
				+ ImGui::GetStyle().FramePadding.x * 2.0f;
			if (ImGui::Button(bWorldSpace ? "World" : "Local", {GizmoSpaceButtonWidth, 0.0f}))
				Gizmo->SetSpace(bWorldSpace ? EGizmoSpace::Local : EGizmoSpace::World);
			ImGui::SetItemTooltip("Gizmo coordinate space (Scale always uses Local)");
			ImGui::SameLine();
		}
		// 조명·셰이딩 모델은 Lit일 때만 보인다.
        if (ViewportAdapter)
        {
            FViewRenderMode RenderMode = ViewportAdapter->GetViewRenderMode(ViewIndex);
            bool bChanged = false;

            int ViewMode = static_cast<int>(RenderMode.ViewMode);
            ImGui::SetNextItemWidth(CalcComboWidth(ViewModeNames, IM_ARRAYSIZE(ViewModeNames)));
            if (ImGui::Combo("##ViewMode", &ViewMode, ViewModeNames, IM_ARRAYSIZE(ViewModeNames)))
            {
                RenderMode.ViewMode = static_cast<EViewMode>(ViewMode);
                bChanged = true;
            }
            ImGui::SameLine();

            if (RenderMode.ViewMode == EViewMode::Lit)
            {
                const char* LightingModelLabels[] = {"Lambert", "Phong", "Blinn-Phong"};
                int LightingModel = static_cast<int>(RenderMode.LightingModel);
                ImGui::SetNextItemWidth(CalcComboWidth(LightingModelLabels, IM_ARRAYSIZE(LightingModelLabels)));
                if (ImGui::Combo("##LightingModel", &LightingModel, LightingModelLabels, IM_ARRAYSIZE(LightingModelLabels)))
                {
                    RenderMode.LightingModel = static_cast<ELightingModel>(LightingModel);
                    bChanged = true;
                }
                ImGui::SetItemTooltip("Lighting Model");
                ImGui::SameLine();

                int ShadingModel = static_cast<int>(RenderMode.ShadingModel);
                ImGui::SetNextItemWidth(CalcComboWidth(ShadingModelNames, IM_ARRAYSIZE(ShadingModelNames)));
                if (ImGui::Combo("##ShadingModel", &ShadingModel, ShadingModelNames, IM_ARRAYSIZE(ShadingModelNames)))
                {
                    RenderMode.ShadingModel = static_cast<EShadingModel>(ShadingModel);
                    bChanged = true;
                }
                ImGui::SetItemTooltip("Shading Model");
                ImGui::SameLine();
            }

            if (bChanged)
                ViewportAdapter->SetViewRenderMode(ViewIndex, RenderMode);
        }
        if (CurrentLayoutMode == ELayoutMode::QuadSplit)
		{
			if (ImGui::SmallButton("Single"))
			{
				RequestedLayoutMode = ELayoutMode::Single;
				RequestedSingleViewIndex = ViewIndex;
				bHasLayoutRequest = true;
			}
		}
		else if (ViewIndex == CurrentSingleViewIndex && ImGui::SmallButton("Quad"))
		{
			RequestedLayoutMode = ELayoutMode::QuadSplit;
			RequestedSingleViewIndex = ViewIndex;
			bHasLayoutRequest = true;
		}
		if (ViewportAdapter && ViewIndex == ViewportAdapter->GetEditorViewIndex() &&
			FStatOverlay::IsAnyEnabled() &&
			(FStatOverlay::IsEnabled(EStatFlags::Profile) ||
				FStatRegistry::Find(EditorStats::STAT_PickingTime)))
		{
			ImGui::SameLine();
			if (ImGui::SmallButton("Reset Stats"))
				FStatRegistry::Reset();
		}
		ImGui::EndGroup();
		RecordLastItemAsControl();

		DrawCameraMenu(ViewIndex, {
			ContentOrigin.x + Slots[ViewIndex].Rect.X + Slots[ViewIndex].Rect.Width,
			ContentOrigin.y + Slots[ViewIndex].Rect.Y});
		ImGui::PopID();
	}

	// 마지막으로 선택된 뷰포트만 오버레이
	for (int32 ViewIndex = 0; ViewIndex < 4; ++ViewIndex)
	{
		if (ViewIndex != ViewportAdapter->GetEditorViewIndex()) continue;
		if (!Slots[ViewIndex].bActive)
			continue;
		DrawStatOverlay(DrawList, {
			ContentOrigin.x + Slots[ViewIndex].Rect.X,
			ContentOrigin.y + Slots[ViewIndex].Rect.Y});
	}
	DrawCameraSpeedNotice(DrawList);

	ImGui::End();
	ImGui::PopStyleVar();
}

// 항목별 줄을 모아 한 번에 배경과 텍스트를 그린다.
void FViewportsPanel::DrawStatOverlay(ImDrawList* DrawList, const ImVec2& ViewMin) const
{
	if (!DrawList || !FStatOverlay::IsAnyEnabled())
		return;

	constexpr float BytesPerMegabyte = 1024.0f * 1024.0f;

	// 제목은 UE처럼 노란색, 값은 흰색으로 구분한다.
	struct FStatLine
	{
		FString Text;
		ImU32 Color;
	};
	TArray<FStatLine> Lines;

	if (FStatOverlay::IsEnabled(EStatFlags::FPS))
	{
		Lines.Add({"FPS", TitleColor});
		Lines.Add({std::format("  {:.1f} fps", FStatOverlay::GetFPS()), ValueColor});
		Lines.Add({std::format("  {:.2f} ms", FStatOverlay::GetFrameTimeMs()), ValueColor});
	}

	if (FStatOverlay::IsEnabled(EStatFlags::Memory))
	{
		Lines.Add({"Memory", TitleColor});
		Lines.Add({std::format("  Object  {:.2f} MB ({} allocs)",
			static_cast<double>(FStatOverlay::GetObjectAllocationBytes()) / BytesPerMegabyte,
			FStatOverlay::GetObjectAllocationCount()), ValueColor});
		Lines.Add({std::format("  Process {:.2f} MB",
			static_cast<double>(FStatOverlay::GetProcessWorkingSetBytes()) / BytesPerMegabyte), ValueColor});
	}
	if (FStatOverlay::IsEnabled(EStatFlags::Shadow))
	{
		Lines.Add({ "Shadow", TitleColor });
		const FShadowStats& shadow = FStatOverlay::GetShadowStats();

		Lines.Add({ std::format("  Lights  Dir {} / Point {} / Spot {}",
			shadow.DirectionalLightCount, shadow.PointLightCount, shadow.SpotLightCount), ValueColor });
		Lines.Add({ std::format("  Maps Rendered  {}", shadow.NumShadowMapsRendered), ValueColor });
		Lines.Add({ std::format("  Draw Calls     {}", shadow.NumShadowDrawCalls), ValueColor });
		Lines.Add({ std::format("  Skipped Lights {}", shadow.NumSkippedLights), ValueColor });
		Lines.Add({ std::format("  Skipped Casters {}", shadow.NumSkippedCasterDraws), ValueColor });
		Lines.Add({ std::format("  Cached Maps {}", shadow.NumCachedShadowMaps), ValueColor });
		Lines.Add({ std::format("  Memory         {:.2f} MB",
			static_cast<int64>(shadow.ShadowMapMemoryBytes) / BytesPerMegabyte), ValueColor });
	}

	struct FProfileStatLine
	{
		FString Text;
		ImU32 Color;
		FString Values[3];
	};
	TArray<FProfileStatLine> ProfileLines;
	if (FStatOverlay::IsEnabled(EStatFlags::Profile))
	{
		ProfileLines.Add({"CPU Profile (ms)", TitleColor, {"Last", "Avg", "Max"}});

		for (const auto& [Name, Data] : FStatRegistry::GetAll())
		{
			if (TStatId{ Name } == EditorStats::STAT_PickingTime)
				continue;

			FProfileStatLine Line{"  " + FString(Name), ValueColor};
			Line.Values[0] = std::format("{:.2f}", Data.GetLastMs());
			Line.Values[1] = std::format("{:.2f}", Data.GetRecentAverageMs());
			Line.Values[2] = std::format("{:.2f}", Data.GetMaxMs());
			ProfileLines.Add(Line);
		}
	}

	if (const FCycleStatData* PickingData = FStatRegistry::Find(EditorStats::STAT_PickingTime))
	{
		ProfileLines.Add({ "Picking", TitleColor });
		FProfileStatLine Line{"  Time (ms)", ValueColor};
		Line.Values[0] = std::format("Last: {:.2f}", PickingData->GetLastMs());
		Line.Values[1] = std::format("Attempts: {}", PickingData->CallCount);
		Line.Values[2] = std::format("Acc.: {:.2f}", PickingData->GetTotalMs());
		ProfileLines.Add(Line);
	}

	if (Lines.Num() == 0 && ProfileLines.Num() == 0)
		return;

	// 제어 위젯 한 줄 아래에서 시작해 Combo와 겹치지 않게 한다.
	const float LineHeight = ImGui::GetTextLineHeight();
	const ImVec2 Origin{
		ViewMin.x + StatOverlayMargin,
		ViewMin.y + StatOverlayMargin + ImGui::GetFrameHeight() + StatOverlayMargin};

	// 최대 너비 계산
	float MaxWidth = 0.0f;
	for (int32 Index = 0; Index < Lines.Num(); ++Index)
		MaxWidth = std::max(MaxWidth, ImGui::CalcTextSize(Lines[Index].Text.c_str()).x);

	const float MinValueWidth = ImGui::CalcTextSize("00000.00").x;
	float NameWidth = 0.0f;
	float ValueWidths[3] = { MinValueWidth, MinValueWidth, MinValueWidth };
	bool bHasColumns = false;

	for (const FProfileStatLine& Line : ProfileLines)
	{
		const float TextWidth = ImGui::CalcTextSize(Line.Text.c_str()).x;
		bHasColumns = true;
		NameWidth = std::max(NameWidth, TextWidth);
		for (int32 Column = 0; Column < 3; ++Column)
			ValueWidths[Column] = std::max(MinValueWidth, ImGui::CalcTextSize(Line.Values[Column].c_str()).x);
	}

	float ColumnRight[3]{};
	if (bHasColumns)
	{
		float Width = NameWidth;
		for (int32 Column = 0; Column < 3; ++Column)
		{
			Width += ImGui::GetFontSize() + ValueWidths[Column];
			ColumnRight[Column] = Origin.x + Width;
		}
		MaxWidth = std::max(MaxWidth, Width);
	}

	const ImVec2 BackgroundMin{Origin.x - StatOverlayPadding, Origin.y - StatOverlayPadding};
	const ImVec2 BackgroundMax{
		Origin.x + MaxWidth + StatOverlayPadding,
		Origin.y + LineHeight * static_cast<float>(Lines.Num() + ProfileLines.Num()) + StatOverlayPadding};
	DrawList->AddRectFilled(BackgroundMin, BackgroundMax, StatOverlayBackgroundColor, 4.0f);

	// Lines 그리기
	for (int32 Index = 0; Index < Lines.Num(); ++Index)
	{
		DrawList->AddText(
			{ Origin.x, Origin.y + LineHeight * static_cast<float>(Index) },
			Lines[Index].Color,
			Lines[Index].Text.c_str());
	}

	// ProfileLines 그리기
	for (int32 Index = 0; Index < ProfileLines.Num(); ++Index)
	{
		const FProfileStatLine& Line = ProfileLines[Index];
		const float Y = Origin.y + LineHeight * static_cast<float>(Lines.Num() + Index);
		DrawList->AddText({ Origin.x, Y }, Line.Color, Line.Text.c_str());

		for (int32 Column = 0; Column < 3; ++Column)
		{
			const FString& Text = Line.Values[Column];
			const float TextWidth = ImGui::CalcTextSize(Text.c_str()).x;
			DrawList->AddText({ ColumnRight[Column] - TextWidth, Y }, Line.Color, Text.c_str());
		}
	}
}

// View 크기에 맞춰 Color·Depth Texture와 렌더 정보를 재생성한다.
void FViewportsPanel::ResizeSlot(FViewSlot& Slot, const uint32 Width, const uint32 Height)
{
	D3D11_TEXTURE2D_DESC Desc{};
	Desc.Width = Width;
	Desc.Height = Height;
	Desc.MipLevels = 1;
	Desc.ArraySize = 1;
	Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	Desc.SampleDesc.Count = 1;
	Desc.Usage = D3D11_USAGE_DEFAULT;
	Desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	Slot.ColorTarget = RenderCommand::CreateTexture2D(Desc);
	// CopyResource로 되돌려 쓰므로 ColorTarget과 같은 Desc로 만든다
	Slot.FxaaTarget = RenderCommand::CreateTexture2D(Desc);

	// Linear 값을 8비트에 담으면 어두운 영역이 뭉개지고 1을 넘는 값이 잘리므로 씬 컬러는 float로 만든다
	Desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	Slot.SceneColorTarget = RenderCommand::CreateTexture2D(Desc);

	// Fog 패스가 깊이를 셰이더에서 읽으므로 DSV와 SRV를 둘 다 만들 수 있는 TYPELESS로 만든다
	Desc.Format = DXGI_FORMAT_R24G8_TYPELESS;
	Desc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
	Slot.DepthTarget = RenderCommand::CreateTexture2D(Desc);

	Slot.Width = Width;
	Slot.Height = Height;
	BuildViewRenderingInfos(Slot.SceneRenderingInfo, Slot.RenderingInfo,
		Slot.SceneColorTarget.get(), Slot.ColorTarget.get(), Slot.DepthTarget.get(), Width, Height);
}

void FViewportsPanel::BuildViewRenderingInfos(FRenderingInfo& OutSceneInfo, FRenderingInfo& OutViewInfo,
	FTexture2D* SceneColor, FTexture2D* ViewColor, FTexture2D* Depth, const uint32 Width, const uint32 Height)
{
	OutSceneInfo.ColorRenderTargets.Reset();
	OutSceneInfo.ViewportSetting.Width = Width;
	OutSceneInfo.ViewportSetting.Height = Height;
	FRenderingDesc SceneColorDesc{};
	SceneColorDesc.Texture = SceneColor;
	// 지우기 색은 작업 흐름에 따라 달라지므로 RenderOpaquePass에서 정한다.
	OutSceneInfo.ColorRenderTargets.Add(SceneColorDesc);
	OutSceneInfo.DepthStencil.Texture = Depth;

	OutViewInfo.ColorRenderTargets.Reset();
	OutViewInfo.ViewportSetting.Width = Width;
	OutViewInfo.ViewportSetting.Height = Height;
	FRenderingDesc ViewColorDesc{};
	ViewColorDesc.Texture = ViewColor;
	OutViewInfo.ColorRenderTargets.Add(ViewColorDesc);
	OutViewInfo.DepthStencil.Texture = Depth;
}
