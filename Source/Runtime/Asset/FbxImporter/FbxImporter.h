#pragma once

#include "FbxInfo.h"
#include "Containers/Array.h"
#include "Core/Types.h"
#include "Core/EngineString.h"

struct FStaticMeshData;
struct FSkeletalMeshData;

// FbxImporter.h
namespace fbxsdk
{
	class FbxManager;
	class FbxImporter;
	class FbxIOSettings;
	class FbxScene;
}

class FFbxImporter
{
public:
	//테스트용
	static bool DebugPrintScene(const FString& Path);

	static TUniquePtr<FStaticMeshData> LoadStaticMeshData(const FString& Path, const FFbxImportOptions& Option);
	static TUniquePtr<FSkeletalMeshData> LoadSkeletalMeshData(const FString& Path);

	static bool CreateManager(fbxsdk::FbxManager*& InManager);

	static bool CreateImporter(fbxsdk::FbxManager* InManager, fbxsdk::FbxImporter*& InImporter);

	static bool CreateIOSetting(fbxsdk::FbxManager* InManager, fbxsdk::FbxIOSettings*& InIOSetting);

	static bool InitializeImporter(fbxsdk::FbxManager* InManager, fbxsdk::FbxImporter* InImporter, const FString& Path);

	static bool CreateScene(fbxsdk::FbxManager* InManager, fbxsdk::FbxScene*& InScene);

	static bool ImportFbx(fbxsdk::FbxImporter* InImporter, fbxsdk::FbxScene* InScene);


private:
	// .fbx 파일 경로 -> FFbxInfo 변환
	static bool ParseFbx(const FString& Path, FFbxInfo& Out);

	// FFbxInfo -> FSkeletalMeshData 변환
	static bool CookSkeletalMesh(const FFbxInfo& Raw, FSkeletalMeshData& Out);
	static bool CookStaticMesh(const FFbxInfo& Raw, FStaticMeshData& Out);

	static void Destroy(fbxsdk::FbxManager*& Manager, fbxsdk::FbxImporter*& Importer);
};
