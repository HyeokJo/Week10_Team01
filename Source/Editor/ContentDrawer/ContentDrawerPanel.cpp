#include "EnginePCH.h"
#include "Editor/ContentDrawer/ContentDrawerPanel.h"
#include "Asset/AssetManager.h"
#include "Text/Font.h"
#include "Editor/HitoriEd/EditorDragDrop.h"
#include "Editor/HitoriEd/EditorResourcePaths.h"
#include "Editor/Script/LuaScriptEditor.h"
#include "Sequence/LevelSequence.h"
#include "Sequence/MovieScene.h"
#include "Serialization/JsonArchive.h"
#include "Asset/MeshAssetFile.h"

namespace
{
	bool HasExtension(const fs::path& Path, const char* Extension)
	{
		return _stricmp(Path.extension().string().c_str(), Extension) == 0;
	}

	// 에셋이 있으면 원본은 보이지 않고 에셋 하나로 표시된다. .obj.bin은 자동 생성 캐시라 보이지 않는다
	bool IsHiddenInDrawer(const fs::path& Path)
	{
		if (HasExtension(Path, ".bin") && HasExtension(Path.stem(), ".obj"))
			return true;
		if (HasExtension(Path, ".tmp"))
			return true;
		if (HasExtension(Path, ".fbx") || HasExtension(Path, ".obj"))
			return FMeshAssetFile::Exists(FMeshAssetFile::MakeAssetPath(Path.generic_string()));
		return false;
	}
}

bool FContentDrawerPanel::Init()
{
	FolderIcon = UAssetManager::Get().LoadTexture(EditorResourcePaths::FolderIcon);
	FileIcon = UAssetManager::Get().LoadTexture(EditorResourcePaths::FileIcon);
	return false;
}

void FContentDrawerPanel::Tick(float DeltaTime)
{
}

void FContentDrawerPanel::OnRender()
{
	ImGui::Begin("Content Drawer");
	std::error_code Error;

	if (!fs::is_directory(CurrentPath, Error))
	{
		CurrentPath = AssetRootPath;
	}

	Error.clear();

	if (!fs::is_directory(AssetRootPath, Error))
	{
		ImGui::TextUnformatted("Asset folder is unavailable.");
		ImGui::End();
		return;
	}

	float treeViewWidth = 200.0f;
	ImGui::BeginChild("FolderTreeView", ImVec2(treeViewWidth, 0));
	{
		// 루트 노드
		if (ImGui::TreeNodeEx(AssetRootPath.string().c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth |
			(AssetRootPath == CurrentPath ? ImGuiTreeNodeFlags_Selected : 0) | ImGuiTreeNodeFlags_DrawLinesToNodes))
		{
			if (ImGui::IsItemClicked())
			{
				CurrentPath = AssetRootPath;
			}
			RenderFolderTree(AssetRootPath, CurrentPath);
			ImGui::TreePop();
		}
	}
	ImGui::EndChild();

	ImGui::SameLine();

	ImGui::BeginChild("AssetGridView");
	{
		if (ImGui::Button("Import") && ImportMesh)
		{
			ImportMesh(CurrentPath.generic_string());
		}
		ImGui::SetItemTooltip("Import a .fbx or .obj file into this folder.");
		ImGui::SameLine();
		DrawPathBar();
		ImGui::Separator();
		if (!CreateAssetError.empty())
		{
			ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.35f, 1.0f), "%s", CreateAssetError.c_str());
		}

		// 그리드 레이아웃 계산
		float padding = 16.0f;
		float thumbnailSize = 80.0f;
		float cellSize = thumbnailSize + padding;
		float panelWidth = ImGui::GetContentRegionAvail().x;
		int ColumnCount = static_cast<int>(panelWidth / cellSize);
		if (ColumnCount < 1) ColumnCount = 1;

		// 테이블 기반 그리드 시작
		if (ImGui::BeginTable("AssetGridTable", ColumnCount))
		{
			std::error_code IterateError;
			fs::directory_iterator It(CurrentPath, IterateError);
			const fs::directory_iterator End;

			for (; !IterateError && It != End; It.increment(IterateError))
			{
				const fs::path Path = It->path();

				std::error_code TypeError;
				const bool bIsDirectory = fs::is_directory(Path, TypeError);

				if (TypeError)
					continue;

				if (!bIsDirectory && IsHiddenInDrawer(Path))
					continue;

				ImGui::TableNextColumn();

				std::string PathString = Path.generic_string();
				std::string FilenameString = Path.filename().string();

				// humanoid.fbx.hasset은 humanoid.fbx로 보인다. 숨겨지지 않은 FBX는 아직 임포트되지 않은 것이다
				const bool bMeshAsset = !bIsDirectory && HasExtension(Path, FMeshAssetFile::Extension);
				const bool bUnimportedFbx = !bIsDirectory && HasExtension(Path, ".fbx");
				if (bMeshAsset)
				{
					FilenameString = Path.stem().string();
				}

				ImGui::PushID(PathString.c_str()); // 각 위젯에 고유 ID 부여
				UTexture2D* Thumbnail = nullptr;

				// 실제 에셋일 때만 채워진다.
				// 폴더/파일 아이콘은 끌 수 없어야 하므로 썸네일과 구분해서 들고 있는다.
				UTexture2D* TextureAsset = nullptr;
				UFont* FontAsset = nullptr;

				// 폴더 또는 파일 아이콘 표시
				if (bIsDirectory)
				{
					Thumbnail = FolderIcon;
				}
				else // 파일인 경우
				{
					UAsset* Asset = UAssetManager::GetAssetByPath<UAsset>(PathString);
					if (Asset && Asset->IsA<UTexture2D>())
					{
						TextureAsset = Cast<UTexture2D>(Asset);
						Thumbnail = TextureAsset;
					}
					else if (Asset && Asset->IsA<UFont>())
					{
						FontAsset = Cast<UFont>(Asset);

						// 폰트는 글리프 아틀라스를 그대로 미리보기로 쓴다
						Thumbnail = FontAsset->AtlasTexture;
					}

					if (Thumbnail == nullptr)
					{
						Thumbnail = FileIcon;
					}
				}
				if (Thumbnail)
				{
					ImGui::ImageButton(PathString.c_str(), (ImTextureID)Thumbnail->GetResource()->GetRawSRV(), { thumbnailSize, thumbnailSize });
				}

				// 페이로드에 포인터를 그대로 실어서 받는 쪽이 AssetMap을 다시 뒤지지 않게 한다.
				if (TextureAsset && ImGui::BeginDragDropSource())
				{
					ImGui::SetDragDropPayload(EditorDragDrop::Texture, &TextureAsset, sizeof(UTexture2D*));

					// 끌고 다니는 동안 커서를 따라다니는 미리보기
					ImGui::Image((ImTextureID)TextureAsset->GetResource()->GetRawSRV(), { thumbnailSize, thumbnailSize });
					ImGui::TextUnformatted(FilenameString.c_str());

					ImGui::EndDragDropSource();
				}
				if (FontAsset && ImGui::BeginDragDropSource())
				{
					ImGui::SetDragDropPayload(EditorDragDrop::Font, &FontAsset, sizeof(UFont*));

					ImGui::TextUnformatted(FilenameString.c_str());

					ImGui::EndDragDropSource();
				}
				if (!bIsDirectory && Path.extension() == ".sequence" && ImGui::BeginDragDropSource())
				{
					ImGui::SetDragDropPayload(EditorDragDrop::Sequence, PathString.c_str(), PathString.size() + 1);
					ImGui::TextUnformatted(FilenameString.c_str());
					ImGui::EndDragDropSource();
				}
				// 더블 클릭 이벤트 처리
				if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
				{
					if (bIsDirectory)
					{
						CurrentPath /= Path.filename(); // 하위 폴더로 이동
					}
					else if (Path.extension() == ".lua")
					{
						FLuaScriptEditor::EditScript(Path.generic_string());
					}
					else if (Path.extension() == ".sequence" && OpenLevelSequence)
					{
						OpenLevelSequence(PathString);
					}
					else if ((bMeshAsset || bUnimportedFbx) && OpenMesh)
					{
						OpenMesh(PathString);
					}
				}
				if (bMeshAsset)
				{
					DrawMeshAssetMenu(PathString);
				}
				// 파일 이름 표시
				ImGui::TextWrapped("%s", FilenameString.c_str());
				if (bUnimportedFbx)
				{
					ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
					ImGui::TextWrapped("Not imported");
					ImGui::PopStyleColor();
				}

				ImGui::PopID();
			}
			ImGui::EndTable();
		}
		if (ImGui::BeginPopupContextWindow("AssetContextMenu", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
		{
			if (ImGui::BeginMenu("Create"))
			{
				if (ImGui::MenuItem("Level Sequence"))
				{
					CreateLevelSequenceFile();
				}
				ImGui::EndMenu();
			}
			ImGui::EndPopup();
		}
	}
	ImGui::EndChild();
	ImGui::End();
}

void FContentDrawerPanel::DrawMeshAssetMenu(const FString& AssetPath)
{
	if (!ImGui::BeginPopupContextItem("MeshAssetMenu"))
		return;

	if (ImGui::IsWindowAppearing())
	{
		MeshMenuSource.clear();

		FMeshAssetInfo Info;
		if (!FMeshAssetFile::ReadInfo(AssetPath, Info))
		{
			MeshMenuStatus = "The asset file could not be read";
		}
		else
		{
			MeshMenuSource = FMeshAssetFile::FindSource(Info);
			if (MeshMenuSource.empty())
			{
				MeshMenuStatus = "Source not found";
			}
			else if (Info.SourceHash == 0)
			{
				MeshMenuStatus = "Source state unknown";
			}
			else
			{
				MeshMenuStatus = FMeshAssetFile::ComputeFileHash(MeshMenuSource) == Info.SourceHash ? "Source unchanged" : "Source changed";
			}
		}
	}

	ImGui::TextUnformatted(MeshMenuStatus.c_str());
	if (!MeshMenuSource.empty())
	{
		ImGui::TextDisabled("%s", MeshMenuSource.c_str());
	}
	ImGui::Separator();

	const bool bCanReimport = ReimportMesh != nullptr;
	if (ImGui::MenuItem("Reimport", nullptr, false, bCanReimport && !MeshMenuSource.empty()))
	{
		ReimportMesh(AssetPath, MeshMenuSource);
	}
	if (ImGui::MenuItem("Reimport From File...", nullptr, false, bCanReimport))
	{
		ReimportMesh(AssetPath, FString());
	}
	ImGui::EndPopup();
}

void FContentDrawerPanel::CreateLevelSequenceFile()
{
	CreateAssetError.clear();
	ULevelSequence* Sequence = nullptr;
	UMovieScene* Scene = nullptr;
	try
	{
		fs::path Path = CurrentPath / "NewLevelSequence.sequence";
		for (int32 Index = 1; fs::exists(Path); ++Index)
		{
			Path = CurrentPath / ("NewLevelSequence_" + std::to_string(Index) + ".sequence");
		}
		Sequence = NewObject<ULevelSequence>();
		Scene = NewObject<UMovieScene>(Sequence);
		Scene->Initialize(5.f, 150);
		Sequence->Initialize(Scene);
		if (!FJsonArchive::SaveSequence(Sequence, Path.generic_string()))
		{
			CreateAssetError = "Could not create the level sequence file.";
		}
	}
	catch (const std::exception& Error)
	{
		CreateAssetError = "Create failed: " + FString(Error.what());
	}
	delete Scene;
	delete Sequence;
}

void FContentDrawerPanel::DrawPathBar()
{
	// 순회 중에 CurrentPath를 바꾸지 않도록 이동할 폴더만 기록해 두고 마지막에 반영한다.
	fs::path NextPath = CurrentPath;

	// 상위 폴더로 이동. 루트에서는 비활성화한다.
	ImGui::BeginDisabled(CurrentPath == AssetRootPath);
	if (ImGui::ArrowButton("##ParentFolder", ImGuiDir_Left))
		NextPath = CurrentPath.parent_path();
	ImGui::EndDisabled();
	ImGui::SetItemTooltip("Parent Folder");

	ImGui::SameLine();
	if (ImGui::SmallButton(AssetRootPath.string().c_str()))
		NextPath = AssetRootPath;

	fs::path SegmentPath = AssetRootPath;
	int32 SegmentIndex = 0;
	for (const fs::path& Segment : fs::relative(CurrentPath, AssetRootPath))
	{
		if (Segment == ".")
			continue;

		SegmentPath /= Segment;
		ImGui::SameLine();
		ImGui::TextDisabled(">");
		ImGui::SameLine();

		// 폴더 이름이 같아도 위치가 다르면 다른 버튼이 되도록 순번으로 ID를 나눈다.
		ImGui::PushID(SegmentIndex++);
		if (ImGui::SmallButton(Segment.string().c_str()))
			NextPath = SegmentPath;
		ImGui::PopID();
	}

	CurrentPath = NextPath;
}

void FContentDrawerPanel::RenderFolderTree(const fs::path& Path, fs::path& SelectedPath)
{
	std::error_code IterateError;
	fs::directory_iterator It(Path, IterateError);
	const fs::directory_iterator End;

	for (; !IterateError && It != End; It.increment(IterateError))
	{
		const fs::path Entry = It->path();

		std::error_code TypeError;
		const bool bIsDirectory =
			fs::is_directory(Entry, TypeError);

		if (TypeError || !bIsDirectory)
			continue;

		const FString FilenameString =
			Entry.filename().string();

		ImGuiTreeNodeFlags Flags =
			ImGuiTreeNodeFlags_OpenOnArrow |
			ImGuiTreeNodeFlags_SpanAvailWidth |
			ImGuiTreeNodeFlags_DrawLinesToNodes;

		if (Entry == SelectedPath)
			Flags |= ImGuiTreeNodeFlags_Selected;

		const bool bIsOpen =
			ImGui::TreeNodeEx(FilenameString.c_str(), Flags);

		if (ImGui::IsItemClicked())
			SelectedPath = Entry;

		if (bIsOpen)
		{
			RenderFolderTree(Entry, SelectedPath);
			ImGui::TreePop();
		}
	}

	if (IterateError)
	{
		ImGui::TextUnformatted("Could not read this folder.");
	}
}
