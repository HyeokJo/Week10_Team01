#include "EnginePCH.h"
#include "DecalRenderer.h"

#include "Component/DecalComponent.h"
#include "Asset/AssetManager.h"
#include "Render/RenderCommand.h"
#include "Render/RenderResourceManager.h"
#include "Render/Texture2D.h"
bool FDecalRenderer::Init()
{
    Shader = FRenderResourceManager::GetShaderProgram("Resources/Shader/DecalProjectionShader.hlsl");
    if (!Shader)
    {
        HTR_LOG(Error, "[Decal] Shader not found");
        return false;
    }

    ConstantBuffer = RenderCommand::CreateConstantBuffer(sizeof(FDecalConstants));
    if (!ConstantBuffer)
    {
        HTR_LOG(Error, "[Decal] Constant buffer creation failed");
        return false;
    }

    PipelineState.Shader = Shader;
    PipelineState.Topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    PipelineState.BlendState = EBlendState::AlphaBlend;
    PipelineState.DepthStencilState = EDepthStencilState::Disabled;
    PipelineState.RasterizerState = ERasterizerState::SolidNone;

    return true;
}

bool FDecalRenderer::OnRender(FTexture2D* DepthTexture, const FMatrix& ViewProjection, const UDecalComponent& Decal)
{
    UMaterial* Material = Decal.GetDecalMaterial();
    if (!Material)
    {
        Material = UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial");
    }
    if (!DepthTexture || !DepthTexture->GetStencilSRV() || !Material || Material->Textures.Num() <= 0 || !Material->Textures[0])
    {
        return false;
    }
    UTexture2D* DecalTexture = Material->Textures[0];

    FDecalConstants Constants{};
    Constants.InverseViewProjection = ViewProjection.Inverse();
    Constants.WorldToDecal = Decal.GetWorldMatrix().Inverse();
    Constants.DecalSize = Decal.GetDecalSize();
    Constants.Opacity = Decal.GetOpacity();
    Constants.BaseColor = Material->BaseColor;
    Constants.ProjectionEndFade = Decal.GetProjectionEndFade();

    RenderCommand::BindPipelineState(PipelineState);
    RenderCommand::BindShaderResource(0, DepthTexture, EShaderBindFlagBits::Pixel);
    RenderCommand::BindShaderResource(1, DecalTexture, EShaderBindFlagBits::Pixel);
    RenderCommand::BindStencilShaderResource(2, DepthTexture, EShaderBindFlagBits::Pixel);
    RenderCommand::BindSamplerState(0, Material->SamplerState, EShaderBindFlagBits::Pixel);
    RenderCommand::UpdateBufferData(ConstantBuffer.get(), &Constants);
    RenderCommand::BindConstantBuffer(0, ConstantBuffer.get(), EShaderBindFlagBits::Pixel);
    RenderCommand::Draw(3);

    return true;
}
