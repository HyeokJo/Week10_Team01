#pragma once

#include "Asset/FbxImporter/FbxInfo.h"
#include "Core/EngineString.h"
#include "Core/Types.h"
#include "Render/SkeletalMeshData.h"
#include "Render/StaticMeshData.h"

enum class EMeshAssetType : uint32
{
	Static = 1,
	Skeletal = 2,
};

// 임포트 결과를 담는 파일(.hasset)에 함께 저장되는 정보
struct FMeshAssetInfo
{
	EMeshAssetType Type = EMeshAssetType::Static;
	// 이 결과를 만든 옵션. 리임포트 창의 기본값이 된다
	FFbxImportOptions Options;
	// 리임포트할 때 원본을 찾는 후보. 저장소 안의 원본은 상대 경로가, 같은 PC의 외부 파일은 절대 경로가 맞는다
	// 로드에는 쓰지 않으므로 둘 다 없어도 된다
	FString SourceRelativePath;
	FString SourceAbsolutePath;
	// 원본 파일 내용의 해시. 수정 시각과 달리 git pull로 바뀌지 않는다. 0이면 모른다 (버전 1 파일)
	uint64 SourceHash = 0;
};

struct FMeshAssetContents
{
	FMeshAssetInfo Info;
	TUniquePtr<FStaticMeshData> StaticMesh;      // Type == Static일 때
	TUniquePtr<FSkeletalMeshData> SkeletalMesh;  // Type == Skeletal일 때
};

// 메시 에셋 파일. 원본 FBX·OBJ 없이 이 파일만으로 로드된다
// 이름은 "원본 이름 + .hasset"이고(humanoid.fbx.hasset), 에셋 키는 .hasset을 뺀 값이다.
// 그래서 씬과 코드에 저장된 키("Models/Humanoid/humanoid.fbx")를 그대로 쓸 수 있다
class FMeshAssetFile
{
public:
	static constexpr const char* Extension = ".hasset";

	static FString MakeAssetPath(const FString& SourcePath) { return SourcePath + Extension; }
	static FString StripExtension(const FString& AssetPath);

	static bool Exists(const FString& AssetPath);
	// 없거나 형식이 다르거나 내용이 깨졌으면 false다
	static bool Read(const FString& AssetPath, FMeshAssetContents& OutContents);
	// 메시 데이터는 건너뛰고 헤더 정보만 읽는다
	static bool ReadInfo(const FString& AssetPath, FMeshAssetInfo& OutInfo);

	// 원본을 찾는다. 저장소 안의 경로(상대)를 먼저 보고, 없으면 같은 PC의 외부 경로(절대)를 본다. 없으면 빈 문자열이다
	static FString FindSource(const FMeshAssetInfo& Info);
	// 파일을 못 읽으면 0이다
	static uint64 ComputeFileHash(const FString& Path);

	// 쓰다가 실패해도 기존 파일이 망가지지 않도록 임시 파일에 쓴 뒤 교체한다
	static bool Write(const FString& AssetPath, const FString& SourcePath, const FFbxImportOptions& Options, const FStaticMeshData& Data);
	static bool Write(const FString& AssetPath, const FString& SourcePath, const FFbxImportOptions& Options, const FSkeletalMeshData& Data);
};
