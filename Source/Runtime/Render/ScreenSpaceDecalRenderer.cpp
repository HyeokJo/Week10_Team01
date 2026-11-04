#include "EnginePCH.h"
#include "ScreenSpaceDecalRenderer.h"

#include "Engine/DecalSceneProxy.h"
#include "Core/StatOverlay.h"
#include "Render/DecalRenderer.h"
#include "Render/RenderingInfo.h"
#include "Render/ColorWorkflow.h"
#include "Render/RenderCommand.h"
#include "Render/RenderResourceManager.h"
#include "Render/Texture2D.h"

namespace
{
    constexpr uint32 InstanceResourceSlot = 14;

    struct FBatchConstants
    {
        uint32 InstanceOffset;
        uint32 Padding[3]{};
    };

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
    ColorOnlyShader = FRenderResourceManager::GetShaderProgram("Resources/Shader/DecalProjectionShader.hlsl_COLORONLY");
    if (!Shader || !ColorOnlyShader)
    {
        HTR_LOG(Error, "[Decal] Shader not found");
        return false;
    }

    ViewConstantBuffer = RenderCommand::CreateConstantBuffer(sizeof(FScreenSpaceDecalViewConstants));
    BatchConstantBuffer = RenderCommand::CreateConstantBuffer(sizeof(FBatchConstants));
    if (!ViewConstantBuffer || !ViewConstantBuffer->GetBuffer() || !BatchConstantBuffer || !BatchConstantBuffer->GetBuffer())
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

bool FScreenSpaceDecalRenderer::EnsureInstanceCapacity(uint32 Count)
{
    if (Count <= InstanceCapacity)
        return true;

    uint32 NewCapacity = std::max(InstanceCapacity * 2, 128u);
    while (NewCapacity < Count) NewCapacity *= 2;
    InstanceSRV.Reset();
    InstanceBuffer.Reset();
    InstanceCapacity = 0;

    D3D11_BUFFER_DESC Desc{};
    Desc.ByteWidth = NewCapacity * sizeof(FScreenSpaceDecalInstance);
    Desc.Usage = D3D11_USAGE_DYNAMIC;
    Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    Desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    Desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    Desc.StructureByteStride = sizeof(FScreenSpaceDecalInstance);
    ID3D11Device* Device = RenderCommand::GetDevice();
    if (FAILED(Device->CreateBuffer(&Desc, nullptr, InstanceBuffer.GetAddressOf())))
        return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC SrvDesc{};
    SrvDesc.Format = DXGI_FORMAT_UNKNOWN;
    SrvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    SrvDesc.Buffer.NumElements = NewCapacity;
    if (FAILED(Device->CreateShaderResourceView(InstanceBuffer.Get(), &SrvDesc, InstanceSRV.GetAddressOf())))
        return false;
    InstanceCapacity = NewCapacity;
    return true;
}

void FScreenSpaceDecalRenderer::OnRender(FTexture2D* DepthTexture, const FMatrix& ViewProjection,
    const TArray<const FDecalSceneProxy*>& Decals, const FViewportSettings& Viewport, bool bOrthographic, bool bWriteNormals, FTexture2D* ReceiverNormals)
{
    if (!Shader || !ViewConstantBuffer || !ViewConstantBuffer->GetBuffer() || !BatchConstantBuffer || !BatchConstantBuffer->GetBuffer() ||
        !DepthTexture || !DepthTexture->GetStencilSRV() || Viewport.Width == 0 || Viewport.Height == 0)
        return;

    Instances.Reset();
    Batches.Reset();
    const float InvWidth = 1.f / static_cast<float>(Viewport.Width);
    const float InvHeight = 1.f / static_cast<float>(Viewport.Height);
    for (const FDecalSceneProxy* Decal : Decals)
    {
        const float Fade = FDecalRenderer::CalculateScreenFade(*Decal, ViewProjection, bOrthographic);
        if (Fade <= 0.f) continue;
        FStatOverlay::AddDecalSubmission();
        UTexture2D* Texture = Decal->GetTexture();
        if (!Texture || !Texture->GetResource() || Decal->GetOpacity() <= 0.f) continue;
        UTexture2D* NormalTexture = bWriteNormals ? Decal->GetNormalTexture() : nullptr;
        D3D11_RECT Rect{};
        if (!CalculateDecalScissorRect(*Decal, ViewProjection, Viewport, Rect)) continue;

        FScreenSpaceDecalInstance Instance{};
        Instance.WorldToDecal = Decal->GetWorldToDecal();
        Instance.DecalSize = Decal->GetDecalSize();
        Instance.Opacity = Decal->GetOpacity() * Fade;
        Instance.BaseColor = ColorWorkflow::ToShaderColor(Decal->GetDecalColor());
        Instance.ScreenRect = FVector4((Rect.left - Viewport.StartX) * InvWidth, (Rect.top - Viewport.StartY) * InvHeight,
            (Rect.right - Viewport.StartX) * InvWidth, (Rect.bottom - Viewport.StartY) * InvHeight);
        Instance.ProjectionDirection = Decal->GetProjectionDirection();
        Instance.ProjectionEndFade = Decal->GetProjectionEndFade();
        Instance.AngleFadeCosines = Decal->GetAngleFadeCosines();
        Instance.bHasNormalMap = NormalTexture && NormalTexture->GetResource() ? 1u : 0u;
        Instance.bFlipNormalGreen = Decal->IsNormalGreenFlipped() ? 1u : 0u;

        // Only merge adjacent compatible draws: regrouping A/B/A by texture changes alpha compositing.
        if (Batches.IsEmpty() || Batches.Last().Texture != Texture || Batches.Last().NormalTexture != NormalTexture ||
            Batches.Last().Sampler != Decal->GetSamplerState())
            Batches.Add({Texture, NormalTexture, Decal->GetSamplerState(), static_cast<uint32>(Instances.Num()), 0});
        ++Batches.Last().InstanceCount;
        Instances.Add(Instance);
    }
    if (Instances.IsEmpty() || !EnsureInstanceCapacity(static_cast<uint32>(Instances.Num()))) return;

    ID3D11DeviceContext* Context = RenderCommand::GetContext();
    D3D11_MAPPED_SUBRESOURCE Mapped{};
    if (FAILED(Context->Map(InstanceBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped))) return;
    std::memcpy(Mapped.pData, Instances.GetData(), Instances.Num() * sizeof(FScreenSpaceDecalInstance));
    Context->Unmap(InstanceBuffer.Get(), 0);

    FScreenSpaceDecalViewConstants Constants{};
    Constants.InverseViewProjection = ViewProjection.Inverse();
    Constants.ViewportOrigin = FVector2(static_cast<float>(Viewport.StartX), static_cast<float>(Viewport.StartY));
    Constants.InverseViewportSize = FVector2(InvWidth, InvHeight);
    Constants.bHasReceiverNormals = ReceiverNormals ? 1u : 0u;

    PipelineState.Shader = bWriteNormals ? Shader : ColorOnlyShader;
    RenderCommand::BindPipelineState(PipelineState);
    RenderCommand::BindShaderResource(0, DepthTexture, EShaderBindFlagBits::Pixel);
    RenderCommand::BindStencilShaderResource(2, DepthTexture, EShaderBindFlagBits::Pixel);
    RenderCommand::BindShaderResource(3, ReceiverNormals, EShaderBindFlagBits::Pixel, true);
    RenderCommand::UpdateBufferData(ViewConstantBuffer.get(), &Constants);
    RenderCommand::BindConstantBuffer(0, ViewConstantBuffer.get(), EShaderBindFlagBits::Pixel);
    RenderCommand::BindConstantBuffer(1, BatchConstantBuffer.get(), EShaderBindFlagBits::Vertex);
    ID3D11ShaderResourceView* SRV = InstanceSRV.Get();
    Context->VSSetShaderResources(InstanceResourceSlot, 1, &SRV);
    Context->PSSetShaderResources(InstanceResourceSlot, 1, &SRV);

    for (const FBatch& Batch : Batches)
    {
        FBatchConstants BatchConstants{Batch.FirstInstance};
        RenderCommand::UpdateBufferData(BatchConstantBuffer.get(), &BatchConstants);
        RenderCommand::BindShaderResource(1, Batch.Texture, EShaderBindFlagBits::Pixel);
        RenderCommand::BindShaderResource(4, Batch.NormalTexture, EShaderBindFlagBits::Pixel, true);
        RenderCommand::BindSamplerState(0, Batch.Sampler, EShaderBindFlagBits::Pixel);
        RenderCommand::DrawInstanced(6, Batch.InstanceCount);
        FStatOverlay::AddDecalDrawCall();
    }
    RenderCommand::UnbindShaderResource(InstanceResourceSlot, EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel);
    RenderCommand::UnbindShaderResource(4, EShaderBindFlagBits::Pixel);
    // The receiver-normal target becomes the display output during gamma encoding.
    RenderCommand::UnbindShaderResource(3, EShaderBindFlagBits::Pixel);
}
