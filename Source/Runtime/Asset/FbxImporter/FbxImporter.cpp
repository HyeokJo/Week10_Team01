#include "EnginePCH.h"
#include "FbxImporter.h"
#include "FbxDebugScene.h"
#include "Render/SkeletalMeshData.h"
#include "Render/StaticMeshData.h"
#include "Asset/ObjImporter/StaticMeshBake.h"

#include "Core/EngineLog.h"
#include "Containers/Map.h"

//아래 define은 안 붙여줘도 되지만 fbxsdk 클래스를 들어갔다가 나오면 모호하다고 빨간 줄 뜬다.
//아래 define을 0으로 바꿨다가 1로 다시 바꾸면 모호해지지 않는다.(1로 두도록 하자. 0이면 안된다)
#define FBXSDK_NAMESPACE_USING 1
#include <fbxsdk.h>


//#include <fstream>
//#include <sstream>
//#include <charconv>
//#include <filesystem>
//#include <algorithm>

//#include "Containers/StringView.h"
//#include "Render/Vertex.h"

namespace
{
	// Mapping으로 번호를 고르고, Reference가 IndexToDirect면 한 번 더 거친다.
	template <typename T>
	bool GetLayerValue(FbxLayerElementTemplate<T>* Element, int ControlPointIndex, int PolygonVertexIndex, int PolygonIndex, T& OutValue)
	{
		if (Element == nullptr) return false;

		int Index = 0;
		switch (Element->GetMappingMode())
		{
			case FbxLayerElement::eByControlPoint:
				Index = ControlPointIndex;
				break;
			case FbxLayerElement::eByPolygonVertex:
				Index = PolygonVertexIndex;
				break;
			case FbxLayerElement::eByPolygon:
				Index = PolygonIndex;
				break;
			case FbxLayerElement::eAllSame:
				Index = 0;
				break;
			default:
				return false;
		}

		//인덱스 거쳐서 간다
		if (Element->GetReferenceMode() == FbxLayerElement::eIndexToDirect)
		{
			//인덱스 검사
			if (Index < 0 || Index >= Element->GetIndexArray().GetCount())
			{
				return false;
			}

			Index = Element->GetIndexArray().GetAt(Index);
		}

		if (Index < 0 || Index >= Element->GetDirectArray().GetCount())
		{
			return false;
		}

		OutValue = Element->GetDirectArray().GetAt(Index);

		return true;
	}

	// DisplayContent와 달리 끝까지 내려간다. 순환은 PrepareScene에서 막았다.
	void CollectMeshNodes(FbxNode* Node, TArray<FbxNode*>& OutNodes)
	{
		if (Node == nullptr) return;

		//Attribute 타입이 eMesh인 것만 고르기
		//eMesh : Attribute 타입이 FbxMesh인 노드이다
		//		ControlPoint와 폴리곤 데이터를 가진 노드
		// eMesh 노드는 오브젝트 하나 단위이다
		//		Body, Hair, Cloth와 같이 메시가 쪼개져 있다면 eMesh 노드가 여러개다
		//		한 메시 안에서 머티리얼이 다르면 노드를 쪼개는 게 아니라 
		//		폴리곤마다 머티리얼 번호(ElementMaterial, ByPolygon)가 붙는다(섹션이 나뉜다)
		FbxNodeAttribute* Attribute = Node->GetNodeAttribute();
		if (Attribute && Attribute->GetAttributeType() == FbxNodeAttribute::eMesh)
		{
			OutNodes.Add(Node);
		}

		//재귀로 eMesh 노드 찾기
		// eMesh 노드만 찾나? 다른 타입의 노드일 순 없을까?
		for (int i = 0; i < Node->GetChildCount(); i++)
		{
			CollectMeshNodes(Node->GetChild(i), OutNodes);
		}
	}

	// 프로퍼티(소켓)에 꽂힌 파일 텍스처를 찾는다
	FbxFileTexture* FindFileTexture(const FbxProperty& Property, const char* PropertyName, const FString& FbxPath)
	{
		// 소켓에 파일 텍스처가 바로 꽂힌 경우
		const int32 FileCount = Property.GetSrcObjectCount<FbxFileTexture>();
		if (FileCount > 0)
		{
			if (FileCount > 1)
			{
				HTR_LOG(Warning, "[FBX] {}: {} has {} textures, using the first", FbxPath, PropertyName, FileCount);
			}
			return Property.GetSrcObject<FbxFileTexture>(0);
		}

		// 여러 장을 섞은 경우: 소켓 -> LayeredTexture -> FileTexture
		const int32 LayeredCount = Property.GetSrcObjectCount<FbxLayeredTexture>();
		for (int32 LayeredIndex = 0; LayeredIndex < LayeredCount; ++LayeredIndex)
		{
			FbxLayeredTexture* Layered = Property.GetSrcObject<FbxLayeredTexture>(LayeredIndex);
			const int32 LayerCount = Layered->GetSrcObjectCount<FbxFileTexture>();
			if (LayerCount == 0) continue;

			if (LayerCount > 1)
			{
				// 엔진은 슬롯당 텍스처 한 장만 쓴다. 블렌드 결과는 재현하지 않는다
				HTR_LOG(Warning, "[FBX] {}: {} is a layered texture ({} layers), using layer 0 only",
						FbxPath, PropertyName, LayerCount);
			}
			return Layered->GetSrcObject<FbxFileTexture>(0);
		}

		// 텍스처 없음 (ProceduralTexture처럼 파일이 없는 텍스처 포함)
		return nullptr;
	}

	//텍스처 경로
	FString FindTexturePath(FbxSurfaceMaterial* Material, const char* PropertyName, const FString& FbxPath)
	{
		FbxProperty Property = Material->FindProperty(PropertyName);
		if (!Property.IsValid()) return {};

		FbxFileTexture* Texture = FindFileTexture(Property, PropertyName, FbxPath);
		if (Texture == nullptr) return {};

		auto ToPath = [](const char* Utf8)
			{
				return fs::path(reinterpret_cast<const char8_t*>(Utf8 ? Utf8 : ""));
			};

		const fs::path FbxDirectory = fs::path(FbxPath).parent_path();
		const fs::path Candidates[] =
		{
			ToPath(Texture->GetFileName()),                              // 절대 경로 또는 .fbm 추출 경로
			FbxDirectory / ToPath(Texture->GetRelativeFileName()),       // fbx 폴더 기준 상대 경로
			FbxDirectory / ToPath(Texture->GetFileName()).filename(),    // 파일 이름만 같은 폴더에 있는 경우
		};

		std::error_code Error;
		for (const fs::path& Candidate : Candidates)
		{
			if (!Candidate.empty() && fs::exists(Candidate, Error))
			{
				// 엔진은 작업 폴더(저장소 루트) 기준 경로를 쓴다
				return fs::relative(Candidate, fs::current_path(), Error).generic_string();
			}
		}

		HTR_LOG(Warning, "[FBX] {}: texture not found for {} ({})", FbxPath, PropertyName, Texture->GetFileName());
		return {};
	}

	//머티리얼
	FFbxMaterialInfo ParseMaterial(FbxSurfaceMaterial* Material, const FString& FbxPath)
	{
		FFbxMaterialInfo Info;
		Info.Name = Material->GetName();

		// Phong은 Lambert를 상속한다 -> 이 조건은 Phong에도 참
		if (Material->GetClassId().Is(FbxSurfaceLambert::ClassId))
		{
			FbxSurfaceLambert* Lambert = static_cast<FbxSurfaceLambert*>(Material);
			const FbxDouble3 Diffuse = Lambert->Diffuse.Get();
			const double Factor = Lambert->DiffuseFactor.Get();
			Info.Diffuse = FVector(static_cast<float>(Diffuse[0] * Factor), static_cast<float>(Diffuse[1] * Factor), static_cast<float>(Diffuse[2] * Factor));

			// TransparencyFactor는 툴마다 해석이 달라서, 처음에는 불투명으로 고정하고 화면이 맞으면 켠다.
			// (엔진은 BaseColor.A < 1이면 AlphaBlend로 그려서 잘못 읽으면 메시가 사라져 보인다)
			// Info.Opacity = float(1.0 - Lambert->TransparencyFactor.Get());
			Info.Opacity = 1.0f;
		}

		if (Material->GetClassId().Is(FbxSurfacePhong::ClassId))
		{
			FbxSurfacePhong* Phong = static_cast<FbxSurfacePhong*>(Material);
			const FbxDouble3 Specular = Phong->Specular.Get();
			const double Factor = Phong->SpecularFactor.Get();
			Info.Specular = FVector(static_cast<float>(Specular[0] * Factor), static_cast<float>(Specular[1] * Factor), static_cast<float>(Specular[2] * Factor));
			Info.Shininess = static_cast<float>(Phong->Shininess.Get());
		}

		Info.DiffuseTexture = FindTexturePath(Material, FbxSurfaceMaterial::sDiffuse, FbxPath);
		Info.NormalTexture = FindTexturePath(Material, FbxSurfaceMaterial::sNormalMap, FbxPath);
		if (Info.NormalTexture.empty())
		{
			Info.NormalTexture = FindTexturePath(Material, FbxSurfaceMaterial::sBump, FbxPath);
		}

		return Info;
	}

	// 노드의 로컬 슬롯 번호 -> 전역 슬롯 번호 표를 만든다.
	// 여러 노드가 같은 머티리얼을 쓰면 같은 전역 번호를 준다.
	TArray<int32> ParseNodeMaterials(FbxNode* Node, const FString& FbxPath, TMap<FbxSurfaceMaterial*, int32>& MaterialMap,
									 TArray<FFbxMaterialInfo>& OutMaterials)
	{
		TArray<int32> LocalToGlobal;

		//eMesh 노드들 순회하면서 머티리얼 저장
		for (int i = 0; i < Node->GetMaterialCount(); i++)
		{
			FbxSurfaceMaterial* Material = Node->GetMaterial(i);
			if (Material == nullptr)
			{
				//없다면 -1로 넣기. -1이라면 Default Material로 처리한다
				LocalToGlobal.Add(-1);
				continue;
			}

			//이미 등록된 머티리얼이라면 공유하기
			if (const int32* Found = MaterialMap.Find(Material))
			{
				LocalToGlobal.Add(*Found);
				continue;
			}

			//새로 등록될 머티리얼
			const int32 GlobalIndex = OutMaterials.Num();

			//머티리얼 Map에 추가
			MaterialMap.Add(Material, GlobalIndex);

			//Material Info에 추가
			OutMaterials.Add(ParseMaterial(Material, FbxPath));

			//해당 eMesh 노드가 어떤 머티리얼 인덱스를 쓰는지 추가
			LocalToGlobal.Add(GlobalIndex);
		}

		return LocalToGlobal;
	}

	//메시
	bool ExtractMesh(FbxNode* Node, const TArray<int32>& LocalToGlobal, const FFbxImportOptions& Option, FFbxMeshInfo& Out)
	{
		//ExtractMesh에서 머티리얼 요소가 없거나 로컬 번호가 범위 밖이면 GlobalSlot = -1을 넣는다.

		if (Node == nullptr) return false;

		Out.Name = Node->GetName();

		FbxMesh* Mesh = Node->GetMesh();
		if (Mesh == nullptr) return false;


		// 노드 행렬 : Global(Pre-Rotation, 축/단위 변환 포함) * Geometric(이 노드의 메시에만)
		const FbxAMatrix Global = Node->EvaluateGlobalTransform();

		const FbxAMatrix Geometric(
			Node->GetGeometricTranslation(FbxNode::eSourcePivot),
			Node->GetGeometricRotation(FbxNode::eSourcePivot),
			Node->GetGeometricScaling(FbxNode::eSourcePivot));

		FbxAMatrix M = Global * Geometric;

		// Import Uniform Scale : 결과 전체를 키운다
		if (Option.ImportUniformScale != 1.0f)
		{
			FbxAMatrix Scale;   // 기본 생성 = 단위 행렬
			const double S = Option.ImportUniformScale;
			Scale.SetS(FbxVector4(S, S, S));
			M = Scale * M;
		}

		// 노멀은 이동을 빼고 역전치를 쓴다 (비균일 스케일에서도 면에 수직 유지)
		FbxAMatrix NormalMatrix = M;
		NormalMatrix.SetT(FbxVector4(0.0, 0.0, 0.0));
		NormalMatrix = NormalMatrix.Inverse().Transpose();

		// 거울 스케일(행렬식 < 0)이면 감기 방향이 한 번 더 뒤집힌다 -> Cook에서 처리
		Out.bFlipWinding = M.Determinant() < 0.0;

		// ControlPoint : 행렬을 곱해서 저장
		const int ControlPointCount = Mesh->GetControlPointsCount();
		const FbxVector4* ControlPoints = Mesh->GetControlPoints();
		Out.ControlPoints.Reserve(ControlPointCount);
		for (int i = 0; i < ControlPointCount; i++)
		{
			// 로그상 ControlPoint의 w가 0이다. MultT가 이동까지 반영하는지 box.fbx로 꼭 한 번 확인할 것.
			const FbxVector4 P = M.MultT(ControlPoints[i]);
			Out.ControlPoints.Add(FVector(static_cast<float>(P[0]), static_cast<float>(P[1]), static_cast<float>(P[2])));
		}

		// 레이어 준비
		FbxStringList UVSetNames;
		Mesh->GetUVSetNames(UVSetNames);
		const char* UVSetName = UVSetNames.GetCount() > 0 ? UVSetNames.GetStringAt(0) : nullptr;

		Out.bHasUVs = UVSetName != nullptr;
		Out.bHasNormals = Mesh->GetElementNormalCount() > 0;

		FbxGeometryElementVertexColor* ColorElement = Mesh->GetElementVertexColorCount() > 0 ? Mesh->GetElementVertexColor(0) : nullptr;
		FbxGeometryElementMaterial* MaterialElement = Mesh->GetElementMaterialCount() > 0 ? Mesh->GetElementMaterial(0) : nullptr;

		// 폴리곤 순회
		const int PolygonCount = Mesh->GetPolygonCount();

		//3개씩 삼각형
		Out.PolygonVertices.Reserve(PolygonCount * 3);

		//삼각형별 "전역" 슬롯 번호
		Out.TriangleMaterials.Reserve(PolygonCount);

		for (int p = 0; p < PolygonCount; p++)
		{
			if (Mesh->GetPolygonSize(p) != 3)
			{
				// Triangulate에 실패했다면 이곳으로 들어온다
				HTR_LOG(Warning, "[FBX] {}: polygon {} has {} vertices, skipped", Out.Name, p, Mesh->GetPolygonSize(p));
				continue;
			}

			// 이 폴리곤의 첫 PolygonVertex 번호. 샘플의 vertexId 카운터와 같은 값이다.
			const int PolygonStart = Mesh->GetPolygonVertexIndex(p);

			for (int v = 0; v < 3; v++)
			{
				FFbxPolygonVertex Vertex;
				Vertex.ControlPointIndex = Mesh->GetPolygonVertex(p, v);

				if (Out.bHasNormals)
				{
					FbxVector4 Normal;
					if (Mesh->GetPolygonVertexNormal(p, v, Normal))
					{
						Normal[3] = 0.0;
						const FbxVector4 N = NormalMatrix.MultT(Normal);
						Vertex.Normal = FVector(static_cast<float>(N[0]), static_cast<float>(N[1]), static_cast<float>(N[2])).GetSafeNormal();
					}
				}

				if (Out.bHasUVs)
				{
					FbxVector2 UV;
					bool bUnmapped = false;
					if (Mesh->GetPolygonVertexUV(p, v, UVSetName, UV, bUnmapped) && !bUnmapped)
					{
						Vertex.UV = FVector2(static_cast<float>(UV[0]), static_cast<float>(UV[1]));   // V 뒤집기는 Cook에서
					}
				}

				FbxColor Color;
				if (GetLayerValue(ColorElement, Vertex.ControlPointIndex, PolygonStart + v, p, Color))
				{
					Vertex.Color = FVector4(static_cast<float>(Color.mRed), static_cast<float>(Color.mGreen), static_cast<float>(Color.mBlue), static_cast<float>(Color.mAlpha));
				}

				Out.PolygonVertices.Add(Vertex);
			}

			// 삼각형의 머티리얼 : 요소 없음 -> -1, AllSame -> [0], ByPolygon -> [p]
			int32 GlobalSlot = -1;
			if (MaterialElement)
			{
				const int IndexInArray = MaterialElement->GetMappingMode() == FbxLayerElement::eAllSame ? 0 : p;
				const int LocalSlot = MaterialElement->GetIndexArray().GetAt(IndexInArray);
				if (LocalSlot >= 0 && LocalSlot < LocalToGlobal.Num())
				{
					GlobalSlot = LocalToGlobal[LocalSlot];
				}
			}
			Out.TriangleMaterials.Add(GlobalSlot);
		}
		return true;
	}


	//----------------Cook------------------

	// PrepareScene에서 Y-up 오른손으로 맞췄으니 OBJ와 같은 변환을 쓴다 (엔진: X 앞, Y 오른쪽, Z 위, 왼손)
	FVector ToEngine(const FVector& V)
	{
		return FVector(-V.Z, V.X, V.Y);
	}

	// 값이 전부 같아야 GPU 정점 하나를 같이 쓴다. 메시 번호를 넣어 서로 다른 메시의 정점이 섞이지 않게 한다.
	struct FFbxVertexKey
	{
		int32 MeshIndex = 0;
		int32 ControlPointIndex = 0;
		FVector Normal;
		FVector2 UV;
		FVector4 Color;

		bool operator==(const FFbxVertexKey& Other) const
		{
			return MeshIndex == Other.MeshIndex && ControlPointIndex == Other.ControlPointIndex
				&& Normal.X == Other.Normal.X && Normal.Y == Other.Normal.Y && Normal.Z == Other.Normal.Z
				&& UV.X == Other.UV.X && UV.Y == Other.UV.Y
				&& Color.X == Other.Color.X && Color.Y == Other.Color.Y && Color.Z == Other.Color.Z && Color.W == Other.Color.W;
		}
	};

	struct FFbxVertexKeyHash
	{
		size_t operator()(const FFbxVertexKey& Key) const
		{
			size_t Hash = std::hash<int32>{}(Key.MeshIndex);
			auto Combine = [&Hash](size_t Value) { Hash = Hash * 31 + Value; };
			Combine(std::hash<int32>{}(Key.ControlPointIndex));
			Combine(std::hash<float>{}(Key.Normal.X)); Combine(std::hash<float>{}(Key.Normal.Y)); Combine(std::hash<float>{}(Key.Normal.Z));
			Combine(std::hash<float>{}(Key.UV.X));     Combine(std::hash<float>{}(Key.UV.Y));
			Combine(std::hash<float>{}(Key.Color.X));  Combine(std::hash<float>{}(Key.Color.W));
			return Hash;
		}
	};
}



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
		Destroy(Manager);
		return false;
	}

	// Importer Initialize And Import
	if (!InitializeImporter(Manager, Importer, Path) || !ImportFbx(Importer, Scene))
	{
		Destroy(Manager);
		return false;
	}

	//Display Scene : 로그 찍는 함수들은 FbxDebugScene에 모아 두었다
	FFbxDebugScene::DisplayScene(Scene);

	//Texture Path : 상대 경로를 fbx 파일 폴더 기준으로 풀어야 해서 Path가 필요하다
	HTR_LOG(Info, "--------------------Texture Path--------------------");
	FFbxDebugScene::DisplayTexturePaths(Scene, Path);

	//Destroy
	Destroy(Manager);

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

	if (!ParseFbx(Path, Option, Info) || !CookStaticMesh(Info, *Data))
	{
		return nullptr;
	}

	//FStaticMeshBake::WriteBaked(BinPath, *Data);
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
	if (Path.size() == 0) return false;

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

bool FFbxImporter::ParseFbx(const FString& Path, const FFbxImportOptions& Option, FFbxInfo& Out)
{
	//축/단위 변환, 삼각화, 노드 순회, Global * Geometric 적용, 정점별 N/UV/Color 읽기, 머티리얼 값 읽기, 텍스처 경로 찾기

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

	//SDK 준비, Import
	if (!BeginFbxImport(Path, Manager, Importer, Ios, Scene))
	{
		Destroy(Manager);
		return false;
	}

	//Scene 검사, 축 변환, 단위 변환, Triangulate
	if (!PrepareScene(Manager, Scene, Option))
	{
		Destroy(Manager);
		return false;
	}

	//Scene에서 추출
	const bool Result = ExtractScene(Scene, Path, Option, Out);

	Destroy(Manager);

	return Result;
}

bool FFbxImporter::CookSkeletalMesh(const FFbxInfo& Raw, FSkeletalMeshData& Out)
{
	return false;
}

bool FFbxImporter::CookStaticMesh(const FFbxInfo& Raw, FStaticMeshData& Out)
{
	//ToEngine 축변환, 감기 순서 뒤집기, UV V 뒤집기, 중복 정점 제거, 섹션 만들기, 탄젠트, AABB, Validate
	//CookStaticMesh에서 TriangleMaterials에 - 1이 하나라도 있으면 Default 슬롯을 하나 추가하고(Name = "Default", 흰색),
	// -1을 그 슬롯 번호로 바꾼다.지금처럼 "범위 밖이면 0"으로 처리하면 같은 문제가 생겨요.


	FStaticMeshData Cooked;

	// 머티리얼 슬롯 (없으면 Default 하나. Validate가 섹션 없는 메시를 통과시키지 않는다)
	for (const FFbxMaterialInfo& RawMaterial : Raw.Materials)
	{
		FStaticMaterialSlot Slot;
		Slot.Name = RawMaterial.Name;
		Slot.BaseColor = FLinearColor(RawMaterial.Diffuse.X, RawMaterial.Diffuse.Y, RawMaterial.Diffuse.Z, RawMaterial.Opacity);
		Slot.SpecularColor = FLinearColor(RawMaterial.Specular.X, RawMaterial.Specular.Y, RawMaterial.Specular.Z, 1.0f);
		Slot.Shininess = RawMaterial.Shininess;
		Slot.DiffuseTexturePath = RawMaterial.DiffuseTexture;
		Slot.NormalTexturePath = RawMaterial.NormalTexture;
		Cooked.MaterialSlots.Add(std::move(Slot));
	}
	if (Cooked.MaterialSlots.IsEmpty())
	{
		FStaticMaterialSlot DefaultSlot;
		DefaultSlot.Name = "Default";
		Cooked.MaterialSlots.Add(DefaultSlot);
	}
	const int32 SlotCount = Cooked.MaterialSlots.Num();

	// 정점(중복 제거) + 슬롯별 인덱스
	TArray<TArray<uint32>> SlotIndices;
	SlotIndices.SetNum(SlotCount);

	TMap<FFbxVertexKey, uint32, FFbxVertexKeyHash> Cache;

	for (int32 MeshIndex = 0; MeshIndex < Raw.Meshes.Num(); MeshIndex++)
	{
		const FFbxMeshInfo& Mesh = Raw.Meshes[MeshIndex];
		const int32 TriangleCount = Mesh.PolygonVertices.Num() / 3;

		for (int32 Tri = 0; Tri < TriangleCount; Tri++)
		{
			const FFbxPolygonVertex* Corners = &Mesh.PolygonVertices[Tri * 3];

			// 노멀이 없는 파일은 면 노멀을 쓴다 (OBJ와 같음). FBX 공간에서 구하고 아래에서 같이 ToEngine.
			FVector FaceNormal;
			if (!Mesh.bHasNormals)
			{
				const FVector& A = Mesh.ControlPoints[Corners[0].ControlPointIndex];
				const FVector& B = Mesh.ControlPoints[Corners[1].ControlPointIndex];
				const FVector& C = Mesh.ControlPoints[Corners[2].ControlPointIndex];
				FaceNormal = (B - A).Cross(C - A).GetSafeNormal();   // 오른손, 반시계 = 앞
			}

			uint32 Indices[3];
			for (int32 c = 0; c < 3; c++)
			{
				const FFbxPolygonVertex& Corner = Corners[c];

				FFbxVertexKey Key;
				Key.MeshIndex = MeshIndex;
				Key.ControlPointIndex = Corner.ControlPointIndex;
				Key.Normal = Mesh.bHasNormals ? Corner.Normal : FaceNormal;
				Key.UV = Corner.UV;
				Key.Color = Corner.Color;

				if (const uint32* Found = Cache.Find(Key))
				{
					Indices[c] = *Found;
					continue;
				}

				FVertexPNCT Vertex;
				Vertex.Position = ToEngine(Mesh.ControlPoints[Corner.ControlPointIndex]);
				Vertex.Normal = ToEngine(Key.Normal);
				Vertex.UV = FVector2(Corner.UV.X, 1.0f - Corner.UV.Y);   // FBX는 아래가 원점, DX는 위가 원점
				Vertex.Color = Corner.Color;

				const uint32 NewIndex = static_cast<uint32>(Cooked.Vertices.Num());
				Cache.Add(Key, NewIndex);
				Cooked.Vertices.Add(Vertex);
				Indices[c] = NewIndex;
			}

			int32 Slot = Mesh.TriangleMaterials[Tri];
			if (Slot < 0 || Slot >= SlotCount) Slot = 0;
			TArray<uint32>& Bucket = SlotIndices[Slot];

			// OBJ와 같이 (0, 2, 1)로 뒤집는다. 거울 스케일이면 이미 한 번 뒤집혀 있으니 원래 순서로.
			// 화면에서 안쪽 면만 보이면 이 조건 하나만 반대로 바꿔 보면 된다.
			const bool bReverse = !Mesh.bFlipWinding;
			Bucket.Add(Indices[0]);
			Bucket.Add(bReverse ? Indices[2] : Indices[1]);
			Bucket.Add(bReverse ? Indices[1] : Indices[2]);
		}
	}

	// 섹션 : 슬롯 순서대로 이어 붙인다 
	for (int32 Slot = 0; Slot < SlotCount; Slot++)
	{
		const TArray<uint32>& Bucket = SlotIndices[Slot];
		if (Bucket.IsEmpty()) continue;

		FStaticMeshSection Section;
		Section.StartIndex = static_cast<uint32>(Cooked.Indices.Num());
		Section.IndexCount = static_cast<uint32>(Bucket.Num());
		Section.MaterialSlotIndex = static_cast<uint32>(Slot);
		Cooked.Indices.Append(Bucket);
		Cooked.Sections.Add(Section);
	}

	// 탄젠트, AABB, 검증 (OBJ Cook 끝부분과 같음)
	Cooked.CalculateTangents(Cooked.Vertices, Cooked.Indices);

	if (!Cooked.Vertices.IsEmpty())
	{
		FVector Min = Cooked.Vertices[0].Position;
		FVector Max = Min;
		for (const FVertexPNCT& Vertex : Cooked.Vertices)
		{
			Min.X = std::min(Min.X, Vertex.Position.X); 
			Max.X = std::max(Max.X, Vertex.Position.X);
			Min.Y = std::min(Min.Y, Vertex.Position.Y); 
			Max.Y = std::max(Max.Y, Vertex.Position.Y);
			Min.Z = std::min(Min.Z, Vertex.Position.Z); 
			Max.Z = std::max(Max.Z, Vertex.Position.Z);
		}
		Cooked.AABB.Min = Min;
		Cooked.AABB.Max = Max;
	}

	FString Error;
	if (!Cooked.Validate(Error))
	{
		HTR_LOG(Error, "[FBX] {}: {}", Raw.Path, Error);
		return false;
	}

	HTR_LOG(Info, "[FBX] {}: vertices {}, indices {}, sections {}", Raw.Path,
			Cooked.Vertices.Num(), Cooked.Indices.Num(), Cooked.Sections.Num());
	Out = std::move(Cooked);
	return true;
}

void FFbxImporter::Destroy(FbxManager*& Manager)
{
	if (Manager)
	{
		HTR_LOG(Info, "Destroy Fbx Manager");
		Manager->Destroy();
		Manager = nullptr;
	}
}

bool FFbxImporter::BeginFbxImport(const FString& Path, FbxManager*& Manager, FbxImporter*& Importer, FbxIOSettings*& Ios, FbxScene*& Scene)
{
	//Manager Create
	//FbxIOSettings Create : 무엇을 읽을지 고르는 옵션(머티리얼, 애니메이션 등)
	//Importer Create : 파일을 열고(Initialize) Scene을 채운다. 작업을 마치고 Destroy해도 된다.
	//Scene Create : fbx를 읽은 결과를 담는 그래프(노드 트리, 축, 단위, fps 등 전역 설정, 포즈, AnimStack이 모두 들어간다)
	if (!CreateManager(Manager) || !CreateIOSetting(Manager, Ios) || !CreateImporter(Manager, Importer) || !CreateScene(Manager, Scene))
	{
		Destroy(Manager);
		return false;
	}

	// Importer Initialize And Import
	if (!InitializeImporter(Manager, Importer, Path) || !ImportFbx(Importer, Scene))
	{
		Destroy(Manager);
		return false;
	}

	return true;
}

bool FFbxImporter::PrepareScene(fbxsdk::FbxManager* Manager, fbxsdk::FbxScene* Scene, const FFbxImportOptions& Option)
{
	if (Manager == nullptr || Scene == nullptr) return false;

	// Check the scene integrity!
	FbxStatus Status;
	FbxArray<FbxString*> Details;
	FbxSceneCheckUtility SceneCheck(Scene, &Status, &Details);

	auto LogAndClearDetails = [](FbxArray<FbxString*>& Details, ELogVerbosity Verbosity)
		{
			for (int i = 0; i < Details.GetCount(); i++)
			{
				FLog::Emit(Verbosity, std::format("[FBX]    {}", Details[i]->Buffer()));
			}
			FbxArrayDelete(Details);
		};

	//순환 검사
	if (!SceneCheck.Validate(FbxSceneCheckUtility::eCheckCycles))
	{
		HTR_LOG(Error, "[FBX] Scene has cycles");
		LogAndClearDetails(Details, ELogVerbosity::Error);
		return false;
	}
	//혹시 남아있는 문제 메세지 비우기
	LogAndClearDetails(Details, ELogVerbosity::Warning);

	//깨진 데이터 체크
	if (!SceneCheck.Validate(FbxSceneCheckUtility::eCkeckData))
	{
		HTR_LOG(Warning, "[FBX] Scene integrity check reported problems");
	}

	LogAndClearDetails(Details, ELogVerbosity::Warning);


	FbxGlobalSettings& GlobalSetting = Scene->GetGlobalSettings();

	//좌표계
	//OBJ와 같은 Y-up 오른손으로 맞추기.
	//	ToEngine이라는 과정을 통해 왼손 z-up으로 바꾸게 되는데 그냥 여기서 바로 바꿔도 되지 않을까? -> 나중에 체크해보기
	// Claude : 축 / 단위 ConvertScene은 정점이 아니라 노드 변환만 바꿔요.
	//		그래서 다음 단계에서 노드 행렬을 곱하지 않으면 변환이 사라져요.
	if (Option.bConvertScene && GlobalSetting.GetAxisSystem() != FbxAxisSystem::MayaYUp)
	{
		FbxAxisSystem::MayaYUp.ConvertScene(Scene);
	}

	//엔진 단위를 cm로 하자(안됨)
	//	엔진이 이미 m 단위여서 일단 m 단위로 하고 나중에 cm 단위로 바꾸고 싶다.
	if (GlobalSetting.GetSystemUnit() != FbxSystemUnit::m)
	{
		FbxSystemUnit::m.ConvertScene(Scene);
	}

	//다각형을 삼각형으로 만들기
	//메시 노드 수집보다 반드시 먼저 와야한다.
	//	Attribute를 갈아 끼우기 때문
	FbxGeometryConverter lGeomConverter(Manager);
	try
	{
		// 삼각형 변환에 실패한 메시가 있다면 false를 반환한다
		if (!lGeomConverter.Triangulate(Scene, true))
		{
			HTR_LOG(Warning, "[FBX] Triangulate() return false");
		}
	}
	catch (std::runtime_error)
	{
		HTR_LOG(Error, "Scene integrity verification failed.");
		return false;
	}

	return true;
}

bool FFbxImporter::ExtractScene(fbxsdk::FbxScene* Scene, const FString& Path, const FFbxImportOptions& Option, FFbxInfo& Out)
{
	Out.Path = Path;

	//eMesh 노드 모으기
	TArray<FbxNode*> MeshNodes;
	CollectMeshNodes(Scene->GetRootNode(), MeshNodes);

	if (MeshNodes.IsEmpty())
	{
		HTR_LOG(Warning, "[FBX] {} : no mesh node", Path);
		return false;
	}

	//머티리얼
	TMap<FbxSurfaceMaterial*, int32> MaterialMap;
	for (FbxNode* Node : MeshNodes)
	{
		const TArray<int32> LocalToGlobal = ParseNodeMaterials(Node, Path, MaterialMap, Out.Materials);

		FFbxMeshInfo MeshInfo;
		if (ExtractMesh(Node, LocalToGlobal, Option, MeshInfo))
		{
			HTR_LOG(Info, "[FBX] {}: mesh \"{}\" cp {}, triangles {}", Path, MeshInfo.Name,
					MeshInfo.ControlPoints.Num(), MeshInfo.PolygonVertices.Num() / 3);
			Out.Meshes.Add(std::move(MeshInfo));
		}
	}

	HTR_LOG(Info, "[FBX] {}: meshes {}, materials {}", Path, Out.Meshes.Num(), Out.Materials.Num());

	return !Out.Meshes.IsEmpty();
}
