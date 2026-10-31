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
#include "Engine/Scene.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Text/TextRenderer.h"
#include "UObject/UObjectIterator.h"
#include "DecalRenderer.h"
#include "Component/DecalComponent.h"
#include "Core/StatOverlay.h"
#include "Core/Windows/WindowsPlatformTime.h"
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
}

void FSceneRenderer::Render(UWorld& World, const FSceneView& SceneView, const FSceneRenderSettings& Settings,
	const FRenderingInfo& SceneInfo, const FRenderingInfo& ViewInfo, FTexture2D* DecalBaseColorTarget, FTexture2D* FxaaTemp, FRenderQueue& RenderQueue)
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

	FDecalStats DecalStats{};
	DecalStats.NumTotalDecals = World.GetScene().GetDecalComponents().Num();
	FStatOverlay::SetDecalStats(DecalStats);

	// 1. 불투명·반투명 Pass (Linear 씬 컬러)
	RenderOpaquePass(SceneView, Settings, World, SceneInfo, RenderQueue, Lighting.Directional, DecalBaseColorTarget, FrustumPlanes);

	// 데이터 시각화 뷰는 씬 컬러 값 자체가 결과이므로 안개·인코딩·FXAA로 값을 바꾸지 않는다.
	const EViewMode ViewMode = SceneView.RenderMode.ViewMode;
	const bool bVisualizeData = IsDataVisualization(ViewMode);

	RenderTranslucentPass(SceneView, Settings, World, SceneInfo);

	if (Settings.bDepthView)
	{
		// 깊이 시각화는 표시용 타깃을 통째로 덮어쓰므로 인코딩이 필요 없다
		RenderDepthPass(SceneView, ViewInfo);
	}
	else
	{
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
									const FRenderingInfo& SceneInfo, FRenderQueue& RenderQueue, const FDirectionalLightInfo& DirectionalLight, 
									FTexture2D* DecalBaseColorTarget, const FFrustumPlanes& FrustumPlanes)
{
	const FViewRenderMode ViewRenderMode = SceneView.RenderMode;

	const bool bDrawPrimitives = Settings.bDrawPrimitives;
	const bool bVisualizeLightCount = Settings.bVisualizeTileLightCount;

	const TArray<UDecalComponent*>& Decals = ViewWorld.GetScene().GetDecalComponents();

	const bool bUseDecals = Settings.bDrawDecals && !Settings.bDepthView && !IsDataVisualization(ViewRenderMode.ViewMode) &&
		!Settings.bVisualizeCascades && !Settings.bVisualizeTileLightCount && bDrawPrimitives && !Decals.IsEmpty() && DecalBaseColorTarget != nullptr;

	const ERasterizerState SceneRasterizerState = ViewRenderMode.ViewMode == EViewMode::Wireframe ? ERasterizerState::Wireframe : ERasterizerState::SolidBack;

	Renderer->SetDecalBaseColorTexture(nullptr);

	if (ViewRenderMode.ViewMode == EViewMode::Lit)
	{
		Renderer->EnsureTileLightCapacity(SceneView.ViewProjection, SceneInfo.ViewportSetting.Width, SceneInfo.ViewportSetting.Height, bVisualizeLightCount);
	}

	// ==========================================
	// 공통 준비: RenderQueue 정렬
	// ==========================================

	if (bDrawPrimitives)
	{
		RenderCommand::SetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		Renderer->RenderQueueSorting(RenderQueue, SceneView.ViewProjection, SceneView.Location);
	}

	if (bUseDecals)
	{
		FRenderingInfo DepthPassInfo = SceneInfo;

		for (FRenderingDesc& Color : DepthPassInfo.ColorRenderTargets)
		{
			Color.LoadOp = ERenderTargetLoadOp::Load;
		}

		DepthPassInfo.DepthStencil.LoadOp = ERenderTargetLoadOp::Clear;

		DepthPassInfo.DepthStencil.ClearValue.DepthClearValue = 1.0f;
		DepthPassInfo.DepthStencil.ClearValue.StencilClearValue = 0;

		RenderCommand::UnbindShaderResource(0, EShaderBindFlagBits::Pixel);

		RenderCommand::BeginRenderPass(DepthPassInfo);

		RenderCommand::SetRasterizerState(SceneRasterizerState);
		RenderCommand::SetBlendState(EBlendState::NoColorWrite);
		RenderCommand::SetDepthStencilState(EDepthStencilState::Default);

		FViewRenderMode DepthMode = ViewRenderMode;
		DepthMode.ViewMode = EViewMode::Unlit;

		Renderer->SetViewMode(DepthMode);

		Renderer->SetOpaquePassMode(EOpaquePassMode::DepthPrepass);

		Renderer->RenderOpaque(SceneView.ViewProjection);

		RenderCommand::EndRenderPass();

		RenderDecalPass(SceneView, Settings, ViewWorld, SceneInfo, DecalBaseColorTarget, FrustumPlanes);

		Renderer->BindShadowForShading();

		Renderer->SetDecalBaseColorTexture(DecalBaseColorTarget);
	}

	FRenderingInfo PassInfo = SceneInfo;

	const float ClearValue = ColorWorkflow::bLinear ? FLinearColor::SRGBToLinear(0.3f) : 0.3f;

	PassInfo.ColorRenderTargets[0].ClearValue.ColorClearValue = FVector4(ClearValue, ClearValue, ClearValue, 1.0f);

	if (bUseDecals)
	{
		PassInfo.DepthStencil.LoadOp = ERenderTargetLoadOp::Load;
	}

	RenderCommand::BeginRenderPass(PassInfo);

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
		Renderer->SetOpaquePassMode(bUseDecals ? EOpaquePassMode::DepthReadOnly : EOpaquePassMode::Normal);
		Renderer->RenderOpaque(SceneView.ViewProjection);
		DrawWorldText(SceneView, ViewWorld);
		RenderCommand::SetRasterizerState(ERasterizerState::SolidBack);
	}

	RenderCommand::EndRenderPass();
	Renderer->SetDecalBaseColorTexture(nullptr);
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

void FSceneRenderer::RenderDecalPass(const FSceneView& SceneView, const FSceneRenderSettings& Settings, const UWorld& ViewWorld, const FRenderingInfo& SceneInfo, FTexture2D* DecalBaseColorTarget, const FFrustumPlanes& FrustumPlanes)
{
	const uint64 StartCycles = FPlatformTime::GetCycles64();

	FDecalStats DecalStats{};

	const auto UpdatePassTime = [&](){
		const uint64 ElapsedCycles = FPlatformTime::GetCycles64() - StartCycles;
		DecalStats.CpuPassTimeMs = FPlatformTime::ToMilliseconds64(ElapsedCycles);
		};

	const TArray<UDecalComponent*>& Decals = ViewWorld.GetScene().GetDecalComponents();
	DecalStats.NumTotalDecals = Decals.Num();
	const bool bIsGameWorld = ViewWorld.GetWorldType() != EWorldType::Editor;
	int32 NumVisibleDecals = 0;

	for (UDecalComponent* Decal : Decals)
	{
		if (!Decal)
		{
			continue;
		}

		if (bIsGameWorld && Decal->IsHiddenInGame())
		{
			++DecalStats.NumHiddenDecals;
			continue;
		}

		++NumVisibleDecals;
	}

	if (!Settings.bDrawPrimitives || Decals.IsEmpty())
	{
		UpdatePassTime();
		FStatOverlay::SetDecalStats(DecalStats);
		return;
	}

	FFrustumCorners FrustumCorners{};
	const bool bHasFrustumCorners = ExtractFrustumCorners(SceneView.ViewProjection, FrustumCorners);

	FRenderingInfo PassInfo{};
	PassInfo.ViewportSetting = SceneInfo.ViewportSetting;
	FRenderingDesc DecalColorDesc{};
	DecalColorDesc.Texture = DecalBaseColorTarget;
	DecalColorDesc.LoadOp = ERenderTargetLoadOp::Clear;
	DecalColorDesc.ClearValue.ColorClearValue = FVector4(0.0f, 0.0f, 0.0f, 0.0f);
	PassInfo.ColorRenderTargets.Add(DecalColorDesc);

	PassInfo.DepthStencil.Texture = nullptr;

	RenderCommand::BeginRenderPass(PassInfo);

	const auto ClassifyBounds = [&FrustumPlanes](const FBox& Bounds, uint32& PlaneMask)
		{
			switch (ClassifyBoxInFrustum(
				Bounds, FrustumPlanes, PlaneMask))
			{
			case EFrustumTest::Outside:
				return EBVHCullResult::Outside;

			case EFrustumTest::Intersect:
				return EBVHCullResult::Intersect;

			case EFrustumTest::Inside:
				return EBVHCullResult::Inside;
			}

			return EBVHCullResult::Outside;
		};

	ViewWorld.GetScene().GetDecalBVH().QueryCull(FrustumAllPlanesMask, ClassifyBounds, [&](UDecalComponent* Decal)
		{
			if (!Decal)
			{
				return;
			}

			if (bIsGameWorld && Decal->IsHiddenInGame())
			{
				return;
			}

			// BVH AABB 판정을 통과한 후보에 대해 정밀 OBB 판정을 수행한다.
			if (bHasFrustumCorners)
			{
				FOrientedBox DecalOBB{};
				const bool bValidOBB = TryMakeWorldOBB(Decal->CalcLocalBounds(), Decal->GetWorldMatrix(), DecalOBB);
				if (bValidOBB && !IsOBBInFrustumSAT(DecalOBB, FrustumPlanes, FrustumCorners))
				{
					++DecalStats.NumOBBCulledDecals;
					return;
				}
			}

			++DecalStats.NumInFrustumDecals;
			if (DecalRenderer->OnRender(SceneInfo.DepthStencil.Texture, SceneView.ViewProjection, *Decal))
			{
				++DecalStats.NumDrawCalls;
			}
		});

	DecalStats.NumFrustumCulledDecals = std::max(0, NumVisibleDecals - DecalStats.NumOBBCulledDecals - DecalStats.NumInFrustumDecals);

	RenderCommand::EndRenderPass();

	RenderCommand::UnbindShaderResource(0, EShaderBindFlagBits::Pixel);
	RenderCommand::UnbindShaderResource(1, EShaderBindFlagBits::Pixel);
	RenderCommand::UnbindShaderResource(2, EShaderBindFlagBits::Pixel);

	UpdatePassTime();
	FStatOverlay::SetDecalStats(DecalStats);
}

void FSceneRenderer::RenderTranslucentPass(const FSceneView& SceneView, const FSceneRenderSettings& Settings, const UWorld& ViewWorld, const FRenderingInfo& SceneInfo)
{
	if (!Settings.bDrawPrimitives)
	{
		return;
	}

	FRenderingInfo PassInfo = SceneInfo;

	for (FRenderingDesc& Color : PassInfo.ColorRenderTargets)
	{
		Color.LoadOp = ERenderTargetLoadOp::Load;
	}

	PassInfo.DepthStencil.LoadOp = ERenderTargetLoadOp::Load;

	RenderCommand::BeginRenderPass(PassInfo);

	RenderCommand::SetRasterizerState(ERasterizerState::SolidBack);
	Renderer->RenderTranslucent(SceneView.ViewProjection);
	RenderCommand::EndRenderPass();
}
