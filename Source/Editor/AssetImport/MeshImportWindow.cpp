#include "EnginePCH.h"
#include "Editor/AssetImport/MeshImportWindow.h"

#include <imgui.h>

#include <algorithm>
#include <filesystem>
#include <fstream>

#include "Asset/AssetManager.h"
#include "Asset/MeshAssetFile.h"

namespace fs = std::filesystem;

namespace
{
	constexpr const char* PopupName = "Import Mesh###MeshImportWindow";
	constexpr const char* SkipListPath = "Saved/FbxImportSkipped.txt";
	constexpr float MinImportScale = 0.0001f;

	bool ContainsPath(const TArray<FString>& Paths, const FString& Path)
	{
		return std::find(Paths.begin(), Paths.end(), Path) != Paths.end();
	}

	TArray<FString> ReadSkipList()
	{
		TArray<FString> Paths;
		std::ifstream In(SkipListPath);
		FString Line;
		while (std::getline(In, Line))
		{
			if (!Line.empty())
			{
				Paths.Add(Line);
			}
		}
		return Paths;
	}

	void AppendSkipList(const FString& Path)
	{
		// 시작할 때 다시 찾는 파일만 기억하면 된다. 외부 파일은 스캔에 나오지 않는다
		if (Path.rfind("Assets/", 0) != 0 || ContainsPath(ReadSkipList(), Path))
			return;

		std::error_code Error;
		fs::create_directories(fs::path(SkipListPath).parent_path(), Error);
		std::ofstream Out(SkipListPath, std::ios::app);
		Out << Path << "\n";
	}

	void DrawCheckbox(const char* Label, bool& bValue, const char* Tooltip)
	{
		ImGui::Checkbox(Label, &bValue);
		ImGui::SetItemTooltip("%s", Tooltip);
	}

	bool IsFbx(const FString& Path)
	{
		return _stricmp(fs::path(Path).extension().string().c_str(), ".fbx") == 0;
	}
}

void FMeshImportWindow::Init(const TArray<FString>& UnimportedPaths)
{
	ResetOptions();

	const TArray<FString> Skipped = ReadSkipList();
	for (const FString& Path : UnimportedPaths)
	{
		if (!ContainsPath(Skipped, Path))
		{
			Enqueue(Path, fs::path(Path).parent_path().generic_string());
		}
	}
}

void FMeshImportWindow::Enqueue(const FString& SourcePath, const FString& TargetFolder)
{
	const FString FileName = fs::path(SourcePath).filename().string();
	const FString AssetPath = FMeshAssetFile::MakeAssetPath((fs::path(TargetFolder) / FileName).generic_string());

	// 덮어쓰면 그 메시를 쓰는 컴포넌트가 지워진 데이터를 가리키게 된다. 교체는 리임포트가 맡는다
	const FString Key = UAssetManager::GetAssetKeyFromPath(FMeshAssetFile::StripExtension(AssetPath));
	if (UAssetManager::GetAssetByPath<UAsset>(Key))
	{
		Failures.Add(std::format("{}: '{}' already exists", FileName, Key));
		return;
	}

	const bool bQueued = std::any_of(Queue.begin(), Queue.end(), [&AssetPath](const FMeshImportRequest& Request) { return Request.AssetPath == AssetPath; });
	if (!bQueued)
	{
		Queue.Add({ SourcePath, AssetPath });
	}
}

void FMeshImportWindow::EnqueueReimport(const FString& SourcePath, const FString& AssetPath, const FFbxImportOptions& StoredOptions)
{
	const bool bQueued = std::any_of(Queue.begin(), Queue.end(), [&AssetPath](const FMeshImportRequest& Request) { return Request.AssetPath == AssetPath; });
	if (bQueued)
		return;

	Queue.Add({ SourcePath, AssetPath, true, StoredOptions });
	if (Queue.Num() == 1)
	{
		ResetOptions();
	}
}

void FMeshImportWindow::ResetOptions()
{
	if (!Queue.IsEmpty() && Queue[0].bReimport)
	{
		Options = Queue[0].StoredOptions;
		return;
	}

	Options = FFbxImportOptions{};
	Options.bImportStaticMesh = true;
	Options.bImportSkeletalMesh = true;   // 둘 다 켜면 스킨이 있을 때 스켈레탈, 없을 때 스태틱이 된다
}

void FMeshImportWindow::AdvanceQueue()
{
	if (!Queue.IsEmpty())
	{
		Queue.RemoveAt(0);
	}
	++ProcessedCount;
	if (Queue.IsEmpty())
	{
		ProcessedCount = 0;
	}
}

void FMeshImportWindow::Import(const bool bInApplyToRemaining)
{
	if (Queue.IsEmpty())
		return;

	bImporting = true;
	bImportTextDrawn = false;
	bApplyToRemaining = bInApplyToRemaining;
}

void FMeshImportWindow::Skip()
{
	if (Queue.IsEmpty())
		return;

	if (!Queue[0].bReimport)
	{
		AppendSkipList(Queue[0].SourcePath);
	}
	AdvanceQueue();
	ResetOptions();
}

void FMeshImportWindow::CancelAll()
{
	Queue.Reset();
	ProcessedCount = 0;
	ResetOptions();
}

bool FMeshImportWindow::ConsumeImportRequest(FMeshImportRequest& OutRequest, FFbxImportOptions& OutOptions)
{
	if (!bImporting || !bImportTextDrawn || Queue.IsEmpty())
		return false;

	OutRequest = Queue[0];
	OutOptions = Options;
	return true;
}

void FMeshImportWindow::ReportImportResult(const FMeshImportRequest& Request, const bool bSuccess)
{
	if (!bSuccess)
	{
		Failures.Add(Request.SourcePath);
	}
	AdvanceQueue();

	// 남은 파일에 같은 옵션을 쓰는 중이면 다음 프레임에 이어서 한다
	bImportTextDrawn = false;
	if (!bApplyToRemaining || Queue.IsEmpty())
	{
		bImporting = false;
		bApplyToRemaining = false;
		ResetOptions();
	}
}

void FMeshImportWindow::DrawPrompt()
{
	const FMeshImportRequest Request = Queue[0];
	const int32 Total = ProcessedCount + Queue.Num();

	ImGui::Text("%s%s  (%d / %d)", Request.bReimport ? "Reimport: " : "", fs::path(Request.SourcePath).filename().string().c_str(), ProcessedCount + 1, Total);
	ImGui::TextDisabled("From: %s", Request.SourcePath.c_str());
	ImGui::TextDisabled("To:   %s", Request.AssetPath.c_str());
	ImGui::Separator();

	if (bImporting)
	{
		ImGui::TextUnformatted("Importing...");
		bImportTextDrawn = true;
		return;
	}

	const bool bFbx = IsFbx(Request.SourcePath);
	if (bFbx)
	{
		ImGui::SetNextItemWidth(160.0f);
		if (ImGui::DragFloat("Uniform Scale", &Options.ImportUniformScale, 0.01f, MinImportScale, 100000.0f, "%.4f", ImGuiSliderFlags_AlwaysClamp))
		{
			Options.ImportUniformScale = std::max(Options.ImportUniformScale, MinImportScale);
		}
		ImGui::SetItemTooltip("Multiplies vertices and bone translations.");

		// 리임포트는 객체를 유지하므로 메시 종류를 바꿀 수 없다
		if (!Request.bReimport)
		{
			DrawCheckbox("Import Skeletal Mesh", Options.bImportSkeletalMesh,
				"Imports as a skeletal mesh if the file has a skin.\nIf both are on, a file without a skin becomes a static mesh.");
			DrawCheckbox("Import Static Mesh", Options.bImportStaticMesh,
				"Imports as a static mesh.\nIf only this is on, a skin in the file is ignored.");
		}
		DrawCheckbox("Convert Scene Axes", Options.bConvertScene,
			"Converts the scene to Y-up right-handed before reading it.\nTurn off only if the result is rotated.");
		DrawCheckbox("Force Front X Axis", Options.bForceFrontXAxis,
			"On: the model faces +X (actor forward).\nOff: the model faces +Y (Unreal default).");
	}
	else
	{
		ImGui::TextDisabled("This file type has no import options.");
	}

	// 둘 다 끄면 FBX에서 만들 것이 없다
	const bool bCanImport = Request.bReimport || !bFbx || Options.bImportSkeletalMesh || Options.bImportStaticMesh;

	ImGui::Separator();
	ImGui::BeginDisabled(!bCanImport);
	if (ImGui::Button(Request.bReimport ? "Reimport" : "Import"))
	{
		Import(false);
	}
	ImGui::SameLine();
	if (Queue.Num() > 1 && !Request.bReimport)
	{
		if (ImGui::Button("Import All"))
		{
			Import(true);
		}
		ImGui::SetItemTooltip("Imports this and the remaining files with these options.");
		ImGui::SameLine();
	}
	ImGui::EndDisabled();
	if (ImGui::Button("Skip"))
	{
		Skip();
	}
	ImGui::SetItemTooltip("%s", Request.bReimport ? "Keep the current asset."
		: "Do not ask again for this file. Double-click it in the Content Drawer to import it later.");
	ImGui::SameLine();
	if (ImGui::Button("Cancel All"))
	{
		CancelAll();
	}
	ImGui::SetItemTooltip("Stop asking for now. Files in the project will be asked again on the next start.");
}

void FMeshImportWindow::DrawFailures()
{
	ImGui::TextUnformatted("Failed to import:");
	for (const FString& Failure : Failures)
	{
		ImGui::BulletText("%s", Failure.c_str());
	}
	ImGui::TextDisabled("See the Output Log for details.");
	ImGui::Separator();
	if (ImGui::Button("OK"))
	{
		Failures.Reset();
	}
}

void FMeshImportWindow::OnRender()
{
	if (Queue.IsEmpty() && Failures.IsEmpty())
		return;

	if (!ImGui::IsPopupOpen(PopupName))
	{
		ImGui::OpenPopup(PopupName);
	}

	const ImGuiViewport* Main = ImGui::GetMainViewport();
	ImGui::SetNextWindowViewport(Main->ID);
	ImGui::SetNextWindowPos(ImVec2(Main->Pos.x + Main->Size.x * 0.5f, Main->Pos.y + Main->Size.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (!ImGui::BeginPopupModal(PopupName, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
		return;

	if (!Queue.IsEmpty())
	{
		DrawPrompt();
	}
	else
	{
		DrawFailures();
	}

	if (Queue.IsEmpty() && Failures.IsEmpty())
	{
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
}
