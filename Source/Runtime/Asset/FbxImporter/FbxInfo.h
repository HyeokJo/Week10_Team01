#pragma once

#include "Containers/Array.h"
#include "Core/EngineString.h"
#include "Core/Types.h"
#include "Math/Vector.h"
#include "Math/Vector2.h"
#include "Math/Matrix.h"

struct FFbxImportOptions
{
	bool bImportStaticMesh = true;
	bool bImportSkeletalMesh = false;
	bool bImportMaterials = true;
	bool bImportTextures = true;
	bool bConvertScene = true;  // 축 변환
	// FBX의 정면(Y-up 오른손 기준 +Z)을 엔진 어디로 보낼지. true: +X (액터 Forward와 일치), false: +Y (언리얼 기본과 같음)
	bool bForceFrontXAxis = true;
	float ImportUniformScale = 1.0f;
	bool bCombineMeshes = true;  // 노드 여러 개 → 1개
	
	//생각 나는 추가 옵션들
	//중복 버텍스 제거(같은 위치)

};

// 삼각형 꼭짓점 하나
struct FFbxPolygonVertex
{
	// 메시 안 Cp 번호 (나중에 스킨 가중치 찾기)
	int32 ControlPointIndex = 0;

	// 변환까지 끝난 값
	FVector Normal = FVector(0.f, 0.f, 0.f);

	// 아직 V를 뒤집기 전
	FVector2 UV = FVector2(0.f, 0.f);

	// 없으면 (1,1,1,1)
	FVector4 Color = FVector4(1.f, 1.f, 1.f, 1.f);

};

struct FFbxSkinInfluence
{
	int32 BoneIndex = -1;
	float Weight = 0.f;
};

struct FFbxMeshInfo
{
	FString Name;
	TArray<FVector> ControlPoints;     // Global * Geometric 적용 후
	TArray<FFbxPolygonVertex> PolygonVertices;   // 3개씩 = 삼각형
	TArray<int32> TriangleMaterials; // 삼각형별 "전역" 슬롯 번호
	bool bHasNormals = true;
	bool bHasUVs = true;
	bool bFlipWinding = false; // 행렬식 < 0
	bool bHasSkin = false;
	// ControlPoints와 같은 개수. ControlPoint마다 (뼈 번호, 가중치) 목록. 스켈레탈로 읽을 때만 채운다
	TArray<TArray<FFbxSkinInfluence>> ControlPointInfluences;
};

// Cook이 만든 GPU 정점 하나가 어느 메시의 어느 ControlPoint에서 왔는지.
// 스켈레탈 Cook이 이 표로 ControlPointInfluences를 정점 순서로 옮긴다
struct FFbxVertexSource
{
	int32 MeshIndex = 0;
	int32 ControlPointIndex = 0;
};

struct FFbxMaterialInfo
{
	FString Name;
	// Lambert가 아닌 머티리얼은 값을 못 읽는다. 0이면 검고 투명(AlphaBlend)하게 나오므로 흰색·불투명이 기본
	FVector Diffuse = FVector(1.f, 1.f, 1.f);
	float Opacity = 1.f;
	FVector Specular = FVector(0.f, 0.f, 0.f);
	float Shininess = 0.f;
	FString DiffuseTexture;
	FString NormalTexture;        // 저장소 루트 기준
};

// 뼈 하나. Global Space (ConvertScene 단위 변환 후, ToEngine 전)
struct FFbxBoneInfo
{
	FString Name;
	
	// Bones 배열 안의 번호. 루트는 -1
	int32 ParentIndex = -1;
	
	// 바인드 때 Global (FbxAMatrix를 그대로 복사한 배치)
	FMatrix GlobalBindPose = FMatrix::Identity;

	// TransformLinkMatrix에서 왔는가 (아니면 BindPose/Evaluate)
	bool bFromCluster = false;
};

struct FFbxInfo
{
	FString Path; // 저장소 루트 기준
	bool bForceFrontXAxis = true;   // Cook의 ToEngine이 읽는다 (FFbxImportOptions에서 복사)
	TArray<FFbxMeshInfo> Meshes;
	TArray<FFbxMaterialInfo> Materials;
	// 부모가 항상 자식보다 앞
	TArray<FFbxBoneInfo> Bones;
};

