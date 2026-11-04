#pragma once

#include "Asset/FbxImporter/FbxInfo.h"
#include "Containers/Array.h"
#include "Core/EngineString.h"

// 임포트할 파일 하나. 원본은 프로젝트 밖에 있을 수 있어서 저장 위치를 따로 갖는다
struct FMeshImportRequest
{
	FString SourcePath;
	FString AssetPath;   // 저장할 .hasset 경로
	bool bReimport = false;
	FFbxImportOptions StoredOptions;   // 리임포트 창의 초기값. 지난번 임포트의 옵션이다
};

// 메시 임포트 대기열과 옵션 창
// 파일마다 옵션이 다를 수 있어서 한 개씩 묻는다. 실제 임포트는 그리기 도중에 하지 않고 UEditorEngine이 프레임 시작에 한다
class FMeshImportWindow
{
public:
	// 시작할 때 찾은 미임포트 FBX를 대기열에 넣는다. 전에 Skip한 파일은 묻지 않는다
	void Init(const TArray<FString>& UnimportedPaths);
	// 사용자가 직접 고른 파일. Skip 기록과 상관없이 묻는다. 결과는 TargetFolder에 저장한다
	void Enqueue(const FString& SourcePath, const FString& TargetFolder);
	// 이미 있는 에셋의 내용을 교체한다. 옵션 창은 지난번 옵션에서 시작한다
	void EnqueueReimport(const FString& SourcePath, const FString& AssetPath, const FFbxImportOptions& StoredOptions);

	void OnRender();

	// 지금 처리할 임포트가 있으면 true다. "Importing" 표시가 화면에 나간 뒤에 true가 된다
	bool ConsumeImportRequest(FMeshImportRequest& OutRequest, FFbxImportOptions& OutOptions);
	void ReportImportResult(const FMeshImportRequest& Request, bool bSuccess);

	// 창의 버튼이 하는 일
	// bApplyToRemaining이면 남은 파일도 묻지 않고 지금 옵션으로 임포트한다
	void Import(bool bApplyToRemaining);
	// 프로젝트 안의 파일은 Saved에 기록해 다음 실행부터 묻지 않는다
	void Skip();
	// 이번 실행에서만 그만 묻는다. 프로젝트 안의 미임포트 파일은 다음 실행에서 다시 묻는다
	void CancelAll();

	FFbxImportOptions& GetOptions() { return Options; }
	int32 GetPendingCount() const { return Queue.Num(); }

private:
	void ResetOptions();
	void DrawPrompt();
	void DrawFailures();
	void AdvanceQueue();

	TArray<FMeshImportRequest> Queue;
	TArray<FString> Failures;
	FFbxImportOptions Options;
	int32 ProcessedCount = 0;

	bool bImporting = false;
	bool bImportTextDrawn = false;
	bool bApplyToRemaining = false;
};
