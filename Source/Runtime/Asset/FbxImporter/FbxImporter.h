#pragma once

#include "FbxInfo.h"
#include "Containers/Array.h"
#include "Core/Types.h"
#include "Core/EngineString.h"

struct FSkeletalMeshData;

// FbxImporter.h
namespace fbxsdk
{
	class FbxManager;
	class FbxImporter;
	class FbxIOSettings;
	class FbxScene;
	class FbxGlobalSettings;
	class FbxNode;
}

class FFbxImporter
{
public:
	//테스트용
	static bool DebugPrintScene(const FString& Path);

	static TUniquePtr<FSkeletalMeshData> LoadStaticMeshData(const FString& Path);

	static bool CreateManager(fbxsdk::FbxManager*& InManager);

	static bool CreateImporter(fbxsdk::FbxManager* InManager, fbxsdk::FbxImporter*& InImporter);

	static bool CreateIOSetting(fbxsdk::FbxManager* InManager, fbxsdk::FbxIOSettings*& InIOSetting);

	static bool InitializeImporter(fbxsdk::FbxManager* InManager, fbxsdk::FbxImporter* InImporter, const FString& Path);

	static bool CreateScene(fbxsdk::FbxManager* InManager, fbxsdk::FbxScene*& InScene);

	static bool ImportFbx(fbxsdk::FbxImporter* InImporter, fbxsdk::FbxScene* InScene);

	static bool DisplayScene(fbxsdk::FbxScene* InScene);
	static void DisplayMetaData(fbxsdk::FbxScene* InScene);
	static void DisplayLightSettings(fbxsdk::FbxGlobalSettings* InSettings);
	static void DisplayTimeSettings(fbxsdk::FbxGlobalSettings* InSettings);
	static void DisplayHierarchy(fbxsdk::FbxScene* InScene);
	static void DisplayHierarchy(fbxsdk::FbxNode* pNode, int pDepth);
	static void DisplayContent(fbxsdk::FbxScene* pScene);
	static void DisplayContent(fbxsdk::FbxNode* pNode);

//
//	// 디버그용 출력
//	static void PrintObjInfo(const FObjInfo& ObjInfo);
//	static void PrintSMD(const FStaticMeshData& SMD);
//
//private:
//	// .obj 파일 경로 -> FObjInfo 변환
//	static bool ParseObj(const FString& Path, FObjInfo& Out);
//	// FObjInfo -> FStaticMeshData 변환
//	static bool Cook(const FObjInfo& Raw, FStaticMeshData& Out);
};
