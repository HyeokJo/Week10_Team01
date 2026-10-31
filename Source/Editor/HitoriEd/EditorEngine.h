#pragma once

#include "Engine/Engine.h"
#include "Core/Window.h"
#include "Core/Types.h"

#include "Render/Renderer.h"
#include "Render/RenderDevice.h"
#include "Render/Swapchain.h"
#include "Editor/EditorUI/ImGuiRenderer.h"
#include "Editor/Rendering/GridRenderer.h"
#include "Editor/Gizmo/GizmoRenderer.h"
#include "Render/LineBatcher.h"
#include "Render/FogRenderer.h"
#include "Render/FXAARenderer.h"
#include "Render/GammaEncodeRenderer.h"
#include "Render/DepthViewRenderer.h"
#include "Render/SceneRenderer.h"
#include "Render/SceneRenderTargets.h"

#include "Editor/EditorUI/EditorUI.h"
#include "Editor/OutputLog/OutputLogPanel.h"
#include "Editor/Details/DetailsPanel.h"
#include "Editor/Settings/SettingsPanel.h"
#include "Editor/Viewports/ViewportsPanel.h"
#include "Editor/LevelEditor/MultipleViewports/Adapter/MultipleViewportsAdapter.h"
#include "Editor/ContentDrawer/ContentDrawerPanel.h"
#include "Editor/Sequencer/Sequencer.h"

#include "Editor/Rendering/Outline.h"
#include "Editor/Rendering/OutLineRenderer.h"
#include "Editor/Outliner/OutlinerPanel.h"
#include "Editor/HitoriEd/EditorSelection.h"

#include "Render/SkyboxRenderer.h"

#include "PlayInEditorDataTypes.h"
#include "Core/Misc/Optional.h"

//Temp
#include "Text/Font.h"
#include "Text/TextRenderer.h"

class UEditorEngine : public UEngine
{
	DECLARE_CLASS(UEditorEngine, UEngine)

public:
	FEngineConfig GetConfig() const override;
	bool Init() override;
	void Tick(float DeltaTime) override;
	void PreExit() override;

	// Active View의 입력과 Picking 결과만 Gizmo 및 선택 상태에 반영한다.
	void UpdateGizmoAndPicking();
	// View 하나의 씬을 그린다.
	// 씬은 SceneRenderingInfo(Linear float)에 그리고, 인코딩한 결과와 에디터 표시는 ViewRenderingInfo(sRGB)에 그린다.
	void RenderFrame(const FSceneView& SceneView, const FSceneRenderSettings& Settings, UWorld& ViewWorld, const FRenderingInfo& SceneRenderingInfo, const FRenderingInfo& ViewRenderingInfo, FTexture2D* DecalBaseColorTarget, FTexture2D* FxaaTexture, FRenderQueue& InRenderQueue);
	// 네 View 결과와 ImGui를 메인 Swapchain 백버퍼에 합성한다. Present는 FEngineLoop가 한다.
	void PresentFrame();
	void DeleteActor(AActor* Actor);
	// 선택된 액터를 모두 삭제한다 (Delete 키, 아웃라이너 Delete 메뉴).
	void DeleteSelectedActors();
	void DeleteComponent(UActorComponent* Component);

	//Play 버튼을 눌렀을때 Play Session 실행을 요청한다.
	inline void RequestPlaySession() { bPlaySessionRequested = true; }
	inline void RequestEndPlayMap() { bRequestEndPlayMapQueued = true; bIsPaused = false; }

	//실제 PIE 를 실행
	void StartPlayInEditorSession();
	UWorld* CreatePIEWorldByDuplication(FWorldContext& PIEContext, UWorld* InEditorWorld);

	void EndPlayMap();

	bool IsPlaying() const { return PlayWorld != nullptr; }

	FWorldContext& GetEditorWorldContext();

	////FEditorDelegates::PrePIEEnded / EndPIE.Broadcast()
	//void TeardownPlaySession(const FWorldContext& PIEContext);            // 액터 EndPlay, 월드 정리(CleanupWorld)
	//void RestoreEditorWorld(UWorld* EditorWorld);            // GWorld를 에디터 월드로 복구
	//void DestroyWorldContext(UWorld* PlayWorld);

private:
	// 이번 프레임 DeltaTime을 패널에 전달하고 에디터 단축키를 처리한다.
	void BeginFrame(float DeltaTime);
	// 패널 요청과 입력을 Core Adapter에 전달해 레이아웃·카메라 상태를 갱신한다.
	void UpdateMultipleViewportState(float DeltaTime);
	// 월드를 정확히 한 번 Tick·Capture한 뒤 에디터 상호작용을 갱신한다.
	void TickWorldAndEditor(float DeltaTime);
	// 한 번 캡처한 월드 결과를 재사용해 현재 레이아웃의 각 View를 렌더한다.
	void RenderMultipleViewports();
	// PIE View가 PlayWorld 메인 카메라로 보이도록 Adapter Override를 갱신하고 게임 카메라 입력을 켜고 끈다.
	void UpdatePIEViewCamera();

	// 별도 PIE 창(New Editor Window)을 만들고 정리한다.
	bool CreatePIEWindow();
	void DestroyPIEWindow();
	// 창 크기에 맞춰 Swapchain과 오프스크린 타깃을 다시 만든다. 그릴 수 없으면 false다.
	bool UpdatePIEWindowTargets();
	// PIE 창의 오프스크린 결과를 백버퍼로 복사한다. Present는 EndFrame에서 한다.
	void CopyPIEWindowToBackbuffer();
	bool IsPIEInWindow() const { return PIEWindow != nullptr; }
	// View별 FXAA 임시 텍스처. 외부 View는 PIE 창 타깃을 쓴다.
	FTexture2D* GetFxaaTargetForView(int32 ViewIndex) const;
	// 화면 합성과 View 설정 보관으로 프레임을 마무리한다.
	void EndFrame();

	FSceneView MakeSceneView(int32 ViewIndex) const;
	FSceneRenderSettings MakeSceneRenderSettings() const;

	//Overlay Pass
	void RenderOverlayPass(const int32 ViewIndex, UWorld& ViewWorld, const FRenderingInfo& ViewRenderingInfo, const FMatrix& ViewProjection, const FVector& ViewCameraLocation, const FVector& ViewCameraForward, FRenderQueue& RenderQueue);

	FWorldContext* GetPIEWorldContext(int32 WorldPIEInstance = 0);
	UWorld* GetActiveWorld() const { return PlayWorld ? PlayWorld : EditorWorld; }
	// PIE를 시작한 View만 PlayWorld를, 나머지 View는 EditorWorld를 그린다.
	UWorld* GetWorldForView(int32 ViewIndex) const { return PlayWorld && ViewIndex == PIEViewIndex ? PlayWorld : EditorWorld; }
	// PIE 중이고 일시정지가 아닌 View는 Gizmo·Picking 등 에디터 상호작용을 하지 않는다.
	bool IsPlayingView(int32 ViewIndex) const { return PlayWorld && ViewIndex == PIEViewIndex && !bIsPaused; }

	void HandleOnPIEWorldChanged(UWorld* OldWorld, UWorld* NewWorld);

	// FEngineLoop 소유. OnInit에서 받아 둔다.
	FWindow* MainWindow = nullptr;
	FSwapchain* MainWindowSC = nullptr;
	FRenderer* Renderer = nullptr;

	TUniquePtr<FEditorUI> EditorUI;

	FSceneRenderer SceneRenderer;
	TUniquePtr<FImGuiRenderer> ImGuiRenderer;
	TUniquePtr<FGridRenderer> GridRenderer;
	TUniquePtr<FGizmoRenderer> GizmoRenderer;
	TUniquePtr<FTextRenderer> TextRenderer;
	TUniquePtr<FLineBatcher> LineBatcher;
	TUniquePtr<FGizmo> Gizmo;
	TUniquePtr<FOutline> Outline;
	TUniquePtr<FOutlineRenderer> OutlineRenderer;

	UFont* SystemFont;

	FOutputLogPanel* OutputLogPanel = nullptr;

	FDetailsPanel* DetailsPanel = nullptr;
	FSettingsPanel* SettingsPanel = nullptr;
	FViewportsPanel* ViewportsPanel = nullptr;
	FMultipleViewportsAdapter MultipleViewportsAdapter;
	FRenderQueue RenderQueue;
	FOutlinerPanel* OutlinerPanel = nullptr;
	FContentDrawerPanel* ContentDrawerPanel = nullptr;
	FLevelSequencePanel* LevelSequencerPanel = nullptr;

	UWorld* EditorWorld = nullptr;
	UWorld* PlayWorld = nullptr;
	// PIE 시작 시점의 편집 View. 별도 창이면 Adapter의 ExternalViewIndex다. PIE가 아니면 InvalidViewIndex다.
	int32 PIEViewIndex = InvalidViewIndex;

	// Play 버튼이 다음 세션을 띄울 위치
	EPlayModeType PlayMode = EPlayModeType::InViewport;

	// New Editor Window(PIE) 모드에서만 존재한다. Swapchain이 창 핸들을 쓰므로 창보다 먼저 해제한다.
	TUniquePtr<FWindow> PIEWindow;
	TUniquePtr<FSwapchain> PIESwapchain;
	FSceneRenderTargets PIETargets;
	bool bPIEWindowRendered = false;

	FVector UUIDLocation;

	FEditorCommand Save;

	FEditorCommand Play;
	FEditorCommand Pause;
	FEditorCommand Resume;
	FEditorCommand StepFrame;
	FEditorCommand Stop;

	bool bPlaySessionRequested = false;
	bool bRequestEndPlayMapQueued = false;
	bool bStepRequested = false;
	bool bIsPaused = false;

	void ResetSceneSelection();

	// 선택이 바뀌면 Primary 액터 기준으로 기즈모·디테일을, 선택 전체로 외곽선을 맞춘다.
	void SyncSelectionToEditors();
	// 기즈모는 Primary 액터의 루트만 움직이므로, 드래그하는 동안 같은 변화량을 나머지 선택 액터에 적용한다.
	void UpdateSelectionGizmoDrag();

	// 선택한 액터를 씬 파일과 같은 형식으로 복사해 두고, 붙여넣을 때 같은 위치에 새로 만든다.
	void CopySelectedActors();
	void PasteCopiedActors();
	// 선택된 액터가 화면에 들어오도록 마지막으로 편집한 View의 카메라를 옮긴다. 회전은 그대로 둔다 (F 키).
	void FocusOnSelection();

	FEditorSelection Selection;

	FEditorCommand Copy;
	FEditorCommand Paste;
	FEditorCommand Duplicate;
	FEditorCommand FocusSelection;
	// Space: 기즈모 모드를 이동 → 회전 → 크기 순으로 바꾼다.
	FEditorCommand CycleGizmoMode;

	// 레벨 툴바의 Add 메뉴. 편집 중인 View 카메라 앞에 스폰하고 새 액터를 선택한다.
	void SpawnActorFromToolbar(UClass* Class);
	TArray<uint8> CopiedActorData;

	// 다중 선택 기즈모 드래그를 시작한 순간의 월드 Transform. 0번은 Primary 액터의 루트다.
	struct FGizmoDragStart
	{
		USceneComponent* Component = nullptr;
		FVector Location;
		FQuat Rotation;
		FVector Scale;
	};
	TArray<FGizmoDragStart> GizmoDragStarts;

	void CreateNewScene();
	void OpenScene();
	void SaveCurrentScene();
	void SaveSceneAs();
};
