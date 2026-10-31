#include "EnginePCH.h"
#include "SceneRenderer.h"

#include "ColorWorkflow.h"
#include "DepthViewRenderer.h"
#include "FogRenderer.h"
#include "FXAARenderer.h"
#include "GammaEncodeRenderer.h"
#include "RenderCommand.h"
#include "Renderer.h"
#include "RenderingInfo.h"
#include "SkyboxRenderer.h"
#include "Component/TextRenderComponent.h"
#include "Core/ResourcePaths.h"
#include "Core/StatOverlay.h"
#include "Core/Windows/WindowsPlatformTime.h"
#include "Engine/Scene.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Text/TextRenderer.h"
#include "UObject/UObjectIterator.h"
#include "DecalRenderer.h"
#include "Engine/DecalSceneProxy.h"

#include <algorithm>

FSceneRenderer::FSceneRenderer() = default;

FSceneRenderer::~FSceneRenderer() = default;

void FSceneRenderer::Init(FRenderer* InRenderer)
{
	Renderer = InRenderer;

	SkyboxRenderer = MakeUnique<FSkyboxRenderer>();
	SkyboxRenderer->Init(ResourcePaths::DefaultSkyPanorama);

	FogRenderer = MakeUnique<FFogRenderer>();
	FogRenderer->Init(Renderer);

	FXAARenderer = MakeUnique<FFXAARenderer>();
	FXAARenderer->Init();

	GammaEncodeRenderer = MakeUnique<FGammaEncodeRenderer>();
	GammaEncodeRenderer->Init();

	DepthViewRenderer = MakeUnique<FDepthViewRenderer>();
	DepthViewRenderer->Init();

	TextRenderer = MakeUnique<FTextRenderer>();
	TextRenderer->Init();

	DecalRenderer = MakeUnique<FDecalRenderer>();
	DecalRenderer->Init();
}

void FSceneRenderer::BeginFrame()
{
	bShadowAtlasRenderedThisFrame = false;
	FStatOverlay::ResetDecalStats();
}

void FSceneRenderer::Render(UWorld& World, const FSceneView& SceneView, const FSceneRenderSettings& Settings,
	const FRenderingInfo& SceneInfo, const FRenderingInfo& ViewInfo, FTexture2D* FxaaTemp, FRenderQueue& RenderQueue)
{
	FLightingConstants Lighting;
	FShadowStats ShadowStats{};
	TArray<FPointLightInfo> PointLights;
	TArray<FSpotLightInfo> SpotLights;
	TArray<FDirectionalLightInfo> DirectionalLights;
	TArray<FShadowInfo> ShadowInfos;

	//프리뷰를 보여줄 LightProxy
	FLightSceneProxy* PreviewShadowMapLightProxy = nullptr;

	World.GetScene().BuildLightingConstants(Lighting);
	const bool bDrawShadows = Settings.bDrawShadows;

	FFrustumPlanes FrustumPlanes = ExtractFrustumPlanes(SceneView.ViewProjection);

	World.GetScene().BuildStructuredLightAndShadowData(PointLights, SpotLights, DirectionalLights, ShadowInfos, PreviewShadowMapLightProxy, ShadowStats, bDrawShadows, &FrustumPlanes, SceneView.Location);

	// 라이트 컬링은 View마다 다르지만 아틀라스는 하나라서, 다른 View가 방금 자기 라이트로 덮어썼을 수 있다.
	// 아틀라스에 지금 들어 있는 것이 이 View의 목록과 같고 그 뒤로 씬이 바뀌지 않았을 때만 재사용한다.
	const bool bSameShadowSet = ShadowAtlasWorld == &World
		&& ShadowAtlasInfos.Num() == ShadowInfos.Num()
		&& (ShadowInfos.IsEmpty() || std::memcmp(ShadowAtlasInfos.GetData(), ShadowInfos.GetData(), sizeof(FShadowInfo) * ShadowInfos.Num()) == 0);
	const bool bSceneShadowUnchanged = !World.GetScene().IsShadowDirty() || bShadowAtlasRenderedThisFrame;
	const bool bCacheHit = FScene::bCacheWholeSceneShadows && bSameShadowSet && bSceneShadowUnchanged;

	Renderer->SetLighting(Lighting, PointLights, SpotLights);
	Renderer->SetShadowFilterSettings(Settings.ShadowFilter);

	const FShadowInfo* DirectionalShadowInfo = bDrawShadows ? World.GetScene().GetDirectionalShadowInfo() : nullptr;

	//Shadow Info StructuredBuffer, ShadowMap 만들기
	if (DirectionalShadowInfo != nullptr || ShadowInfos.Num() > 0)
	{
		// 아틀라스를 다시 그리면 무엇을 그렸는지 기록한다. RenderShadowPass가 타일 배치를 채우므로 그 전에 복사한다.
		if (!bCacheHit)
		{
			ShadowAtlasWorld = &World;
			ShadowAtlasInfos = ShadowInfos;
			bShadowAtlasRenderedThisFrame = true;
		}

		Renderer->RenderShadowPass(
			DirectionalShadowInfo,
			Lighting.Directional.Direction,
			SceneView.Location,
			SceneView.Forward,
			SceneView.FovDegrees,
			SceneView.AspectRatio,
			ShadowInfos,
			World.GetScene().Proxies,
			PreviewShadowMapLightProxy,
			ShadowStats,
			Settings.bVisualizeCascades,
			bCacheHit
		);
	}

	Renderer->BindShadowForShading();

	FStatOverlay::SetShadowStats(ShadowStats);

	// 1. 불투명·반투명 Pass (Linear 씬 컬러)
	RenderOpaquePass(SceneView, Settings, World, SceneInfo, RenderQueue, Lighting.Directional);

	if (Settings.bDepthView)
	{
		// 깊이 시각화는 표시용 타깃을 통째로 덮어쓰므로 인코딩이 필요 없다
		RenderDepthPass(SceneView, ViewInfo);
	}
	else
	{
		// 데이터 시각화 뷰는 씬 컬러 값 자체가 결과이므로 안개·인코딩·FXAA로 값을 바꾸지 않는다.
		const EViewMode ViewMode = SceneView.RenderMode.ViewMode;
		const bool bVisualizeData = IsDataVisualization(ViewMode);

		//직교인 경우 안개 Pass를 그리지 않는다.
		if (!SceneView.bOrthographic && !bVisualizeData && Settings.bExponentialHeightFog)
			// 2. 안개 Pass (Linear 씬 컬러)
			RenderFogPass(SceneView, World, SceneInfo);

		// 3. Gamma Encode Pass: Linear 씬 컬러 → (Lit이면 ACES) → 표시용 sRGB
		const bool bEncode = !bVisualizeData && ColorWorkflow::bLinear;
		const bool bToneMap = bEncode && Settings.bToneMapping && ViewMode == EViewMode::Lit;
		RenderGammaEncodePass(SceneInfo, ViewInfo, bEncode, bToneMap, Settings.ExposureEV, SceneView.bConstrainAspectRatio, SceneView.ConstrainedAspectRatio, Settings.DamageEffectIntensity, Settings.ScreenEffects);

		// 4. FXAA Pass: 밝기 계산이 감마 공간을 전제로 하므로 인코딩 뒤에 한다
		if (!bVisualizeData && Settings.bEnableFXAA)
			RenderFXAAPass(FxaaTemp, ViewInfo);
	}
}

void FSceneRenderer::RenderOpaquePass(const FSceneView& SceneView, const FSceneRenderSettings& Settings, const UWorld& ViewWorld, 
									  const FRenderingInfo& SceneInfo, FRenderQueue& RenderQueue, const FDirectionalLightInfo& DirectionalLight)
{
	// 뷰 모드가 셰이더 permutation을, Wireframe 여부가 Fill Mode를 정한다. 삼각형 연결은 그대로다.
	const FViewRenderMode ViewRenderMode = SceneView.RenderMode;
	const bool bVisualizeLightCount = Settings.bVisualizeTileLightCount;
	if (ViewRenderMode.ViewMode == EViewMode::Lit)
	{
		Renderer->EnsureTileLightCapacity(
			SceneView.ViewProjection,
			SceneInfo.ViewportSetting.Width,
			SceneInfo.ViewportSetting.Height, bVisualizeLightCount);
	}

	// 배경은 화면에서 sRGB 0.3 회색으로 보여야 한다. Linear 작업 흐름에서는 인코딩될 것이므로 디코딩해 지운다.
	FRenderingInfo PassInfo = SceneInfo;
	const float ClearValue = ColorWorkflow::bLinear ? FLinearColor::SRGBToLinear(0.3f) : 0.3f;
	PassInfo.ColorRenderTargets[0].ClearValue.ColorClearValue = FVector4(ClearValue, ClearValue, ClearValue, 1.0f);
	RenderCommand::BeginRenderPass(PassInfo);

	const bool bDrawPrimitives = Settings.bDrawPrimitives;
	const ERasterizerState SceneRasterizerState = ViewRenderMode.ViewMode == EViewMode::Wireframe
		? ERasterizerState::Wireframe : ERasterizerState::SolidBack;

	// 렌더 루프 — 반드시 RenderAll보다 먼저
	// 직교는 시선이 모두 평행해 파노라마의 한 점만 확대되므로 하늘을 그리지 않고 지우기 색을 배경으로 쓴다.
	// 데이터 시각화 뷰도 하늘은 데이터가 없으므로 그리지 않는다.
	if (!SceneView.bOrthographic && !IsDataVisualization(ViewRenderMode.ViewMode))
	{
		SkyboxRenderer->OnRender(SceneView.ViewProjection, SceneView.Location, DirectionalLight);
	}

	if (bDrawPrimitives)
	{
		RenderCommand::SetRasterizerState(SceneRasterizerState);
		RenderCommand::SetBlendState(EBlendState::Opaque);
		RenderCommand::SetDepthStencilState(EDepthStencilState::Default);

		RenderCommand::SetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		Renderer->SetViewMode(ViewRenderMode);
		// 불투명을 먼저 그리고 반투명을 먼 것부터 그린다.
		// Grid는 깊이를 쓰지 않는 반투명 위에도 겹칠 수 있지만, 언리얼의 에디터 프리미티브처럼 Overlay Pass에서 마지막에 합성한다.
		Renderer->RenderQueueSorting(RenderQueue, SceneView.ViewProjection, SceneView.Location);
		Renderer->RenderOpaque(SceneView.ViewProjection);
		RenderDecalPass(SceneView, Settings, ViewWorld);
		DrawWorldText(SceneView, ViewWorld);
		RenderCommand::SetRasterizerState(SceneRasterizerState); // TextRenderer가 상태를 바꾸므로 되돌림
		Renderer->RenderTranslucent(SceneView.ViewProjection);
		// 장면 Wireframe이 Grid·Gizmo·UI로 전파되지 않도록 복원한다.
		RenderCommand::SetRasterizerState(ERasterizerState::SolidBack);
	}


	RenderCommand::EndRenderPass();
}

void FSceneRenderer::DrawWorldText(const FSceneView& SceneView, const UWorld& ViewWorld)
{
	for (TObjectIterator<UTextRenderComponent> TextComponent; TextComponent; ++TextComponent)
	{
		if (!TextComponent || !TextComponent->GetFont() || !TextComponent->IsVisible())
		{
			continue;
		}
		// 에디터·PIE 월드의 컴포넌트가 함께 순회되므로 이 View의 월드만 그린다.
		if (!TextComponent->GetOwner() || TextComponent->GetOwner()->GetWorld() != &ViewWorld)
		{
			continue;
		}

		TextRenderer->OnRender(
			TextComponent->GetText(),
			TextComponent->GetWorldMatrix(),
			TextComponent->GetTextSize(),
			*TextComponent->GetFont(),
			SceneView.ViewProjection
		);
	}
}

void FSceneRenderer::RenderFogPass(const FSceneView& SceneView, const UWorld& ViewWorld, const FRenderingInfo& SceneInfo)
{
	const TArray<FExponentialHeightFogSceneInfo>& SceneFogInfos = ViewWorld.GetScene().FogInfos;

	if (SceneFogInfos.Num() == 0)
		return;

	FRenderingInfo PassInfo = SceneInfo;

	//Load는 기존 내용을 그대로 두고 그 위에 그림을 그린다는뜻
	for (FRenderingDesc& Color : PassInfo.ColorRenderTargets)
		Color.LoadOp = ERenderTargetLoadOp::Load;

	PassInfo.DepthStencil.Texture = nullptr;
	RenderCommand::BeginRenderPass(PassInfo);

	FogRenderer->OnRender(SceneInfo.DepthStencil.Texture, SceneView.ViewProjection, SceneView.Location, SceneFogInfos[0].FogInfo);

	//Fog는 여러개있더라도 1개의 Fog만 인식해서 그리도록 해야한다.

	RenderCommand::EndRenderPass();
}

void FSceneRenderer::RenderGammaEncodePass(const FRenderingInfo& SceneInfo, const FRenderingInfo& ViewInfo, bool bEncode, bool bToneMap, 
	float ExposureEV, bool bLetterbox, float TargetAspect, float InDamageIntensity, const FCameraScreenEffects& ScreenEffect)
{
	FTexture2D* SceneColor = SceneInfo.ColorRenderTargets[0].Texture;
	FTexture2D* ViewColor = ViewInfo.ColorRenderTargets[0].Texture;
	if (!SceneColor || !ViewColor)
		return;

	// 출력은 표시용 타깃 전체를 덮어쓰므로 Clear·깊이가 필요 없다
	FRenderingInfo PassInfo;
	PassInfo.ViewportSetting = ViewInfo.ViewportSetting;

	FRenderingDesc ColorDesc{};
	ColorDesc.Texture = ViewColor;
	ColorDesc.LoadOp = ERenderTargetLoadOp::DontCare;
	PassInfo.ColorRenderTargets.Add(ColorDesc);
	PassInfo.DepthStencil.Texture = nullptr;

	RenderCommand::BeginRenderPass(PassInfo);
	GammaEncodeRenderer->OnRender(SceneColor, bEncode, bToneMap, ExposureEV, bLetterbox, TargetAspect, InDamageIntensity,
		ViewInfo.ViewportSetting.Width, ViewInfo.ViewportSetting.Height, ScreenEffect);

	RenderCommand::EndRenderPass();

	// 다음 프레임에 SceneColor를 RTV로 쓸 때 SRV로 남아 있으면 D3D가 강제로 해제하므로 먼저 비운다
	RenderCommand::UnbindShaderResource(0, EShaderBindFlagBits::Pixel);
}

void FSceneRenderer::RenderFXAAPass(FTexture2D* FxaaTarget, const FRenderingInfo& ViewInfo)
{
	FTexture2D* SceneColor = ViewInfo.ColorRenderTargets[0].Texture;
	if (!SceneColor || !FxaaTarget)
		return;

	// 출력은 임시 텍스처, 깊이 없음, 어차피 전부 덮어쓰니 Clear 불필요
	FRenderingInfo PassInfo;
	PassInfo.ViewportSetting = ViewInfo.ViewportSetting;

	FRenderingDesc ColorDesc{};
	ColorDesc.Texture = FxaaTarget;
	ColorDesc.LoadOp = ERenderTargetLoadOp::DontCare;
	PassInfo.ColorRenderTargets.Add(ColorDesc);
	PassInfo.DepthStencil.Texture = nullptr;

	//장면 텍스처를 읽어서 임시 텍스처에 FXAA 결과를 그린다
	RenderCommand::BeginRenderPass(PassInfo);
	FXAARenderer->OnRender(SceneColor);
	RenderCommand::EndRenderPass();

	// SceneColor가 SRV로 남아 있으면 Overlay 패스에서 RTV로 쓸 때 D3D가 강제로 해제하므로 먼저 비운다
	RenderCommand::UnbindShaderResource(0, EShaderBindFlagBits::Pixel);

	RenderCommand::CopyTexture(SceneColor, FxaaTarget);
}

void FSceneRenderer::RenderDepthPass(const FSceneView& SceneView, const FRenderingInfo& ViewInfo)
{
	FRenderingInfo PassInfo = ViewInfo;

	for (FRenderingDesc& Color : PassInfo.ColorRenderTargets)
		Color.LoadOp = ERenderTargetLoadOp::DontCare;
	PassInfo.DepthStencil.Texture = nullptr;

	RenderCommand::BeginRenderPass(PassInfo);
	DepthViewRenderer->OnRender(ViewInfo.DepthStencil.Texture, SceneView.ViewProjection, SceneView.Location, SceneView.Forward);
	RenderCommand::EndRenderPass();
}

void FSceneRenderer::RenderDecalPass(const FSceneView& SceneView, const FSceneRenderSettings& Settings, const UWorld& ViewWorld)
{
	if(!Settings.bDrawDecals || !DecalRenderer || Settings.bDepthView || Settings.bVisualizeTileLightCount)
		return;

	const EViewMode ViewMode = SceneView.RenderMode.ViewMode;

	if (ViewMode != EViewMode::Lit && ViewMode != EViewMode::Unlit)
		return;

	const uint64 StartCycles = FPlatformTime::GetCycles64();
	const bool bGameView = ViewWorld.GetWorldType() != EWorldType::Editor;

	TArray<const FDecalSceneProxy*> SortedDecals;
	SortedDecals.Reserve(ViewWorld.GetScene().GetDecalProxies().Num());
	for (const FDecalSceneProxy* Decal : ViewWorld.GetScene().GetDecalProxies())
	{
		if (!Decal || (bGameView && Decal->IsHiddenInGame()))
			continue;

		SortedDecals.Add(Decal);
	}

	std::stable_sort(SortedDecals.begin(), SortedDecals.end(),
		[](const FDecalSceneProxy* A, const FDecalSceneProxy* B)
		{
			return A->GetSortOrder() < B->GetSortOrder();
		});

	for (const FDecalSceneProxy* Decal : SortedDecals)
	{
		FStatOverlay::AddDecalSubmission();
		Renderer->RenderDecal(*DecalRenderer, *Decal, SceneView.ViewProjection);
	}

	const uint64 ElapsedCycles = FPlatformTime::GetCycles64() - StartCycles;
	FStatOverlay::AddDecalCPUTime(FPlatformTime::ToMilliseconds64(ElapsedCycles));
}
