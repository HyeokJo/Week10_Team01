#include "EnginePCH.h"
#include "DecalRenderer.h"

#include "Core/StatOverlay.h"
#include "Core/EngineTimer.h"

#include "Engine/DecalSceneProxy.h"
#include "Engine/PrimitiveSceneProxy.h"
#include "Render/RenderCommand.h"
#include "Render/RenderPacket.h"
#include "Render/RenderResourceManager.h"
#include "Render/Texture2D.h"
#include "Render/ColorWorkflow.h"
#include "Render/Renderer.h"
#include "Render/SceneRenderer.h"

bool FDecalRenderer::Init()
{
	bReady = false;

	FShaderProgram* Shader = FRenderResourceManager::GetShaderProgram("Resources/Shader/DecalShader.hlsl");

	if (!Shader) 
	{
		return false;
	}

	const FConstantBufferBinding* Binding = Shader->VertexShader->GetBindings().FindConstantBuffer("DecalConstants");

	if(!Binding || Binding->Slot != 0 || Binding->Size != sizeof(FDecalConstants))
	{
		return false;
	}

	ConstantBuffer = RenderCommand::CreateConstantBuffer(sizeof(FDecalConstants));

	if (!ConstantBuffer || !ConstantBuffer->GetBuffer())
	{
		return false;
	}

	PipelineState.Shader = Shader;
	PipelineState.Topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	PipelineState.RasterizerState = ERasterizerState::SolidBack;
	PipelineState.BlendState = EBlendState::AlphaBlend;
	PipelineState.DepthStencilState = EDepthStencilState::ReadOnlyLessEqual;

	bReady = true;
	return true;
}

float FDecalRenderer::CalculateScreenFade(const FDecalSceneProxy& Decal, const FMatrix& ViewProjection, bool bOrthographic)
{
	const float FadeScreenSize = Decal.GetFadeScreenSize();
	if (FadeScreenSize <= 0.f)
		return 1.f;

	const FBox& Bounds = Decal.GetBounds();
	const FVector Center = (Bounds.Min + Bounds.Max) * 0.5f;
	const float Radius = ((Bounds.Max - Bounds.Min) * 0.5f).Size();
	// The vertical projection scale includes FOV or orthographic zoom, independent of camera rotation.
	const float ProjectionScaleY = FVector(ViewProjection.M[0][1], ViewProjection.M[1][1], ViewProjection.M[2][1]).Size();
	float ScreenSize = Radius * ProjectionScaleY;
	if (!bOrthographic)
	{
		const float ViewDepth = (FVector4(Center.X, Center.Y, Center.Z, 1.f) * ViewProjection).W;
		if (ViewDepth <= -Radius)
			return 0.f;
		// A bound crossing the camera plane must not fade out or divide by zero.
		if (ViewDepth <= Radius)
			return 1.f;
		ScreenSize /= ViewDepth;
	}

	// Fully visible at the threshold, fully faded below half the threshold.
	const float Alpha = FMath::Clamp(ScreenSize / FadeScreenSize * 2.f - 1.f, 0.f, 1.f);
	return Alpha * Alpha * (3.f - 2.f * Alpha);
}

void FDecalRenderer::OnRender(const FRenderPacket& Receiver, const FDecalSceneProxy& Decal, const FSceneView& SceneView, float ScreenFade)
{
	UTexture2D* DecalTexture = Decal.GetTexture();

	if(!bReady || !Receiver.Mesh || Decal.GetOpacity() <= 0.0f || !DecalTexture || ScreenFade <= 0.f)
	{
		return;
	}

	FDecalConstants Constants{};
	
	Constants.World = Receiver.Proxy ? Receiver.Proxy->GetLocalToWorld() : (Receiver.Model ? *Receiver.Model : FMatrix::Identity);

	Constants.ViewProjection = SceneView.ViewProjection;
	Constants.WorldToDecal = Decal.GetWorldToDecal();
	Constants.NormalMatrix = Receiver.Proxy ? Receiver.Proxy->GetNormalMatrix() : MakeNormalMatrix(Constants.World);
	Constants.DecalSize = Decal.GetDecalSize();
	Constants.Opacity = Decal.GetOpacity() * ScreenFade;
	Constants.DecalColor = ColorWorkflow::ToShaderColor(Decal.GetDecalColor());
	Constants.CameraPosition = SceneView.Location;
	Constants.Shininess = Receiver.Material->Shininess;
	const FLinearColor Specular = ColorWorkflow::ToShaderColor(Receiver.Material->SpecularColor);
	Constants.SpecularColor = FVector(Specular.R, Specular.G, Specular.B);
	Constants.bLit = SceneView.RenderMode.ViewMode == EViewMode::Lit ? 1u : 0u;
	Constants.LightingModel = static_cast<uint32>(SceneView.RenderMode.LightingModel);
	Constants.bReceiveShadow = !Receiver.Proxy || Receiver.Proxy->IsReceiveShadow() ? 1u : 0u;
	UTexture2D* ReceiverNormal = Receiver.Material->Textures.Num() > 1 ? Receiver.Material->Textures[1] : nullptr;
	Constants.bHasReceiverNormalMap = ReceiverNormal && SceneView.RenderMode.ShadingModel == EShadingModel::Phong ? 1u : 0u;
	Constants.bHasDecalNormalMap = Decal.GetNormalTexture() ? 1u : 0u;
	Constants.ReceiverUVOffset = Receiver.Material->UVScrollSpeed * EngineTimer::GetTotalTime();
	Constants.bFlipReceiverNormalGreen = Receiver.Material->bFlipNormalGreen ? 1u : 0u;
	Constants.bFlipDecalNormalGreen = Decal.IsNormalGreenFlipped() ? 1u : 0u;
	Constants.ProjectionDirection = Decal.GetProjectionDirection();
	Constants.AngleFadeCosEnd = Decal.GetAngleFadeCosines().X;
	Constants.AngleFadeCosStart = Decal.GetAngleFadeCosines().Y;

	RenderCommand::BindMesh(Receiver.Mesh, Receiver.LODIndex);
	RenderCommand::BindPipelineState(PipelineState);

	RenderCommand::BindShaderResource(0, DecalTexture, EShaderBindFlagBits::Pixel);
	RenderCommand::BindSamplerState(0, Decal.GetSamplerState(), EShaderBindFlagBits::Pixel);
	RenderCommand::BindShaderResource(1, ReceiverNormal, EShaderBindFlagBits::Pixel, true);
	RenderCommand::BindShaderResource(4, Decal.GetNormalTexture(), EShaderBindFlagBits::Pixel, true);
	// s1/s2 belong to shadows; the receiver keeps its own UV addressing mode.
	RenderCommand::BindSamplerState(3, Receiver.Material->SamplerState, EShaderBindFlagBits::Pixel);

	RenderCommand::UpdateBufferData(ConstantBuffer.get(), &Constants, sizeof(Constants));

	RenderCommand::BindConstantBuffer(0, ConstantBuffer.get(), EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel);

	const uint32 IndexCount = Receiver.IndexCount != 0 ? Receiver.IndexCount : Receiver.Mesh->GetIndexBuffer(Receiver.LODIndex)->GetIndexCount();
	
	RenderCommand::DrawIndexed(IndexCount, Receiver.StartIndex);
	FStatOverlay::AddDecalDrawCall();

	RenderCommand::UnbindShaderResource(0, EShaderBindFlagBits::Pixel);
	RenderCommand::UnbindShaderResource(1, EShaderBindFlagBits::Pixel);
	RenderCommand::UnbindShaderResource(4, EShaderBindFlagBits::Pixel);
}
