#pragma once

#include "Editor/EditorUI/EditorPanel.h"

#include <filesystem>
#include <functional>

namespace fs = std::filesystem;

class UTexture2D;

class FContentDrawerPanel : public IEditorPanel
{
public:
	bool Init() override;
	void Tick(float DeltaTime) override;
	void OnRender() override;
	const char* GetPanelName() const override { return "Content Drawer"; }
	void RenderFolderTree(const fs::path& Path, fs::path& SelectedPath);
	void SetOpenLevelSequenceBinding(std::function<void(const FString&)> InOpenLevelSequence)
	{
		OpenLevelSequence = std::move(InOpenLevelSequence);
	}
	// 메시 파일을 더블클릭했을 때. 무엇을 할지는 받는 쪽이 에셋 종류를 보고 정한다
	void SetOpenMeshBinding(std::function<void(const FString&)> InOpenMesh)
	{
		OpenMesh = std::move(InOpenMesh);
	}
	// Import 버튼. 인자는 지금 보고 있는 폴더이고 임포트 결과가 여기에 저장된다
	void SetImportMeshBinding(std::function<void(const FString&)> InImportMesh)
	{
		ImportMesh = std::move(InImportMesh);
	}
	// 우클릭 메뉴의 리임포트. 인자는 .hasset 경로와 원본 경로이고, 원본이 비어 있으면 받는 쪽이 파일을 고르게 한다
	void SetReimportMeshBinding(std::function<void(const FString&, const FString&)> InReimportMesh)
	{
		ReimportMesh = std::move(InReimportMesh);
	}

private:
	// 현재 폴더를 Assets > A > B 형태로 그린다. 조각을 누르면 그 폴더로 이동한다.
	void DrawPathBar();
	void CreateLevelSequenceFile();
	void DrawMeshAssetMenu(const FString& AssetPath);

	fs::path AssetRootPath = "Assets";
	fs::path CurrentPath = "Assets";
	FString CreateAssetError;
	std::function<void(const FString&)> OpenLevelSequence;
	std::function<void(const FString&)> OpenMesh;
	std::function<void(const FString&)> ImportMesh;
	std::function<void(const FString&, const FString&)> ReimportMesh;

	// 메뉴가 열릴 때 한 번 계산한다. 원본 전체를 읽어 해시를 내므로 매 프레임 하지 않는다
	FString MeshMenuSource;
	FString MeshMenuStatus;

	UTexture2D* FolderIcon = nullptr;
	UTexture2D* FileIcon = nullptr;

};
