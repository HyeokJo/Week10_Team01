#include "EnginePCH.h"
#include "ScreenSpaceDecalRenderer.h"

#include "Engine/DecalSceneProxy.h"
#include "Render/RenderingInfo.h"
#include "Render/ColorWorkflow.h"
#include "Render/RenderCommand.h"
#include "Render/RenderResourceManager.h"
#include "Render/Texture2D.h"

namespace
{
    bool CalculateDecalScissorRect(const FDecalSceneProxy& Decal, const FMatrix& ViewProjection, const FViewportSettings& Viewport, D3D11_RECT& OutRect)
    {
        if (Viewport.Width == 0 || Viewport.Height == 0)
        {
            return false;
        }
        const LONG ViewLeft = static_cast<LONG>(Viewport.StartX);
        const LONG ViewTop = static_cast<LONG>(Viewport.StartY);
        const LONG ViewRight = ViewLeft + static_cast<LONG>(Viewport.Width);
        const LONG ViewBottom = ViewTop + static_cast<LONG>(Viewport.Height);

        const FVector Size = Decal.GetDecalSize();
        const FMatrix DecalToWorld = Decal.GetDecalToWorld();

        float MinX = std::numeric_limits<float>::max();
        float MinY = std::numeric_limits<float>::max();
        float MaxX = std::numeric_limits<float>::lowest();
        float MaxY = std::numeric_limits<float>::lowest();

        bool bUseFullViewport = false;

        for (int32 Index = 0; Index < 8; ++Index)
        {
            const FVector LocalCorner((Index & 1) ? Size.X : -Size.X, (Index & 2) ? Size.Y : -Size.Y, (Index & 4) ? Size.Z : -Size.Z);
            const FVector4 WorldPosition = FVector4(LocalCorner, 1.0f) * DecalToWorld;
            const FVector4 ClipPosition = WorldPosition * ViewProjection;
            if (!std::isfinite(ClipPosition.X) || !std::isfinite(ClipPosition.Y) || !std::isfinite(ClipPosition.Z) || !std::isfinite(ClipPosition.W) || ClipPosition.W <= 1.0e-4f)
            {
                bUseFullViewport = true;
                break;
            }
            const float NdcX = ClipPosition.X / ClipPosition.W;
            const float NdcY = ClipPosition.Y / ClipPosition.W;
            const float ScreenX = Viewport.StartX + (NdcX * 0.5f + 0.5f) * Viewport.Width;
            const float ScreenY =  Viewport.StartY + (0.5f - NdcY * 0.5f) * Viewport.Height;
            if (!std::isfinite(ScreenX) || !std::isfinite(ScreenY))
            {
                bUseFullViewport = true;
                break;
            }

            MinX = std::min(MinX, ScreenX);
            MinY = std::min(MinY, ScreenY);
            MaxX = std::max(MaxX, ScreenX);
            MaxY = std::max(MaxY, ScreenY);
        }
        if (bUseFullViewport)
        {
            OutRect.left = ViewLeft;
            OutRect.top = ViewTop;
            OutRect.right = ViewRight;
            OutRect.bottom = ViewBottom;
            return true;
        }
        const float ClampedMinX = std::max(MinX, static_cast<float>(ViewLeft));
        const float ClampedMinY = std::max(MinY, static_cast<float>(ViewTop));
        const float ClampedMaxX = std::min(MaxX, static_cast<float>(ViewRight));
        const float ClampedMaxY = std::min(MaxY, static_cast<float>(ViewBottom));
        if (ClampedMinX >= ClampedMaxX || ClampedMinY >= ClampedMaxY)
        {
            return false;
        }
        const LONG Left = std::max<LONG>(ViewLeft, static_cast<LONG>(std::floor(ClampedMinX)) - 1);
        const LONG Top = std::max<LONG>(ViewTop, static_cast<LONG>(std::floor(ClampedMinY)) - 1);
        const LONG Right = std::min<LONG>(ViewRight, static_cast<LONG>(std::ceil(ClampedMaxX)) + 1);
        const LONG Bottom = std::min<LONG>(ViewBottom, static_cast<LONG>(std::ceil(ClampedMaxY)) + 1);
        if (Left >= Right || Top >= Bottom)
        {
            return false;
        }

        OutRect.left = Left;
        OutRect.top = Top;
        OutRect.right = Right;
        OutRect.bottom = Bottom;

        return true;
    }
}

bool FScreenSpaceDecalRenderer::Init()
{
    Shader = FRenderResourceManager::GetShaderProgram("Resources/Shader/DecalProjectionShader.hlsl");
    if (!Shader)
    {
        HTR_LOG(Error, "[Decal] Shader not found");
        return false;
    }

    ConstantBuffer = RenderCommand::CreateConstantBuffer(sizeof(FScreenSpaceDecalConstants));
    if (!ConstantBuffer)
    {
        HTR_LOG(Error, "[Decal] Constant buffer creation failed");
        return false;
    }

    PipelineState.Shader = Shader;
    PipelineState.Topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    PipelineState.BlendState = EBlendState::AlphaBlend;
    PipelineState.DepthStencilState = EDepthStencilState::Disabled;
    PipelineState.RasterizerState = ERasterizerState::SolidNoneScissor;

    return true;
}

bool FScreenSpaceDecalRenderer::OnRender(FTexture2D* DepthTexture, const FMatrix& ViewProjection, const FDecalSceneProxy& Decal, const FViewportSettings& Viewport, float ScreenFade)
{
    UTexture2D* DecalTexture = Decal.GetTexture();
    if (!Shader || !ConstantBuffer || !ConstantBuffer->GetBuffer() || !DepthTexture || !DepthTexture->GetStencilSRV() ||
        !DecalTexture || Decal.GetOpacity() <= 0.f || ScreenFade <= 0.f)
        return false;

    D3D11_RECT ScissorRect{};

    if (!CalculateDecalScissorRect(Decal, ViewProjection, Viewport, ScissorRect))
    {
        return false;
    }

    FScreenSpaceDecalConstants Constants{};
    Constants.InverseViewProjection = ViewProjection.Inverse();
    Constants.WorldToDecal = Decal.GetWorldToDecal();
    Constants.DecalSize = Decal.GetDecalSize();
    Constants.Opacity = Decal.GetOpacity() * ScreenFade;
    Constants.BaseColor = ColorWorkflow::ToShaderColor(Decal.GetDecalColor());
    Constants.ProjectionEndFade = Decal.GetProjectionEndFade();

    RenderCommand::BindPipelineState(PipelineState);
    RenderCommand::SetScissorRect(ScissorRect.left, ScissorRect.top, ScissorRect.right, ScissorRect.bottom);
    RenderCommand::BindShaderResource(0, DepthTexture, EShaderBindFlagBits::Pixel);
    RenderCommand::BindShaderResource(1, DecalTexture, EShaderBindFlagBits::Pixel);
    RenderCommand::BindStencilShaderResource(2, DepthTexture, EShaderBindFlagBits::Pixel);
    RenderCommand::BindSamplerState(0, Decal.GetSamplerState(), EShaderBindFlagBits::Pixel);
    RenderCommand::UpdateBufferData(ConstantBuffer.get(), &Constants);
    RenderCommand::BindConstantBuffer(0, ConstantBuffer.get(), EShaderBindFlagBits::Pixel);
    RenderCommand::Draw(3);

    return true;
}
