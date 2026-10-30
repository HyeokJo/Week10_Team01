#include "EnginePCH.h"
#include "SceneRenderTargets.h"

#include "RenderCommand.h"

void FSceneRenderTargets::Resize(uint32 InWidth, uint32 InHeight)
{
	if (InWidth == Width && InHeight == Height)
	{
		return;
	}

	// 패널 View 슬롯과 같은 구성이다. Fog가 깊이를 읽으므로 깊이는 TYPELESS로 만든다.
	D3D11_TEXTURE2D_DESC Desc{
		.Width = InWidth,
		.Height = InHeight,
		.MipLevels = 1,
		.ArraySize = 1,
		.Format = DXGI_FORMAT_R8G8B8A8_UNORM,
		.SampleDesc = {
			.Count = 1,
		},
		.Usage = D3D11_USAGE_DEFAULT,
		.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
	};

	ViewColor = RenderCommand::CreateTexture2D(Desc);
	FxaaTemp = RenderCommand::CreateTexture2D(Desc);

	Desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	SceneColor = RenderCommand::CreateTexture2D(Desc);

	Desc.Format = DXGI_FORMAT_R24G8_TYPELESS;
	Desc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
	Depth = RenderCommand::CreateTexture2D(Desc);

	SceneInfo.ColorRenderTargets.Reset();
	SceneInfo.ViewportSetting.Width = InWidth;
	SceneInfo.ViewportSetting.Height = InHeight;
	// 지우기 색은 작업 흐름에 따라 달라지므로 RenderOpaquePass에서 정한다.
	SceneInfo.ColorRenderTargets.Add({
		.Texture = SceneColor.get(),
	});
	SceneInfo.DepthStencil.Texture = Depth.get();

	ViewInfo.ColorRenderTargets.Reset();
	ViewInfo.ViewportSetting.Width = InWidth;
	ViewInfo.ViewportSetting.Height = InHeight;
	ViewInfo.ColorRenderTargets.Add({
		.Texture = ViewColor.get(),
	});
	ViewInfo.DepthStencil.Texture = Depth.get();

	Width = InWidth;
	Height = InHeight;
}

void FSceneRenderTargets::Release()
{
	SceneInfo = {};
	ViewInfo = {};
	SceneColor.reset();
	ViewColor.reset();
	Depth.reset();
	FxaaTemp.reset();
	Width = 0;
	Height = 0;
}
