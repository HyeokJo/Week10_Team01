#include "EnginePCH.h"
#include "FbxImporter.h"

//아래 define은 안 붙여줘도 되지만 fbxsdk 클래스를 들어갔다가 나오면 모호하다고 빨간 줄 뜬다.
//아래 define을 0으로 바꿨다가 1로 다시 바꾸면 모호해지지 않는다.(1로 두도록 하자. 0이면 안된다)
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
		return false;
	}

	// Importer Initialize And Import
	if (!InitializeImporter(Manager, Importer, Path) || !ImportFbx(Importer, Scene))
	{
		return false;
	}
		
	//Display Scene
	if (!DisplayScene(Scene))
	{
		return false;
	}

	//Destroy
	if (Importer)
	{
		Importer->Destroy();
	}
	if (Manager)
	{
		Manager->Destroy();
	}

	return true;
}

TUniquePtr<FSkeletalMeshData> FFbxImporter::LoadStaticMeshData(const FString& Path)
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

bool FFbxImporter::DisplayScene(fbxsdk::FbxScene* InScene)
{
	//MetaData
	DisplayMetaData(InScene);

	FbxGlobalSettings& GSetting = InScene->GetGlobalSettings();

	//Light Color
	DisplayLightSettings(&GSetting);

	//Camera
	HTR_LOG(Info, "Default Camera : {}", GSetting.GetDefaultCamera().Buffer());

	//Global Time Setting
	DisplayTimeSettings(&GSetting);

	//Hierarchy
	DisplayHierarchy(InScene);

	return true;
}

void FFbxImporter::DisplayMetaData(fbxsdk::FbxScene* InScene)
{
	FbxDocumentInfo* sceneInfo = InScene->GetSceneInfo();

	if (sceneInfo)
	{
		HTR_LOG(Info, "--------------------Meta-Data--------------------");
		HTR_LOG(Info, "    Title: {}", sceneInfo->mTitle.Buffer());
		HTR_LOG(Info, "    Subject: {}", sceneInfo->mSubject.Buffer());
		HTR_LOG(Info, "    Author: {}", sceneInfo->mAuthor.Buffer());
		HTR_LOG(Info, "    Keywords: {}", sceneInfo->mKeywords.Buffer());
		HTR_LOG(Info, "    Revision: {}", sceneInfo->mRevision.Buffer());
		HTR_LOG(Info, "    Comment: {}", sceneInfo->mComment.Buffer());
		HTR_LOG(Info, "--------------------Meta-Data--------------------");

		FbxThumbnail* thumbnail = sceneInfo->GetSceneThumbnail();
		if (thumbnail)
		{
			HTR_LOG(Info, "    Thumbnail:");

			switch (thumbnail->GetDataFormat())
			{
				case FbxThumbnail::eRGB_24:
					HTR_LOG(Info, "        Format: RGB");
					break;
				case FbxThumbnail::eRGBA_32:
					HTR_LOG(Info, "        Format: RGBA");
					break;
			}

			switch (thumbnail->GetSize())
			{
				default:
					break;
				case FbxThumbnail::eNotSet:
					HTR_LOG(Info, "        Size: no dimensions specified ({} bytes)", thumbnail->GetSizeInBytes());
					break;
				case FbxThumbnail::e64x64:
					HTR_LOG(Info, "        Size: 64 x 64 pixels ({} bytes)", thumbnail->GetSizeInBytes());
					break;
				case FbxThumbnail::e128x128:
					HTR_LOG(Info, "        Size: 128 x 128 pixels ({} bytes)", thumbnail->GetSizeInBytes());
			}
		}
	}
}

void FFbxImporter::DisplayLightSettings(fbxsdk::FbxGlobalSettings* InSettings)
{
	FbxColor pValue = InSettings->GetAmbientColor();

	FbxString lString;

	lString = "Ambient Color : ";
	lString += (float)pValue.mRed;

	lString += " (red), ";
	lString += (float)pValue.mGreen;

	lString += " (green), ";
	lString += (float)pValue.mBlue;

	lString += " (blue)";

	HTR_LOG(Info, "{}", lString.Buffer());
}

void FFbxImporter::DisplayTimeSettings(fbxsdk::FbxGlobalSettings* InSettings)
{
	HTR_LOG(Info, "Time Node : {}", FbxGetTimeModeName(InSettings->GetTimeMode()));
	FbxTimeSpan lTs;
	FbxTime     lStart, lEnd;
	char lTimeString[256];
	InSettings->GetTimelineDefaultTimeSpan(lTs);
	lStart = lTs.GetStart();
	lEnd = lTs.GetStop();
	HTR_LOG(Info, "Timeline default timespan: ");
	HTR_LOG(Info, "     Start: {}", lStart.GetTimeString(lTimeString, FbxUShort(256)));
	HTR_LOG(Info, "     Stop : {}", lEnd.GetTimeString(lTimeString, FbxUShort(256)));
}

void FFbxImporter::DisplayHierarchy(fbxsdk::FbxScene* InScene)
{
	int i;
	FbxNode* lRootNode = InScene->GetRootNode();

	for (i = 0; i < lRootNode->GetChildCount(); i++)
	{
		DisplayHierarchy(lRootNode->GetChild(i), 0);
	}
}

void FFbxImporter::DisplayHierarchy(fbxsdk::FbxNode* pNode, int pDepth)
{
	FbxString lString;
	int i;

	for (i = 0; i < pDepth; i++)
	{
		lString += "  ";
	}

	lString += pNode->GetName();
	HTR_LOG(Info, "{}", lString.Buffer());

	for (i = 0; i < pNode->GetChildCount(); i++)
	{
		DisplayHierarchy(pNode->GetChild(i), pDepth + 1);
	}
}

void FFbxImporter::DisplayContent(fbxsdk::FbxScene* pScene)
{
	int i;
	FbxNode* lNode = pScene->GetRootNode();

	if (lNode)
	{
		for (i = 0; i < lNode->GetChildCount(); i++)
		{
			DisplayContent(lNode->GetChild(i));
		}
	}
}

void FFbxImporter::DisplayContent(fbxsdk::FbxNode* pNode)
{
}
