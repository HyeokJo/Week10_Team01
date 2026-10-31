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

void FDecalRenderer::OnRender(const FRenderPacket& Receiver, const FDecalSceneProxy& Decal, const FMatrix& ViewProjection)
{
	UTexture2D* DecalTexture = Decal.GetTexture();

	if(!bReady || !Receiver.Mesh || Decal.GetOpacity() <= 0.0f || !DecalTexture)
	{
		return;
	}

	FDecalConstants Constants{};
	
	Constants.World = Receiver.Proxy ? Receiver.Proxy->GetLocalToWorld() : (Receiver.Model ? *Receiver.Model : FMatrix::Identity);

	Constants.ViewProjection = ViewProjection;
	Constants.WorldToDecal = Decal.GetWorldToDecal();
	Constants.DecalSize = Decal.GetDecalSize();
	Constants.Opacity = Decal.GetOpacity();

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
