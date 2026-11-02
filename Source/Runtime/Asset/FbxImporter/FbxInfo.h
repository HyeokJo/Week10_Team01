#pragma once

#include "Containers/Array.h"
#include "Core/EngineString.h"
#include "Core/Types.h"
#include "Math/Vector.h"
#include "Math/Vector2.h"

struct FFbxImportOptions
{
	bool bImportStaticMesh = true;
	bool bImportSkeletalMesh = false;
	bool bImportMaterials = true;
	bool bImportTextures = true;
	bool bConvertScene = true;  // 축 변환
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

struct FFbxMeshInfo
{
	FString Name;
	TArray<FVector> ControlPoints;     // Global * Geometric 적용 후
	TArray<FFbxPolygonVertex> PolygonVertices;   // 3개씩 = 삼각형
	TArray<int32> TriangleMaterials; // 삼각형별 "전역" 슬롯 번호
	bool bHasNormals = true;
	bool bHasUVs = true;
	bool bFlipWinding = false; // 행렬식 < 0

};

struct FFbxMaterialInfo
{
	FString Name; 
	FVector Diffuse = FVector(0.f, 0.f, 0.f); 
	float Opacity = 0.f; 
	FVector Specular = FVector(0.f, 0.f, 0.f);
	float Shininess = 0.f;
	FString DiffuseTexture;
	FString NormalTexture;        // 저장소 루트 기준
};

struct FFbxInfo
{
	FString Path; // 저장소 루트 기준
	TArray<FFbxMeshInfo> Meshes;
	TArray<FFbxMaterialInfo> Materials;
};