#include "EnginePCH.h"
#include "Editor/SkeletalMeshViewer/SkeletalMeshViewerPanel.h"

#include "Render/SkeletalMesh.h"
#include "Render/Texture.h"

#include <algorithm>
#include <cmath>

namespace
{
	constexpr float PaneSplitterThickness = 4.0f;
	constexpr float MinSideColumnWidth = 120.0f;
	constexpr float MinViewWidth = 100.0f;
	constexpr float MinPaneHeight = 80.0f;
	constexpr float BoneTreeIndent = 10.0f;
	constexpr float ControlMargin = 8.0f;
	constexpr float MinDetailScale = 1.0e-5f;   // 0이면 자식 뼈를 옮길 때 쓰는 부모 역행렬을 구할 수 없다
	constexpr float DetailDragRatio = 0.005f;
	constexpr float MinDetailDragSpeed = 1.0e-5f;

	// 리그마다 단위가 달라 Local 값이 수십일 수도(위치) 0.004일 수도(크기) 있다. 값 크기에 비례한 간격으로 끈다
	float MakeDragSpeed(const float* Values, const float DefaultSpeed)
	{
		const float Largest = std::max({ std::fabs(Values[0]), std::fabs(Values[1]), std::fabs(Values[2]) });
		return Largest > 0.0f ? std::max(Largest * DetailDragRatio, MinDetailDragSpeed) : DefaultSpeed;
	}

	// 보이지 않는 버튼을 끌어 크기를 조절한다. 드래그 양을 반환한다
	float DrawSplitter(const char* Id, const ImVec2& Size, const bool bVertical)
	{
		ImGui::InvisibleButton(Id, Size);
		const bool bActive = ImGui::IsItemActive();
		if (bActive || ImGui::IsItemHovered())
		{
			ImGui::SetMouseCursor(bVertical ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS);
		}
		ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
			ImGui::GetColorU32(bActive ? ImGuiCol_SeparatorActive : ImGuiCol_Separator));
		if (!bActive)
			return 0.0f;
		return bVertical ? ImGui::GetIO().MouseDelta.x : ImGui::GetIO().MouseDelta.y;
	}

	bool DrawDetailRow(const char* Label, const char* Id, float* Values, const float Speed)
	{
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(Label);
		ImGui::TableSetColumnIndex(1);
		ImGui::SetNextItemWidth(-1.0f);
		return ImGui::DragFloat3(Id, Values, Speed, 0.0f, 0.0f, "%.3f");
	}
}

bool FSkeletalMeshViewerPanel::Init()
{
	SetOpen(false);   // 메시를 열 때 나타난다
	return true;
}

FVector2 FSkeletalMeshViewerPanel::GetLocalMousePosition() const
{
	const ImVec2 Mouse = ImGui::GetMousePos();
	return { Mouse.x - ViewOrigin.x, Mouse.y - ViewOrigin.y };
}

void FSkeletalMeshViewerPanel::OnRender()
{
	bViewHovered = false;
	bWindowHovered = false;
	bWindowFocused = false;

	ImGui::SetNextWindowSize(ImVec2(1280.0f, 720.0f), ImGuiCond_FirstUseEver);
	// FInputSystem은 메인 창의 메시지만 받는다. 창을 밖으로 빼면 카메라 입력이 끊기므로 메인 창 안에 둔다
	ImGui::SetNextWindowViewport(ImGui::GetMainViewport()->ID);
	if (bFocusRequested)
	{
		ImGui::SetNextWindowFocus();
		bFocusRequested = false;
	}

	bool bKeepOpen = true;
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	const bool bVisible = ImGui::Begin(GetPanelName(), &bKeepOpen,
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	ImGui::PopStyleVar();

	// 메인 창에 고정했으므로 밖으로 나간 부분은 보이지 않고 잡을 수도 없다. 떠 있을 때는 안으로 되돌린다
	if (!ImGui::IsWindowDocked())
	{
		const ImGuiViewport* Main = ImGui::GetMainViewport();
		const ImVec2 CurrentSize = ImGui::GetWindowSize();
		const ImVec2 ClampedSize(std::min(CurrentSize.x, Main->Size.x), std::min(CurrentSize.y, Main->Size.y));
		if (ClampedSize.x != CurrentSize.x || ClampedSize.y != CurrentSize.y)
		{
			ImGui::SetWindowSize(ClampedSize);
		}

		const ImVec2 CurrentPos = ImGui::GetWindowPos();
		const ImVec2 ClampedPos(
			FMath::Clamp(CurrentPos.x, Main->Pos.x, Main->Pos.x + Main->Size.x - ClampedSize.x),
			FMath::Clamp(CurrentPos.y, Main->Pos.y, Main->Pos.y + Main->Size.y - ClampedSize.y));
		if (ClampedPos.x != CurrentPos.x || ClampedPos.y != CurrentPos.y)
		{
			ImGui::SetWindowPos(ClampedPos);
		}
	}

	if (!bKeepOpen)
	{
		SetOpen(false);
	}
	else if (bVisible)
	{
		bWindowHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
		bWindowFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

		const ImVec2 Origin = ImGui::GetCursorScreenPos();
		const ImVec2 Available = ImGui::GetContentRegionAvail();
		if (Viewer && Viewer->IsOpen() && Viewer->GetTargets().ViewColor)
		{
			// 창이 좁아져도 렌더 결과 영역이 남게 하되, 오른쪽 열의 최소 폭을 먼저 지킨다
			const float MaxColumnWidth = std::max(MinSideColumnWidth, Available.x - PaneSplitterThickness - MinViewWidth);
			SideColumnWidth = FMath::Clamp(SideColumnWidth, MinSideColumnWidth, MaxColumnWidth);
			const float Height = std::max(1.0f, Available.y);
			const float ViewWidth = std::max(1.0f, Available.x - PaneSplitterThickness - SideColumnWidth);

			// 도구 버튼이 커서를 옮기므로 영역마다 시작 위치를 직접 지정한다
			DrawView(ImVec2(ViewWidth, Height));

			ImGui::SetCursorScreenPos(ImVec2(Origin.x + ViewWidth, Origin.y));
			SideColumnWidth -= DrawSplitter("##SideColumnSplitter", ImVec2(PaneSplitterThickness, Height), true);   // 오른쪽 열이라 왼쪽으로 끌면 넓어진다

			ImGui::SetCursorScreenPos(ImVec2(Origin.x + ViewWidth + PaneSplitterThickness, Origin.y));
			DrawSideColumn(ImVec2(SideColumnWidth, Height));
		}
		else
		{
			ImGui::TextUnformatted("Double-click a skeletal mesh .fbx in the Content Drawer.");
		}
	}

	ImGui::End();
}

void FSkeletalMeshViewerPanel::DrawView(const ImVec2& Size)
{
	ViewOrigin = ImGui::GetCursorScreenPos();
	ViewSize = Size;

	// 빈 영역을 누르면 창이 끌려가므로 버튼이 클릭을 받아 둔다. 위에 도구 버튼이 겹치므로 겹침을 허용한다
	ImGui::SetNextItemAllowOverlap();
	ImGui::InvisibleButton("##ViewerCanvas", ViewSize,
		ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
	bViewHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem | ImGuiHoveredFlags_AllowWhenOverlappedByItem);
	ImGui::GetWindowDrawList()->AddImage(Viewer->GetTargets().ViewColor->GetSRV(), ViewOrigin,
		ImVec2(ViewOrigin.x + ViewSize.x, ViewOrigin.y + ViewSize.y));

	DrawGizmoControls();
}

void FSkeletalMeshViewerPanel::DrawGizmoControls()
{
	FGizmo& Gizmo = Viewer->GetGizmo();
	bool bControlHovered = false;

	const auto ModeButton = [&](const char* Label, const EGizmoMode Mode, const char* Tooltip)
	{
		const bool bCurrent = Gizmo.GetMode() == Mode;
		if (bCurrent)
		{
			ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetColorU32(ImGuiCol_ButtonActive));
		}
		if (ImGui::Button(Label))
		{
			Gizmo.SetMode(Mode);
		}
		bControlHovered |= ImGui::IsItemHovered();
		ImGui::SetItemTooltip("%s", Tooltip);
		if (bCurrent)
		{
			ImGui::PopStyleColor();
		}
		ImGui::SameLine();
	};

	ImGui::SetCursorScreenPos(ImVec2(ViewOrigin.x + ControlMargin, ViewOrigin.y + ControlMargin));
	ModeButton("Move", EGizmoMode::Location, "Move the bone (Space cycles the mode)");
	ModeButton("Rotate", EGizmoMode::Rotation, "Rotate the bone (Space cycles the mode)");
	ModeButton("Scale", EGizmoMode::Scale, "Scale the bone (Space cycles the mode)");

	const bool bWorldSpace = Gizmo.GetSpace() == EGizmoSpace::World;
	if (ImGui::Button(bWorldSpace ? "World" : "Local"))
	{
		Gizmo.SetSpace(bWorldSpace ? EGizmoSpace::Local : EGizmoSpace::World);
	}
	bControlHovered |= ImGui::IsItemHovered();
	ImGui::SetItemTooltip("Gizmo coordinate space (Scale always uses Local)");

	// 버튼을 누를 때 뒤의 뼈 기즈모가 함께 잡히지 않게 한다
	bViewHovered = bViewHovered && !bControlHovered;
}

void FSkeletalMeshViewerPanel::DrawSideColumn(const ImVec2& Size)
{
	const USkeletalMeshDebugComponent* Component = Viewer->GetMeshComponent();
	const USkeletalMesh* Mesh = Component ? Component->GetSkeletalMesh() : nullptr;
	if (!Mesh)
		return;

	if (BoneTreeMesh != Mesh)
	{
		RebuildBoneTree(*Mesh);
	}

	const ImVec2 Origin = ImGui::GetCursorScreenPos();
	const float MaxDetailsHeight = std::max(MinPaneHeight, Size.y - PaneSplitterThickness - MinPaneHeight);
	DetailsHeight = FMath::Clamp(DetailsHeight, MinPaneHeight, MaxDetailsHeight);
	const float TreeHeight = std::max(1.0f, Size.y - PaneSplitterThickness - DetailsHeight);

	DrawHierarchy(ImVec2(Size.x, TreeHeight));

	ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + TreeHeight));
	DetailsHeight -= DrawSplitter("##DetailsSplitter", ImVec2(Size.x, PaneSplitterThickness), false);   // 아래쪽 칸이라 위로 끌면 넓어진다

	ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + TreeHeight + PaneSplitterThickness));
	DrawBoneDetails(ImVec2(Size.x, DetailsHeight));
}

void FSkeletalMeshViewerPanel::DrawHierarchy(const ImVec2& Size)
{
	const USkeletalMeshDebugComponent* Component = Viewer->GetMeshComponent();

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));
	ImGui::BeginChild("##BoneHierarchy", Size, ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_HorizontalScrollbar);
	ImGui::PopStyleVar();

	const int32 Selected = Component->GetSelectedBone();
	bRevealSelectedBone = Selected != LastSeenSelectedBone && Selected >= 0;
	LastSeenSelectedBone = Selected;

	ImGui::Text("Bone Hierarchy (%d)", BoneChildren.Num());
	ImGui::Separator();
	// 뼈 계층은 깊어서 기본 들여쓰기로는 이름이 오른쪽 밖으로 밀려난다
	ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, BoneTreeIndent);
	for (const int32 Root : RootBones)
	{
		DrawBoneNode(*Component, Root);
	}
	ImGui::PopStyleVar();

	// 목록의 빈 곳을 누르면 선택을 푼다
	if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered())
	{
		SelectBone(-1);
	}

	ImGui::EndChild();
}

void FSkeletalMeshViewerPanel::DrawBoneNode(const USkeletalMeshDebugComponent& Component, const int32 BoneIndex)
{
	const bool bLeaf = BoneChildren[BoneIndex].IsEmpty();

	ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick
		| ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
	if (bLeaf)
	{
		Flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
	}
	if (Component.GetSelectedBone() == BoneIndex)
	{
		Flags |= ImGuiTreeNodeFlags_Selected;
	}

	const FString& Name = Component.GetSkeletalMesh()->GetSkeleton().Bones[BoneIndex].Name;
	// "Humanoid:LeftHand"처럼 모든 뼈에 붙는 네임스페이스는 줄여서 보여 준다. 전체 이름은 툴팁으로 볼 수 있다
	const size_t NamespaceEnd = Name.rfind(':');
	const char* ShortName = Name.c_str() + (NamespaceEnd == FString::npos ? 0 : NamespaceEnd + 1);

	if (bRevealSelectedBone)
	{
		// 선택한 뼈의 조상이면 펼쳐서 그 뼈가 보이게 한다
		const TArray<FBoneInfo>& Bones = Component.GetSkeletalMesh()->GetSkeleton().Bones;
		for (int32 Ancestor = Bones[Component.GetSelectedBone()].ParentIndex; Ancestor >= 0; Ancestor = Bones[Ancestor].ParentIndex)
		{
			if (Ancestor == BoneIndex)
			{
				ImGui::SetNextItemOpen(true);
				break;
			}
		}
	}

	const bool bOpen = ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<intptr_t>(BoneIndex)), Flags, "%s", ShortName);
	ImGui::SetItemTooltip("%s", Name.c_str());

	if (bRevealSelectedBone && Component.GetSelectedBone() == BoneIndex)
	{
		ImGui::SetScrollHereY();
	}

	// 화살표로 접고 펼 때는 선택하지 않는다
	if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
	{
		SelectBone(BoneIndex);
	}

	if (bOpen && !bLeaf)
	{
		for (const int32 Child : BoneChildren[BoneIndex])
		{
			DrawBoneNode(Component, Child);
		}
		ImGui::TreePop();
	}
}

void FSkeletalMeshViewerPanel::DrawBoneDetails(const ImVec2& Size)
{
	USkeletalMeshDebugComponent* Component = Viewer->GetMeshComponent();
	const int32 Bone = Component->GetSelectedBone();

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));
	ImGui::BeginChild("##BoneDetails", Size, ImGuiChildFlags_AlwaysUseWindowPadding);
	ImGui::PopStyleVar();

	ImGui::TextUnformatted("Bone Details");
	ImGui::Separator();

	if (Bone < 0)
	{
		ImGui::TextDisabled("Select a bone in the hierarchy.");
	}
	else
	{
		const TArray<FBoneInfo>& Bones = Component->GetSkeletalMesh()->GetSkeleton().Bones;
		const int32 Parent = Bones[Bone].ParentIndex;
		ImGui::TextWrapped("%s", Bones[Bone].Name.c_str());
		ImGui::TextDisabled("Index %d   Parent: %s", Bone, Parent >= 0 ? Bones[Parent].Name.c_str() : "(none)");
		ImGui::Spacing();

		const FMatrix CurrentLocal = Component->GetBoneLocalTransform(Bone);
		if (DetailBone != Bone || DetailLocal != CurrentLocal)
		{
			DetailBone = Bone;
			DetailLocal = CurrentLocal;
			DetailTransform = FSkeletalMeshViewer::DecomposeMatrix(CurrentLocal);
		}

		float Location[3] = { DetailTransform.Location.X, DetailTransform.Location.Y, DetailTransform.Location.Z };
		// 레벨 디테일 패널과 같이 X/Y/Z 칸이 실제 회전축과 맞게 Roll/Pitch/Yaw 순서로 보여 준다
		float Rotation[3] = { DetailTransform.Rotation.Roll, DetailTransform.Rotation.Pitch, DetailTransform.Rotation.Yaw };
		float Scale[3] = { DetailTransform.Scale.X, DetailTransform.Scale.Y, DetailTransform.Scale.Z };
		bool bChanged = false;

		if (ImGui::BeginTable("BoneTransform", 2))
		{
			ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("Location").x);
			ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
			bChanged |= DrawDetailRow("Location", "##BoneLocation", Location, MakeDragSpeed(Location, 0.01f));
			bChanged |= DrawDetailRow("Rotation", "##BoneRotation", Rotation, 0.5f);
			bChanged |= DrawDetailRow("Scale", "##BoneScale", Scale, MakeDragSpeed(Scale, 0.01f));
			ImGui::EndTable();
		}
		ImGui::TextDisabled("Relative to the parent bone");

		if (bChanged)
		{
			DetailTransform.Location = FVector(Location[0], Location[1], Location[2]);
			DetailTransform.Rotation.Roll = Rotation[0];
			DetailTransform.Rotation.Pitch = Rotation[1];
			DetailTransform.Rotation.Yaw = Rotation[2];
			const auto ClampScale = [](const float Value)
			{
				return std::fabs(Value) < MinDetailScale ? (Value < 0.0f ? -MinDetailScale : MinDetailScale) : Value;
			};
			DetailTransform.Scale = FVector(ClampScale(Scale[0]), ClampScale(Scale[1]), ClampScale(Scale[2]));

			Component->SetBoneLocalTransform(Bone, DetailTransform.GetLocalMatrix());
			DetailLocal = Component->GetBoneLocalTransform(Bone);   // 값을 들고 있으므로 다음 프레임에 다시 분해하지 않는다
		}

		ImGui::Spacing();
		if (ImGui::Button("Reset Bone"))
		{
			Component->SetBoneLocalTransform(Bone, Bones[Bone].LocalBindPose);
		}
		ImGui::SameLine();
		if (ImGui::Button("Reset All"))
		{
			Component->ResetToRefPose();
		}
	}

	ImGui::EndChild();
}

void FSkeletalMeshViewerPanel::SelectBone(const int32 BoneIndex)
{
	if (USkeletalMeshDebugComponent* Component = Viewer->GetMeshComponent())
	{
		Component->SetSelectedBone(BoneIndex);
		LastSeenSelectedBone = Component->GetSelectedBone();   // 지금 보고 있는 줄이니 스크롤하지 않는다
	}
}

void FSkeletalMeshViewerPanel::RebuildBoneTree(const USkeletalMesh& Mesh)
{
	BoneTreeMesh = &Mesh;
	DetailBone = -1;

	const TArray<FBoneInfo>& Bones = Mesh.GetSkeleton().Bones;
	BoneChildren.Reset();
	BoneChildren.SetNum(Bones.Num());
	RootBones.Reset();
	for (int32 i = 0; i < Bones.Num(); i++)
	{
		const int32 Parent = Bones[i].ParentIndex;
		if (Parent >= 0)
		{
			BoneChildren[Parent].Add(i);
		}
		else
		{
			RootBones.Add(i);
		}
	}
}
