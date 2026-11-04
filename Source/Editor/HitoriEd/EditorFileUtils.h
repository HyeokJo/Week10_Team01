#pragma once

#include "Engine/World.h"

class FEditorFileUtils
{
public:
	static bool NewScene(UWorld* World);
	static bool SaveScene(UWorld* World);
	static bool SaveSceneAs(UWorld* World);
	static bool LoadScene(UWorld* World);

	static const FString& GetCurrentScenePath() { return CurrentScenePath; }

	// 임포트할 FBX·OBJ를 고른다 (여러 개 가능). 취소하면 비어 있다
	static TArray<FString> OpenImportMeshDialog();

private:
	static FString OpenSaveSceneDialog();
	static FString OpenLoadSceneDialog();

	static FString CurrentScenePath;
};

