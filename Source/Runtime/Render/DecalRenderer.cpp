#include "EnginePCH.h"
#include "DecalRenderer.h"

#include "Core/StatOverlay.h"

#include "Engine/DecalSceneProxy.h"
#include "Engine/PrimitiveSceneProxy.h"
#include "Render/RenderCommand.h"
#include "Render/RenderPacket.h"
#include "Render/RenderResourceManager.h"
#include "Render/Texture2D.h"

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

void FDecalRenderer::OnRender(const FRenderPacket& Receiver, const FDecalSceneProxy& Decal, const FMatrix& ViewProjection, float ScreenFade)
{
	UTexture2D* DecalTexture = Decal.GetTexture();

	if(!bReady || !Receiver.Mesh || Decal.GetOpacity() <= 0.0f || !DecalTexture || ScreenFade <= 0.f)
	{
		return;
	}

	FDecalConstants Constants{};
	
	Constants.World = Receiver.Proxy ? Receiver.Proxy->GetLocalToWorld() : (Receiver.Model ? *Receiver.Model : FMatrix::Identity);

	Constants.ViewProjection = ViewProjection;
	Constants.WorldToDecal = Decal.GetWorldToDecal();
	Constants.DecalSize = Decal.GetDecalSize();
	Constants.Opacity = Decal.GetOpacity() * ScreenFade;
	Constants.DecalColor = Decal.GetDecalColor();

	RenderCommand::BindMesh(Receiver.Mesh, Receiver.LODIndex);
	RenderCommand::BindPipelineState(PipelineState);

	RenderCommand::BindShaderResource(0, DecalTexture, EShaderBindFlagBits::Pixel);
	RenderCommand::BindSamplerState(0, ESamplerState::LinearClamp, EShaderBindFlagBits::Pixel);

	RenderCommand::UpdateBufferData(ConstantBuffer.get(), &Constants, sizeof(Constants));

	RenderCommand::BindConstantBuffer(0, ConstantBuffer.get(), EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel);

	const uint32 IndexCount = Receiver.IndexCount != 0 ? Receiver.IndexCount : Receiver.Mesh->GetIndexBuffer(Receiver.LODIndex)->GetIndexCount();
	
	RenderCommand::DrawIndexed(IndexCount, Receiver.StartIndex);
	FStatOverlay::AddDecalDrawCall();

	RenderCommand::UnbindShaderResource(0, EShaderBindFlagBits::Pixel);
}
