#include "EnginePCH.h"
#include "FbxImporter.h"
#include "FbxDebugScene.h"
#include "Render/SkeletalMeshData.h"
#include "Render/StaticMeshData.h"
#include "Asset/ObjImporter/StaticMeshBake.h"

#include "Core/EngineLog.h"

//아래 define은 안 붙여줘도 되지만 fbxsdk 클래스를 들어갔다가 나오면 모호하다고 빨간 줄 뜬다.
//아래 define을 0으로 바꿨다가 1로 다시 바꾸면 모호해지지 않는다.(1로 두도록 하자. 0이면 안된다)
#define FBXSDK_NAMESPACE_USING 1
#include <fbxsdk.h>


//#include <fstream>
//#include <sstream>
//#include <charconv>
//#include <filesystem>
//#include <algorithm>

//#include "Containers/Map.h"
//#include "Containers/StringView.h"
//#include "Render/Vertex.h"

bool FFbxImporter::DebugPrintScene(const FString& Path)
{
	//FbxManager::Create → FbxIOSettings → FbxImporter::Initialize/Import → 로그 → Destroy
	//samples/Common/Common.cxx의 InitializeSdkObjects와 LoadScene 참고

	if (Path.size() == 0)
	{
		HTR_LOG(Error, "FBX File Path is Empty!");
		return false;
	}

	// Prepare the FBX SDK.
	FbxManager* Manager = nullptr;
	FbxImporter* Importer = nullptr;
	FbxIOSettings* Ios = nullptr;
	FbxScene* Scene = nullptr;

	HTR_LOG(Info, "FBX Import Test Start!");

	//Manager Create
	//FbxIOSettings Create : 무엇을 읽을지 고르는 옵션(머티리얼, 애니메이션 등)
	//Importer Create : 파일을 열고(Initialize) Scene을 채운다. 작업을 마치고 Destroy해도 된다.
	//Scene Create : fbx를 읽은 결과를 담는 그래프(노드 트리, 축, 단위, fps 등 전역 설정, 포즈, AnimStack이 모두 들어간다)
	if (!CreateManager(Manager) || !CreateIOSetting(Manager, Ios) || !CreateImporter(Manager, Importer) || !CreateScene(Manager, Scene))
	{
		Destroy(Manager, Importer);
		return false;
	}

	// Importer Initialize And Import
	if (!InitializeImporter(Manager, Importer, Path) || !ImportFbx(Importer, Scene))
	{
		Destroy(Manager, Importer);
		return false;
	}
		
	//Display Scene : 로그 찍는 함수들은 FbxDebugScene에 모아 두었다
	FFbxDebugScene::DisplayScene(Scene);

	//Texture Path : 상대 경로를 fbx 파일 폴더 기준으로 풀어야 해서 Path가 필요하다
	HTR_LOG(Info, "--------------------Texture Path--------------------");
	FFbxDebugScene::DisplayTexturePaths(Scene, Path);

	//Destroy
	Destroy(Manager, Importer);	

	return true;
}

TUniquePtr<FStaticMeshData> FFbxImporter::LoadStaticMeshData(const FString& Path, const FFbxImportOptions& Option)
{
	const FString BinPath = Path + ".bin";

	//// .bin 파일 읽기
	//if (TUniquePtr<FStaticMeshData> Baked = FStaticMeshBake::ReadBaked(BinPath))
	//{
	//	return Baked;
	//}

	FFbxInfo Info;
	TUniquePtr<FStaticMeshData> Data = MakeUnique<FStaticMeshData>();

	if (!ParseFbx(Path, Info) || !CookStaticMesh(Info, *Data))
	{
		return nullptr;
	}

	FStaticMeshBake::WriteBaked(BinPath, *Data);
	return Data;
}

TUniquePtr<FSkeletalMeshData> FFbxImporter::LoadSkeletalMeshData(const FString& Path)
{
	return TUniquePtr<FSkeletalMeshData>();
}

bool FFbxImporter::CreateManager(fbxsdk::FbxManager*& InManager)
{
	InManager = FbxManager::Create();
	if (!InManager)
	{
		HTR_LOG(Error, "Error: Unable to create FBX Manager");
		return false;
	}
	
	HTR_LOG(Info, "Create Fbx Manager");
	HTR_LOG(Info, "Autodesk FBX SDK version {}", InManager->GetVersion());
	return true;	
}

bool FFbxImporter::CreateImporter(fbxsdk::FbxManager* InManager, fbxsdk::FbxImporter*& InImporter)
{
	InImporter = FbxImporter::Create(InManager, "FbxImporter");
	if (!InImporter)
	{
		HTR_LOG(Error, "Error: Unable to create FBX Importer");
		return false;
	}
	return true;
}

bool FFbxImporter::CreateIOSetting(fbxsdk::FbxManager* InManager, fbxsdk::FbxIOSettings*& InIOSetting)
{
	InIOSetting = FbxIOSettings::Create(InManager, IOSROOT);
	if (!InIOSetting)
	{
		HTR_LOG(Error, "Error: Unable to create FBX IOSetting");
		return false;
	}

	InManager->SetIOSettings(InIOSetting);
	return true;
}

bool FFbxImporter::InitializeImporter(fbxsdk::FbxManager* InManager, fbxsdk::FbxImporter* InImporter, const FString& Path)
{
	int lFileMajor, lFileMinor, lFileRevision;
	int lSDKMajor, lSDKMinor, lSDKRevision;
	int lAnimStackCount;

	// Get the file version number generate by the FBX SDK.
	FbxManager::GetFileFormatVersion(lSDKMajor, lSDKMinor, lSDKRevision);

	//Initialize
	bool bResult = InImporter->Initialize(Path.c_str(), -1, InManager->GetIOSettings());

	InImporter->GetFileVersion(lFileMajor, lFileMinor, lFileRevision);

	if (!bResult)
	{
		FbxString error = InImporter->GetStatus().GetErrorString();
		HTR_LOG(Error, "Error: FbxImporter::Initialize() failed");
		HTR_LOG(Error, "Error returned: {}", error.Buffer());

		if (InImporter->GetStatus().GetCode() == FbxStatus::eInvalidFileVersion)
		{
			HTR_LOG(Error, "FBX file format version for this FBX SDK is {}.{}.{}", lSDKMajor, lSDKMinor, lSDKRevision);
			HTR_LOG(Error, "FBX file format version for file '{}' is {}.{}.{}", Path, lFileMajor, lFileMinor, lFileRevision);

			return false;
		}
	}

	HTR_LOG(Info, "FBX file format version for this FBX SDK is {}.{}.{}", lSDKMajor, lSDKMinor, lSDKRevision);

	if (InImporter->IsFBX())
	{
		HTR_LOG(Info, "FBX file format version for file '{}' is {}.{}.{}", Path, lFileMajor, lFileMinor, lFileRevision);

		HTR_LOG(Info, "Animation Stack Information");

		lAnimStackCount = InImporter->GetAnimStackCount();

		HTR_LOG(Info, "    Number of Animation Stacks: {}", lAnimStackCount);
		HTR_LOG(Info, "    Current Animation Stack: \"{}\"", InImporter->GetActiveAnimStackName().Buffer());

		for (int i = 0; i < lAnimStackCount; i++)
		{
			FbxTakeInfo* lTakeInfo = InImporter->GetTakeInfo(i);

			HTR_LOG(Info, "    Animation Stack {}", i);
			HTR_LOG(Info, "         Name: \"{}\"", lTakeInfo->mName.Buffer());
			HTR_LOG(Info, "         Description: \"{}\"", lTakeInfo->mDescription.Buffer());

			// Change the value of the import name if the animation stack should be imported 
			// under a different name.
			HTR_LOG(Info, "         Import Name: \"{}\"", lTakeInfo->mImportName.Buffer());

			// Set the value of the import state to false if the animation stack should be not
			// be imported. 
			HTR_LOG(Info, "         Import State: {}", lTakeInfo->mSelect ? "true" : "false");
		}

		// Set the import states. By default, the import states are always set to 
		// true. The code below shows how to change these states.		
		(*(InManager->GetIOSettings())).SetBoolProp(IMP_FBX_MATERIAL, true);
		(*(InManager->GetIOSettings())).SetBoolProp(IMP_FBX_TEXTURE, true);
		(*(InManager->GetIOSettings())).SetBoolProp(IMP_FBX_LINK, true);
		(*(InManager->GetIOSettings())).SetBoolProp(IMP_FBX_SHAPE, true);
		(*(InManager->GetIOSettings())).SetBoolProp(IMP_FBX_GOBO, true);
		(*(InManager->GetIOSettings())).SetBoolProp(IMP_FBX_ANIMATION, true);
		(*(InManager->GetIOSettings())).SetBoolProp(IMP_FBX_GLOBAL_SETTINGS, true);
	}

	return true;
}

bool FFbxImporter::CreateScene(fbxsdk::FbxManager* InManager, fbxsdk::FbxScene*& InScene)
{
	InScene = FbxScene::Create(InManager, "My Scene");
	if (!InScene)
	{
		HTR_LOG(Error, "error: unable to create fbx scene!");
		return false;
	}
	return true;
}

bool FFbxImporter::ImportFbx(fbxsdk::FbxImporter* InImporter, fbxsdk::FbxScene* InScene)
{
	bool bResult = InImporter->Import(InScene);

	if (!bResult && InImporter->GetStatus() == FbxStatus::ePasswordError)
	{
		HTR_LOG(Error, "Fbx Import failed. PasswordError.");
		return false;
	}

	if (!bResult || (InImporter->GetStatus() != FbxStatus::eSuccess))
	{
		if (bResult)
		{
			HTR_LOG(Error, "WARNING:");
			HTR_LOG(Error, "   The importer was able to read the file but with errors.");
			HTR_LOG(Error, "   Loaded scene may be incomplete.\n");
		}
		else
		{
			HTR_LOG(Error, "Importer failed to load the file!\n");
		}

		if (InImporter->GetStatus() != FbxStatus::eSuccess)
		{
			HTR_LOG(Error, "   Last error message: {}\n", InImporter->GetStatus().GetErrorString());
		}

		FbxArray<FbxString*> history;
		InImporter->GetStatus().GetErrorStringHistory(history);
		if (history.GetCount() > 1)
		{
			HTR_LOG(Error, "   Error history stack:");
			for (int i = 0; i < history.GetCount(); i++)
			{
				HTR_LOG(Error, "      {}", history[i]->Buffer());
			}
		}
		FbxArrayDelete<FbxString*>(history);

		return false;
	}

	return bResult;
}

bool FFbxImporter::ParseFbx(const FString& Path, FFbxInfo& Out)
{
	if (Path.size() == 0)
	{
		HTR_LOG(Error, "FBX File Path is Empty!");
		return false;
	}

	// Prepare the FBX SDK.
	FbxManager* Manager = nullptr;
	FbxImporter* Importer = nullptr;
	FbxIOSettings* Ios = nullptr;
	FbxScene* Scene = nullptr;

	HTR_LOG(Info, "FBX Import Test Start!");

	//Manager Create
	//FbxIOSettings Create : 무엇을 읽을지 고르는 옵션(머티리얼, 애니메이션 등)
	//Importer Create : 파일을 열고(Initialize) Scene을 채운다. 작업을 마치고 Destroy해도 된다.
	//Scene Create : fbx를 읽은 결과를 담는 그래프(노드 트리, 축, 단위, fps 등 전역 설정, 포즈, AnimStack이 모두 들어간다)
	if (!CreateManager(Manager) || !CreateIOSetting(Manager, Ios) || !CreateImporter(Manager, Importer) || !CreateScene(Manager, Scene))
	{
		Destroy(Manager, Importer);
		return false;
	}

	// Importer Initialize And Import
	if (!InitializeImporter(Manager, Importer, Path) || !ImportFbx(Importer, Scene))
	{
		Destroy(Manager, Importer);
		return false;
	}


}

bool FFbxImporter::CookSkeletalMesh(const FFbxInfo& Raw, FSkeletalMeshData& Out)
{
	return false;
}

bool FFbxImporter::CookStaticMesh(const FFbxInfo& Raw, FStaticMeshData& Out)
{
	return false;
}

void FFbxImporter::Destroy(FbxManager*& Manager, FbxImporter*& Importer)
{
	if (Importer)
	{
		Importer->Destroy();
	}
	if (Manager)
	{
		Manager->Destroy();
	}
}
