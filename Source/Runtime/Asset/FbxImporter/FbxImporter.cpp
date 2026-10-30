#include "EnginePCH.h"
#include "FbxImporter.h"

//아래 define은 안 붙여줘도 되지만 fbxsdk 클래스를 들어갔다가 나오면 모호하다고 빨간 줄 뜬다.
//빌드는 되지만 좀 그래서 아래 define을 0으로 바꿨다가 1로 다시 바꾸면 모호해지지 않는다.(1로 두도록 하자. 0이면 안된다)
#define FBXSDK_NAMESPACE_USING 1
#include <fbxsdk.h>

#include "Render/SkeletalMeshData.h"

//#include <fstream>
//#include <sstream>
//#include <charconv>
//#include <filesystem>
//#include <algorithm>

#include "Core/EngineLog.h"
//#include "Containers/Map.h"
//#include "Containers/StringView.h"
//#include "Render/Vertex.h"

bool FFbxImporter::DebugPrintScene(const FString& Path)
{
	//FbxManager::Create → FbxIOSettings → FbxImporter::Initialize/Import → 로그 → Destroy
	//samples/Common/Common.cxx의 InitializeSdkObjects와 LoadScene 참고



	//Initialize/Import

	//로그

	//Destroy

	//-------------------------

	//Create
	FbxManager* Manager = nullptr;
	FbxScene* Scene = nullptr;
	bool lResult;

	// Prepare the FBX SDK.
	//The first thing to do is to create the FBX Manager which is the object allocator for almost all the classes in the SDK
	HTR_LOG(Info, "FBX Import Test Start!");

	Manager = FbxManager::Create();
	if (!Manager)
	{
		HTR_LOG(Error, "Error: Unable to create FBX Manager");
		return false;
	}
	else
	{
		HTR_LOG(Info, "Create Fbx Manager");
		HTR_LOG(Info, "Autodesk FBX SDK version {}", Manager->GetVersion());
	}

	//FbxIOSettings
	FbxIOSettings* ios = FbxIOSettings::Create(Manager, IOSROOT);
	if (!ios)
	{
		HTR_LOG(Error, "Error: Unable to create FBX IOSetting");
		return false;
	}

	//Create an IOSettings object. This object holds all import/export settings.
	Manager->SetIOSettings(ios);

#ifndef FBXSDK_ENV_WINSTORE
	//Load plugins from the executable directory (optional)
	FbxString lPath = FbxGetApplicationDirectory();
	Manager->LoadPluginsDirectory(lPath.Buffer());
#endif

	//Create an FBX scene. This object holds most objects imported/exported from/to files.
	Scene = FbxScene::Create(Manager, "My Scene");
	if (!Scene)
	{
		FBXSDK_printf("Error: Unable to create FBX scene!\n");
		return false;
	}

	

	Manager->Destroy();
	return false;
}

TUniquePtr<FSkeletalMeshData> FFbxImporter::LoadStaticMeshData(const FString& Path)
{
	return TUniquePtr<FSkeletalMeshData>();
}
