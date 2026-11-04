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

	// 노드에서 위로 올라가며 처음 만나는 뼈. 없으면 -1
	int32 FindParentBone(FbxNode* Node, const TMap<FbxNode*, int32>& NodeToBone)
	{
		for (FbxNode* Current = Node; Current != nullptr; Current = Current->GetParent())
		{
			if (const int32* Found = NodeToBone.Find(Current))
			{
				return *Found;
			}
		}
		return -1;
	}

	// Cluster는 "뼈 -> (ControlPoint, 가중치) 목록"이다. 이것을 "ControlPoint -> (뼈, 가중치) 목록"으로 뒤집는다.
	// OutMeshBind : 바인드 순간 메시 노드의 Global (Cluster::GetTransformMatrix). 정점을 바인드 포즈에 놓을 때 쓴다
	bool ExtractSkinWeights(FbxNode* Node, FbxMesh* Mesh, const TMap<FbxNode*, int32>& NodeToBone,
							FFbxMeshInfo& Out, FbxAMatrix& OutMeshBind)
	{
		const int ControlPointCount = Mesh->GetControlPointsCount();
		const int SkinCount = Mesh->GetDeformerCount(FbxDeformer::eSkin);
		if (SkinCount == 0) return false;

		Out.ControlPointInfluences.SetNum(ControlPointCount);

		bool bMeshBindFound = false;
		for (int s = 0; s < SkinCount; s++)
		{
			FbxSkin* Skin = static_cast<FbxSkin*>(Mesh->GetDeformer(s, FbxDeformer::eSkin));

			for (int c = 0; c < Skin->GetClusterCount(); c++)
			{
				FbxCluster* Cluster = Skin->GetCluster(c);
				FbxNode* Link = Cluster->GetLink();
				if (Link == nullptr) continue;

				const int32* BoneIndex = NodeToBone.Find(Link);
				if (BoneIndex == nullptr) continue;   // CollectClusterLinks가 Link를 전부 뼈로 넣었으니 오지 않는다

				//Additive인 link는 대응하지 않고 경고만 띄운다
				if (Cluster->GetLinkMode() == FbxCluster::eAdditive)
				{
					HTR_LOG(Warning, "[FBX] {}: cluster \"{}\" is additive, treated as normalize", Out.Name, Link->GetName());
				}

				// 한 Skin의 Cluster는 보통 같은 TransformMatrix를 가진다. 첫 값을 쓴다
				if (!bMeshBindFound)
				{
					Cluster->GetTransformMatrix(OutMeshBind);
					bMeshBindFound = true;
				}

				//클러스터가 영향을 주는 ControlPoint의 개수
				const int Count = Cluster->GetControlPointIndicesCount();
				const int* Indices = Cluster->GetControlPointIndices();
				const double* Weights = Cluster->GetControlPointWeights();
				for (int i = 0; i < Count; i++)
				{
					const int ControlPoint = Indices[i];
					const float Weight = static_cast<float>(Weights[i]);
					if (ControlPoint < 0 || ControlPoint >= ControlPointCount || Weight <= 0.0f) continue;

					Out.ControlPointInfluences[ControlPoint].Add({ *BoneIndex, Weight });
				}
			}
		}

		// 가중치가 하나도 없는 ControlPoint는 메시의 부모 뼈(없으면 0번)에 붙인다. 안 그러면 원점으로 빨려 들어간다
		// 정상적이라면 이게 없어야 한다. 경고가 뜬다면 리깅쪽 문제가 있다는 것이니 작업자에게 알려야 한다.
		const int32 FallbackBone = std::max(FindParentBone(Node, NodeToBone), 0);
		int32 Unweighted = 0;
		for (TArray<FFbxSkinInfluence>& List : Out.ControlPointInfluences)
		{
			if (List.IsEmpty())
			{
				List.Add({ FallbackBone, 1.0f });
				Unweighted++;
			}
		}
		if (Unweighted > 0)
		{
			HTR_LOG(Warning, "[FBX] {}: {} control points have no weight, bound to bone {}", Out.Name, Unweighted, FallbackBone);
		}

		Out.bHasSkin = bMeshBindFound;
		return bMeshBindFound;
	}

	// 스킨이 없는 메시(뼈에 붙은 무기, 헬멧 등)는 부모 뼈 하나를 가중치 1로 따라가게 한다
	void AttachToParentBone(FbxNode* Node, const TMap<FbxNode*, int32>& NodeToBone, FFbxMeshInfo& Out)
	{
		const int32 Bone = std::max(FindParentBone(Node, NodeToBone), 0);
		Out.ControlPointInfluences.SetNum(Out.ControlPoints.Num());
		for (TArray<FFbxSkinInfluence>& List : Out.ControlPointInfluences)
		{
			List.Add({ Bone, 1.0f });
		}
		HTR_LOG(Info, "[FBX] {}: no skin, attached to bone {}", Out.Name, Bone);
	}

	// 바인드 공간 -> 씬 공간 보정 행렬. Correction * MeshBind == MeshGlobal
	// Cluster의 행렬은 씬과 다른 공간일 수 있다. 예: 3ds Max에서 온 파일은 씬이 Y-up인데 바인드 행렬은 Z-up으로 남아 있고,
	// 그 차이를 MaxToFilmbox_Root 같은 루트 노드의 회전이 메운다. 첫 스킨 메시로 차이를 구해 메시와 뼈에 똑같이 곱한다.
	// 일반 파일은 단위 행렬에 가깝고, 메시와 뼈의 상대 위치는 바뀌지 않는다
	FbxAMatrix ComputeBindCorrection(const TArray<FbxNode*>& MeshNodes)
	{
		for (FbxNode* Node : MeshNodes)
		{
			FbxMesh* Mesh = Node->GetMesh();
			if (Mesh == nullptr) continue;

			const int SkinCount = Mesh->GetDeformerCount(FbxDeformer::eSkin);
			for (int s = 0; s < SkinCount; s++)
			{
				FbxSkin* Skin = static_cast<FbxSkin*>(Mesh->GetDeformer(s, FbxDeformer::eSkin));
				for (int c = 0; c < Skin->GetClusterCount(); c++)
				{
					FbxCluster* Cluster = Skin->GetCluster(c);
					if (Cluster->GetLink() == nullptr) continue;

					FbxAMatrix MeshBind;
					Cluster->GetTransformMatrix(MeshBind);
					return Node->EvaluateGlobalTransform() * MeshBind.Inverse();
				}
			}
		}
		return FbxAMatrix();   // 단위 행렬
	}

	//메시
	// NodeToBone이 비어 있지 않으면(스켈레탈) 스킨 가중치까지 읽는다
	bool ExtractMesh(FbxNode* Node, const TArray<int32>& LocalToGlobal, const FFbxImportOptions& Option,
					 const TMap<FbxNode*, int32>& NodeToBone, const FbxAMatrix& BindCorrection, FFbxMeshInfo& Out)
	{
		//ExtractMesh에서 머티리얼 요소가 없거나 로컬 번호가 범위 밖이면 GlobalSlot = -1을 넣는다.

		if (Node == nullptr) return false;

		Out.Name = Node->GetName();

		FbxMesh* Mesh = Node->GetMesh();
		if (Mesh == nullptr) return false;

		const bool bSkeletal = !NodeToBone.IsEmpty();

		// 노드 행렬 : Global(Pre-Rotation, 축/단위 변환 포함) * Geometric(이 노드의 메시에만)
		// 스킨 메시는 지금(0프레임) 위치가 아니라 바인드 순간의 Global을 쓴다.
		// 뼈의 바인드 행렬(TransformLinkMatrix)과 같은 순간이어야 스킨 행렬이 바인드 포즈에서 단위 행렬이 된다.
		// 뼈와 똑같이 BindCorrection을 곱해 씬 공간으로 옮긴다
		FbxAMatrix Global = Node->EvaluateGlobalTransform();
		if (bSkeletal)
		{
			FbxAMatrix MeshBind;
			if (ExtractSkinWeights(Node, Mesh, NodeToBone, Out, MeshBind))
			{
				Global = BindCorrection * MeshBind;
			}
		}

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

		if (bSkeletal && !Out.bHasSkin)
		{
			AttachToParentBone(Node, NodeToBone, Out);
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
				if (IndexInArray < MaterialElement->GetIndexArray().GetCount())
				{
					const int LocalSlot = MaterialElement->GetIndexArray().GetAt(IndexInArray);
					if (LocalSlot >= 0 && LocalSlot < LocalToGlobal.Num())
					{
						GlobalSlot = LocalToGlobal[LocalSlot];
					}
				}				
			}
			Out.TriangleMaterials.Add(GlobalSlot);
		}
		return true;
	}

	// FbxAMatrix*FbxMatrix -> FMatrix. 둘 다 [행][열] 배치에 이동이 3행에 있어서 값은 그대로 복사한다.
	// 의미는 같지만 곱셈 표기가 반대다: FBX의 A * B (열 벡터, B 먼저) == 엔진의 B * A (행 벡터, B 먼저)
	template <typename TFbxMatrix>
	FMatrix ToFMatrix(const TFbxMatrix& In)
	{
		FMatrix Out;
		for (int Row = 0; Row < 4; Row++)
		{
			for (int Col = 0; Col < 4; Col++)
			{
				Out.M[Row][Col] = static_cast<float>(In.Get(Row, Col));
			}
		}
		return Out;
	}

	// CollectMeshNodes와 같은 모양.
	void CollectSkeletonNodes(FbxNode* Node, TSet<FbxNode*>& OutNodes)
	{
		if (Node == nullptr) return;

		FbxNodeAttribute* Attribute = Node->GetNodeAttribute();
		if (Attribute && Attribute->GetAttributeType() == FbxNodeAttribute::eSkeleton)
		{
			OutNodes.Add(Node);
		}

		for (int i = 0; i < Node->GetChildCount(); i++)
		{
			CollectSkeletonNodes(Node->GetChild(i), OutNodes);
		}
	}

	// Link는 eSkeleton이 아닐 수 있어서 여기서 집합에 추가한다.
	void CollectClusterLinks(const TArray<FbxNode*>& MeshNodes, TSet<FbxNode*>& OutNodes,
							 TMap<FbxNode*, FbxAMatrix>& OutLinkBind)
	{
		for (FbxNode* MeshNode : MeshNodes)
		{
			FbxMesh* Mesh = MeshNode->GetMesh();
			if (Mesh == nullptr) continue;

			const int SkinCount = Mesh->GetDeformerCount(FbxDeformer::eSkin);
			for (int s = 0; s < SkinCount; s++)
			{
				FbxSkin* Skin = static_cast<FbxSkin*>(Mesh->GetDeformer(s, FbxDeformer::eSkin));

				for (int c = 0; c < Skin->GetClusterCount(); c++)
				{
					FbxCluster* Cluster = Skin->GetCluster(c);
					FbxNode* Link = Cluster->GetLink();
					if (Link == nullptr) continue;   // 끊어진 클러스터

					//Link를 eSkeleton 배열에 추가한다
					//Link 노드가 eNull, eMesh와 같은 걸 가르키게 된다면 앞서 eSkeleton만 구한것으로는 NodeToBone에 포함 못시킨다.
					// 이미 추가된 노드여도 TSet이라 한번만 들어간다.
					OutNodes.Add(Link);

					// 여러 메시(Body, Helmet)가 같은 뼈를 가리키면 처음 것만 쓴다. 보통 값이 같다
					if (OutLinkBind.Find(Link) == nullptr)
					{
						//GetTransformLinkMatrix
						//		바인드 순간 Link 노드의 Global
						//GetTransformMatrix
						//		바인드 순간 메시 노드의 Global
						//GetTransformAssociateModelMatrix
						//		Additive 모드에서만 쓰는 기준 모델의 행렬
						FbxAMatrix LinkBind;

						//바인드 순간의 Link 노드의 Global을 저장한다.
						Cluster->GetTransformLinkMatrix(LinkBind);
						OutLinkBind.Add(Link, LinkBind);
					}
				}
			}
		}
	}

	// FbxPose는 "노드 + 행렬" 목록이다
	//Bind Pose	
	//	스킨을 입힌 순간 노드들의 Global 행렬
	//Rest Pose	
	//	사용자가 저장해 둔 아무 자세. Maya에서 dagPose -save로 저장한 포즈, 캐릭터 기본 스탠스 등. Local일 수도 있다.
	void CollectBindPose(FbxScene* Scene, TMap<FbxNode*, FbxMatrix>& OutPose)
	{
		for (int i = 0; i < Scene->GetPoseCount(); i++)
		{
			FbxPose* Pose = Scene->GetPose(i);
			if (Pose == nullptr || !Pose->IsBindPose()) continue;

			for (int n = 0; n < Pose->GetCount(); n++)
			{
				if (Pose->IsLocalMatrix(n)) continue;   // 바인드 포즈는 보통 Global. Local이면 쓰지 않는다

				FbxNode* Node = Pose->GetNode(n);
				if (Node && OutPose.Find(Node) == nullptr)
				{
					// FbxMatrix (FbxAMatrix 아님)
					//FbxAMatrix
					//	Affine 행렬(이동 회전 스케일)
					//FbxMatrix
					//	일반 4x4 행렬
					//부모의 비균일 스케일 때문에 shear가 생긴 행렬도 있을 수 있어서, 무엇이든 담을 수 있는 일반 행렬을 쓴다.
					OutPose.Add(Node, Pose->GetMatrix(n));
				}
			}
		}
	}

	// 뼈가 아닌 노드는 ParentBone을 그대로 넘겨서 건너뛴다
	void BuildBoneOrder(FbxNode* Node, int32 ParentBone, const TSet<FbxNode*>& BoneNodes,
						TArray<FbxNode*>& OutOrder, TArray<int32>& OutParents, TMap<FbxNode*, int32>& OutNodeToBone)
	{
		if (Node == nullptr) return;

		int32 ThisBone = ParentBone;
		if (BoneNodes.Contains(Node))
		{
			ThisBone = OutOrder.Num();
			OutOrder.Add(Node);
			OutParents.Add(ParentBone);
			OutNodeToBone.Add(Node, ThisBone);
		}

		for (int i = 0; i < Node->GetChildCount(); i++)
		{
			BuildBoneOrder(Node->GetChild(i), ThisBone, BoneNodes, OutOrder, OutParents, OutNodeToBone);
		}
	}

	bool ExtractSkeleton(FbxScene* Scene, const TArray<FbxNode*>& MeshNodes, const FFbxImportOptions& Option,
						 const FbxAMatrix& BindCorrection, FFbxInfo& Out, TMap<FbxNode*, int32>& OutNodeToBone)
	{
		//eSkeleton 노드 모으기
		TSet<FbxNode*> BoneNodes;
		CollectSkeletonNodes(Scene->GetRootNode(), BoneNodes);

		//eSkeleton이 아닌 Link 노드를 BoneNodes에 포함, Link 노드의 Global 구하기.
		TMap<FbxNode*, FbxAMatrix> LinkBind;
		CollectClusterLinks(MeshNodes, BoneNodes, LinkBind);

		if (BoneNodes.IsEmpty())
		{
			HTR_LOG(Warning, "[FBX] {}: no bone", Out.Path);
			return false;
		}

		//모든 노드 중 바인드 포즈, Global인 행렬만 저장한다.
		//LinkBind는 Link에서 얻었기에 가중치에 영향을 주는 뼈만 Global을 구했다
		//PoseBind는 모든 포즈에서 구했기에 가중치 없는 뼈 등도 다 Global을 구했다.
		//우선순위는 Cluster -> BindPose -> Evaluate로 한다.
		//	Cluster
		//		정점 변형을 정의하는 값이라 가장 믿을만 하다
		//	BindPose
		//		Cluster가 없는 뼈의 바인드 값을 채우는 용도
		//	Evaluate
		//		둘 다 없을 때 0프레임의 부모 기준 값을 부모의 바인드로 붙이도록 한다.
		TMap<FbxNode*, FbxMatrix> PoseBind;
		CollectBindPose(Scene, PoseBind);

		// 본 순서, 부모, NodeToBone 채우기
		TArray<FbxNode*> Order;
		TArray<int32> Parents;
		BuildBoneOrder(Scene->GetRootNode(), -1, BoneNodes, Order, Parents, OutNodeToBone);

		// 메시의 Import Uniform Scale과 같은 값 (ExtractMesh의 Scale * M == 엔진 표기 M * Scale)
		FMatrix UniformScale = FMatrix::Identity;
		UniformScale.M[0][0] = UniformScale.M[1][1] = UniformScale.M[2][2] = Option.ImportUniformScale;

		int32 FromCluster = 0;
		int32 FromPose = 0;
		int32 FromEvaluate = 0;
		Out.Bones.Reserve(Order.Num());

		for (int32 i = 0; i < Order.Num(); i++)
		{
			FbxNode* Node = Order[i];

			FFbxBoneInfo Bone;
			Bone.Name = Node->GetName();
			Bone.ParentIndex = Parents[i];

			// 바인드 Global: Cluster -> BindPose -> Evaluate
			// Cluster·BindPose는 바인드 공간 값이라 씬 공간으로 옮긴다. FBX: Correction * Bind == 엔진: Bind × Correction
			if (const FbxAMatrix* Found = LinkBind.Find(Node))
			{
				const FbxAMatrix SceneBind = BindCorrection * (*Found);
				Bone.GlobalBindPose = ToFMatrix(SceneBind);
				Bone.bFromCluster = true;
				FromCluster++;

				// 진단: 바인드(Cluster)와 레퍼런스(Evaluate)의 위치 차이. T-포즈 파일이면 0에 가깝다
				const FbxVector4 Diff = SceneBind.GetT() - Node->EvaluateGlobalTransform().GetT();
				if (Diff.Length() > 1e-3)
				{
					HTR_LOG(Warning, "[FBX] bone \"{}\": bind vs reference differ by {:.4f}", Bone.Name, Diff.Length());
				}
			}
			//Cluster에서 못 찾았으면 = 가중치가 없는 뼈였다. 바인드 포즈에서 찾는다.
			else if (const FbxMatrix* FoundPose = PoseBind.Find(Node))
			{
				Bone.GlobalBindPose = ToFMatrix(*FoundPose) * ToFMatrix(BindCorrection);
				FromPose++;
			}
			else if (Bone.ParentIndex >= 0)
			{
				// 가중치 없는 끝 뼈 등. 0프레임 Global을 그대로 쓰면, 0프레임이 바인드 포즈와 다른 파일에서
				// 부모(바인드)와 어긋난다. 0프레임의 "부모 기준" 값만 가져와 부모의 바인드 Global에 붙인다.
				// FBX: Global = ParentGlobal * Local  ==  엔진: Local × ParentGlobal
				// 부모 값에 UniformScale이 이미 들어 있으므로 아래 스케일을 다시 곱하지 않는다
				// (부모 뼈와 사이에 뼈 아닌 노드가 끼어 있을 수 있어 EvaluateLocalTransform 대신 두 Global로 구한다)
				const FbxAMatrix LocalToParentBone =
					Order[Bone.ParentIndex]->EvaluateGlobalTransform().Inverse() * Node->EvaluateGlobalTransform();
				Bone.GlobalBindPose = ToFMatrix(LocalToParentBone) * Out.Bones[Bone.ParentIndex].GlobalBindPose;
				FromEvaluate++;
				Out.Bones.Add(Bone);
				continue;
			}
			else
			{
				Bone.GlobalBindPose = ToFMatrix(Node->EvaluateGlobalTransform());
				FromEvaluate++;
			}

			//스케일 키우기
			if (Option.ImportUniformScale != 1.0f)
			{
				Bone.GlobalBindPose = Bone.GlobalBindPose * UniformScale;
			}

			Out.Bones.Add(Bone);
		}

		HTR_LOG(Info, "[FBX] {}: bones {} (cluster {}, bindpose {}, evaluate {})",
				Out.Path, Out.Bones.Num(), FromCluster, FromPose, FromEvaluate);

		// 이름, 부모 확인용. 많으면 나중에 지운다
		for (int32 i = 0; i < Out.Bones.Num(); i++)
		{
			const FFbxBoneInfo& Bone = Out.Bones[i];
			const FMatrix& G = Bone.GlobalBindPose;
			HTR_LOG(Info, "[FBX]   [{}] {} parent {} pos ({:.3f}, {:.3f}, {:.3f}){}", i, Bone.Name, Bone.ParentIndex,
					G.M[3][0], G.M[3][1], G.M[3][2], Bone.bFromCluster ? "" : " *");
		}
		return true;
	}


	//----------------Cook------------------

	// PrepareScene에서 Y-up 오른손으로 맞췄다. FBX 캐릭터는 보통 +Z를 보고 왼쪽이 +X다 (엔진: X 앞, Y 오른쪽, Z 위, 왼손)
	// bForceFrontXAxis  true: 정면 +Z -> +X,  false: 정면 +Z -> +Y (언리얼과 같음). 둘 다 왼손으로 바뀌니 감기 처리는 같다
	FVector ToEngine(const FVector& V, bool bForceFrontXAxis)
	{
		return bForceFrontXAxis ? FVector(V.Z, -V.X, V.Y) : FVector(V.X, V.Z, V.Y);
	}

	// ToEngine의 행렬 버전. 정점과 뼈가 같은 변환을 받아야 겹친다
	// 행 벡터: p_e = p_f × C 이므로 M_e = Cᵀ × M_f × C
	FMatrix ToEngineMatrix(const FMatrix& FbxSpace, bool bForceFrontXAxis)
	{
		static const FMatrix FrontX(
			0.f, -1.f, 0.f, 0.f,
			0.f, 0.f, 1.f, 0.f,
			1.f, 0.f, 0.f, 0.f,
			0.f, 0.f, 0.f, 1.f);
		static const FMatrix FrontY(
			1.f, 0.f, 0.f, 0.f,
			0.f, 0.f, 1.f, 0.f,
			0.f, 1.f, 0.f, 0.f,
			0.f, 0.f, 0.f, 1.f);
		const FMatrix& C = bForceFrontXAxis ? FrontX : FrontY;

		return C.GetTransposed() * FbxSpace * C;
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

	FFbxImportOptions StaticOption = Option;
	StaticOption.bImportSkeletalMesh = false;   // 스태틱은 뼈·가중치를 읽지 않는다

	FFbxInfo Info;
	TUniquePtr<FStaticMeshData> Data = MakeUnique<FStaticMeshData>();

	if (!ParseFbx(Path, StaticOption, Info) || !CookStaticMesh(Info, *Data))
	{
		return nullptr;
	}

	//FStaticMeshBake::WriteBaked(BinPath, *Data);
	return Data;
}

TUniquePtr<FSkeletalMeshData> FFbxImporter::LoadSkeletalMeshData(const FString& Path, const FFbxImportOptions& Option)
{
	FFbxImportOptions SkeletalOption = Option;
	//SkeletalOption.bImportSkeletalMesh = true;   // ExtractScene이 뼈를 읽도록

	FFbxInfo Info;
	TUniquePtr<FSkeletalMeshData> Data = MakeUnique<FSkeletalMeshData>();

	if (!ParseFbx(Path, SkeletalOption, Info) || !CookSkeletalMesh(Info, *Data))
	{
		return nullptr;
	}

	return Data;
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

bool FFbxImporter::CookSkeleton(const FFbxInfo& Raw, FReferenceSkeleton& Out)
{
	Out.Bones.Reserve(Raw.Bones.Num());

	for (int32 i = 0; i < Raw.Bones.Num(); i++)
	{
		const FFbxBoneInfo& RawBone = Raw.Bones[i];

		FBoneInfo Bone;
		Bone.Name = RawBone.Name;
		Bone.ParentIndex = RawBone.ParentIndex;
		Bone.GlobalBindPose = ToEngineMatrix(RawBone.GlobalBindPose, Raw.bForceFrontXAxis);
		Bone.InverseBindPose = Bone.GlobalBindPose.Inverse();

		// SceneComponent와 같은 식: Global = Local * ParentGlobal  ->  Local = Global * ParentGlobal^{-1}
		// 부모는 항상 앞 번호라 이미 Out.Bones에 들어가 있다
		Bone.LocalBindPose = Bone.ParentIndex < 0
			? Bone.GlobalBindPose
			: Bone.GlobalBindPose * Out.Bones[Bone.ParentIndex].InverseBindPose;

		if (Out.NameToIndex.Find(Bone.Name))
		{
			HTR_LOG(Warning, "[FBX] {}: duplicate bone name \"{}\"", Raw.Path, Bone.Name);
		}
		else
		{
			Out.NameToIndex.Add(Bone.Name, i);
		}

		Out.Bones.Add(Bone);
	}

	return !Out.Bones.IsEmpty();
}

bool FFbxImporter::CookStaticMesh(const FFbxInfo& Raw, FStaticMeshData& Out, TArray<FFbxVertexSource>* OutSources)
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
	// 정점(중복 제거) + 슬롯별 인덱스
	TArray<TArray<uint32>> SlotIndices;
	SlotIndices.SetNum(Cooked.MaterialSlots.Num());

	// -1(머티리얼 없음)이나 범위 밖 번호를 쓰는 삼각형이 처음 나올 때 Default 슬롯을 "한 번만" 추가한다.
	// (루프 밖에 있어야 한다. 안에 두면 삼각형마다 슬롯이 하나씩 늘어난다)
	int32 DefaultSlot = -1;
	auto GetDefaultSlot = [&]()
		{
			if (DefaultSlot < 0)
			{
				FStaticMaterialSlot Slot;
				Slot.Name = "Default";
				DefaultSlot = static_cast<int32>(Cooked.MaterialSlots.Add(Slot));   // Add는 새 인덱스를 돌려준다
				(void)SlotIndices.AddDefaulted_GetRef();                             // 버킷도 같이 늘림
			}
			return DefaultSlot;
		};

	TMap<FFbxVertexKey, uint32, FFbxVertexKeyHash> Cache;
	if (OutSources)
	{
		OutSources->Reset();
	}

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
				Vertex.Position = ToEngine(Mesh.ControlPoints[Corner.ControlPointIndex], Raw.bForceFrontXAxis);
				Vertex.Normal = ToEngine(Key.Normal, Raw.bForceFrontXAxis);
				Vertex.UV = FVector2(Corner.UV.X, 1.0f - Corner.UV.Y);   // FBX는 아래가 원점, DX는 위가 원점
				Vertex.Color = Corner.Color;

				const uint32 NewIndex = static_cast<uint32>(Cooked.Vertices.Num());
				Cache.Add(Key, NewIndex);
				Cooked.Vertices.Add(Vertex);
				if (OutSources)
				{
					OutSources->Add({ MeshIndex, Corner.ControlPointIndex });   // Vertices와 같은 순서
				}
				Indices[c] = NewIndex;
			}

			int32 Slot = Mesh.TriangleMaterials[Tri];
			if (Slot < 0 || Slot >= Raw.Materials.Num())
			{
				Slot = GetDefaultSlot();
			}
			TArray<uint32>& Bucket = SlotIndices[Slot];

			// OBJ와 같이 (0, 2, 1)로 뒤집는다. 거울 스케일이면 이미 한 번 뒤집혀 있으니 원래 순서로.
			// 화면에서 안쪽 면만 보이면 이 조건 하나만 반대로 바꿔 보면 된다.
			const bool bReverse = !Mesh.bFlipWinding;
			Bucket.Add(Indices[0]);
			Bucket.Add(bReverse ? Indices[2] : Indices[1]);
			Bucket.Add(bReverse ? Indices[1] : Indices[2]);
		}
	}

	// 섹션 : 슬롯 순서대로 이어 붙인다. Default 슬롯이 나중에 붙었을 수 있으니 지금 개수로 돈다
	for (int32 Slot = 0; Slot < SlotIndices.Num(); Slot++)
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

bool FFbxImporter::CookSkeletalMesh(const FFbxInfo& Raw, FSkeletalMeshData& Out)
{
	if (!CookSkeleton(Raw, Out.Skeleton))
	{
		return false;
	}

	// 정점·인덱스·섹션·머티리얼은 스태틱과 똑같이 만든다. 정점마다 출처만 같이 받는다
	FStaticMeshData Static;
	TArray<FFbxVertexSource> Sources;
	if (!CookStaticMesh(Raw, Static, &Sources))
	{
		return false;
	}

	Out.Vertices = std::move(Static.Vertices);
	Out.Indices = std::move(Static.Indices);
	Out.Sections = std::move(Static.Sections);
	Out.MaterialSlots = std::move(Static.MaterialSlots);
	Out.AABB = Static.AABB;

	// 정점마다 뼈 영향: 가중치 큰 순으로 상위 4개만 남기고 합이 1이 되게 나눈다
	//	4개인 이유는 GPU 스키닝에서 뼈 번호 4개가 uint8 * 4 = 4바이트라서 딱 맞아서 그렇다
	//	실제 모양도 차이가 거의 없다. 대부분의 정점은 1 ~ 3개의 뼈의 영향만 받는다.
	//		손가락 사이 같은 곳은 아니긴하다
	//	지금 엔진은 CPU 스키닝이라 가변 개수도 처리할 수 있긴 하다
	const int32 BoneCount = Out.Skeleton.Num();
	Out.Influences.SetNum(Out.Vertices.Num());

	int32 Truncated = 0;
	for (int32 v = 0; v < Out.Vertices.Num(); v++)
	{
		const FFbxVertexSource& Source = Sources[v];
		const FFbxMeshInfo& Mesh = Raw.Meshes[Source.MeshIndex];

		TArray<FFbxSkinInfluence> List;
		if (Source.ControlPointIndex < Mesh.ControlPointInfluences.Num())
		{
			List = Mesh.ControlPointInfluences[Source.ControlPointIndex];
		}

		//Weight 큰 순서로
		std::sort(List.begin(), List.end(),
			[](const FFbxSkinInfluence& A, const FFbxSkinInfluence& B) { return A.Weight > B.Weight; });

		//영향을 준 뼈가 4개보다 많았다
		if (List.Num() > MAX_BONE_INFLUENCES)
		{
			Truncated++;
		}

		FBoneInfluence& Influence = Out.Influences[v];
		float Sum = 0.0f;
		const int32 Count = std::min(List.Num(), MAX_BONE_INFLUENCES);
		for (int32 i = 0; i < Count; i++)
		{
			Influence.BoneIndex[i] = std::clamp(List[i].BoneIndex, 0, BoneCount - 1);
			Influence.Weight[i] = List[i].Weight;
			Sum += List[i].Weight;
		}

		if (Sum > 0.0f)
		{
			for (int32 i = 0; i < Count; i++)
			{
				Influence.Weight[i] /= Sum;
			}
		}
		else
		{
			// 영향이 없는 정점(ExtractMesh가 막지만 혹시 몰라서): 0번 뼈를 그대로 따라간다
			Influence.BoneIndex[0] = 0;
			Influence.Weight[0] = 1.0f;
		}
	}

	if (Truncated > 0)
	{
		HTR_LOG(Info, "[FBX] {}: {} vertices had more than {} influences (kept top {})",
				Raw.Path, Truncated, MAX_BONE_INFLUENCES, MAX_BONE_INFLUENCES);
	}

	HTR_LOG(Info, "[FBX] {}: skeletal mesh bones {}, vertices {}", Raw.Path, BoneCount, Out.Vertices.Num());
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
	Out.bForceFrontXAxis = Option.bForceFrontXAxis;

	//eMesh 노드 모으기
	TArray<FbxNode*> MeshNodes;
	CollectMeshNodes(Scene->GetRootNode(), MeshNodes);

	if (MeshNodes.IsEmpty())
	{
		HTR_LOG(Warning, "[FBX] {} : no mesh node", Path);
		return false;
	}

	// 뼈는 메시 루프보다 먼저: ExtractMesh가 NodeToBone으로 Cluster -> 뼈 번호를 찾는다
	TMap<FbxNode*, int32> NodeToBone;
	FbxAMatrix BindCorrection;   // 스태틱은 단위 행렬 그대로
	if (Option.bImportSkeletalMesh)
	{
		// 스킨이 하나도 없으면 스켈레탈이 아니다 (뼈 노드만 섞인 스태틱 파일). 호출한 쪽이 스태틱으로 다시 읽는다
		bool bAnySkin = false;
		for (FbxNode* Node : MeshNodes)
		{
			if (Node->GetMesh() && Node->GetMesh()->GetDeformerCount(FbxDeformer::eSkin) > 0)
			{
				bAnySkin = true;
				break;
			}
		}
		if (!bAnySkin)
		{
			HTR_LOG(Info, "[FBX] {}: no skin, not a skeletal mesh", Path);

			//Skin이 없는데 BlendShape이 있는 경우를 조사함
			bool bAnyBlendShape = false;
			for (FbxNode* Node : MeshNodes)
			{
				if (Node->GetMesh() && Node->GetMesh()->GetDeformerCount(FbxDeformer::eBlendShape) > 0)
				{
					bAnyBlendShape = true;
					break;
				}
			}			

			//Skin이 없는데 BlendShape이 있다면 런타임에 메시 변형을 할 수 없는 StaticMesh로는 표현할 수 없다
			//대처할 수 없다고 경고를 남긴다.
			if (bAnyBlendShape)
			{
				HTR_LOG(Warning, "[FBX] {}: blend shapes ignored (morph targets not supported)", Path);
			}

			//스킨이 없는데 메시 노드의 조상 중 뼈가 있다면 뼈에 의해 움직이는 Skeletal Mesh일 수 있다.
			//아직 그런 경우에 대해 대응하지 않고 StaticMesh로 그릴 것이기에 경고를 남긴다
			bool bAnyParentsisBone = false;
			for (FbxNode* Node : MeshNodes)
			{
				//Node의 부모를 탐색하며 Attribute가 eSkeleton인지 찾는다.
				for (FbxNode* Current = Node->GetParent(); Current != nullptr; Current = Current->GetParent())
				{
					FbxNodeAttribute* Attribute = Current->GetNodeAttribute();
					if (Attribute && Attribute->GetAttributeType() == FbxNodeAttribute::eSkeleton)
					{
						bAnyParentsisBone = true;
						break;
					}
				}
				//찾아버려서 빠져나가기
				if (bAnyParentsisBone) break;
			}

			if (bAnyParentsisBone)
			{
				HTR_LOG(Warning, "[FBX] {}: mesh is parented to bone, but imported as static (rigid skeletal not supported)", Path);
			}

			return false;
		}

		BindCorrection = ComputeBindCorrection(MeshNodes);

		if (!ExtractSkeleton(Scene, MeshNodes, Option, BindCorrection, Out, NodeToBone))
		{
			return false;
		}
	}


	//머티리얼
	TMap<FbxSurfaceMaterial*, int32> MaterialMap;
	for (FbxNode* Node : MeshNodes)
	{
		const TArray<int32> LocalToGlobal = ParseNodeMaterials(Node, Path, MaterialMap, Out.Materials);

		FFbxMeshInfo MeshInfo;
		if (ExtractMesh(Node, LocalToGlobal, Option, NodeToBone, BindCorrection, MeshInfo))
		{
			HTR_LOG(Info, "[FBX] {}: mesh \"{}\" cp {}, triangles {}", Path, MeshInfo.Name,
					MeshInfo.ControlPoints.Num(), MeshInfo.PolygonVertices.Num() / 3);
			Out.Meshes.Add(std::move(MeshInfo));
		}
	}

	HTR_LOG(Info, "[FBX] {}: meshes {}, materials {}", Path, Out.Meshes.Num(), Out.Materials.Num());

	return !Out.Meshes.IsEmpty();
}
