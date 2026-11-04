#pragma once

#include "FbxInfo.h"
#include "Containers/Array.h"
#include "Core/Types.h"
#include "Core/EngineString.h"

struct FStaticMeshData;
struct FSkeletalMeshData;
struct FReferenceSkeleton;
struct FFbxImportOptions;
struct FFbxInfo;

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
	static TUniquePtr<FSkeletalMeshData> LoadSkeletalMeshData(const FString& Path, const FFbxImportOptions& Option);

	static bool CreateManager(fbxsdk::FbxManager*& InManager);

	static bool CreateImporter(fbxsdk::FbxManager* InManager, fbxsdk::FbxImporter*& InImporter);

	static bool CreateIOSetting(fbxsdk::FbxManager* InManager, fbxsdk::FbxIOSettings*& InIOSetting);

	static bool InitializeImporter(fbxsdk::FbxManager* InManager, fbxsdk::FbxImporter* InImporter, const FString& Path);

	static bool CreateScene(fbxsdk::FbxManager* InManager, fbxsdk::FbxScene*& InScene);

	static bool ImportFbx(fbxsdk::FbxImporter* InImporter, fbxsdk::FbxScene* InScene);


private:
	// .fbx 파일 경로 -> FFbxInfo 변환
	static bool ParseFbx(const FString& Path, const FFbxImportOptions& Option, FFbxInfo& Out);

	// FFbxInfo -> FSkeletalMeshData 변환
	static bool CookSkeleton(const FFbxInfo& Raw, FReferenceSkeleton& Out);
	// OutSources를 넘기면 정점마다 출처(메시, ControlPoint)를 같이 채운다 (스켈레탈 Cook용)
	static bool CookStaticMesh(const FFbxInfo& Raw, FStaticMeshData& Out, TArray<FFbxVertexSource>* OutSources = nullptr);
	// 스켈레톤 + CookStaticMesh(정점·섹션) + 정점별 뼈 영향(상위 4개, 합 1)
	static bool CookSkeletalMesh(const FFbxInfo& Raw, FSkeletalMeshData& Out);

	static void Destroy(fbxsdk::FbxManager*& Manager);

	static bool BeginFbxImport(const FString& Path, fbxsdk::FbxManager*& Manager, fbxsdk::FbxImporter*& Importer, 
							   fbxsdk::FbxIOSettings*& Ios, fbxsdk::FbxScene*& Scene);
	
	static bool PrepareScene(fbxsdk::FbxManager* Manager, fbxsdk::FbxScene* Scene, const FFbxImportOptions& Option);

	static bool ExtractScene(fbxsdk::FbxScene* Scene, const FString& Path, const FFbxImportOptions& Option, FFbxInfo& Out);

};
