#include "EnginePCH.h"
#include "StaticMeshBake.h"

#include <cstring>

#include "Asset/BakeStream.h"
#include "Render/StaticMeshData.h"
#include "Core/EngineLog.h"

static_assert(sizeof(FVertexPNCT) == 64);

// 바이트 그대로 읽고 쓰는 타입. 복사 생성자를 직접 작성하면 여기서 컴파일 오류가 난다
static_assert(std::is_trivially_copyable_v<FVertexPNCT>);
static_assert(std::is_trivially_copyable_v<FStaticMeshSection>);
static_assert(std::is_trivially_copyable_v<FVector4>);
static_assert(std::is_trivially_copyable_v<FBox>);

static_assert(std::is_trivially_copyable_v<FLODSourceVertex>);

using namespace BakeStream;

namespace
{
	// FStaticMeshData, FVertexPNCT, FStaticMeshSection, FStaticMaterialSlot 구조가 바뀌면 올린다
	constexpr uint32 BakeVersion = 8;

	struct FBakeHeader
	{
		// 'S'taic 'M'esh 'B'a'K'e
		char Magic[4] = { 'S', 'M', 'B', 'K' };
		uint32 Version = BakeVersion;
	};
}

void FStaticMeshBake::WriteBody(std::ofstream& Out, const FStaticMeshData& Data)
{
	WriteArray<FVertexPNCT>(Out, Data.Vertices);
	WriteArray<FLODSourceVertex>(Out, Data.LODSourceVertices);
	WriteArray<uint32>(Out, Data.Indices);
	WriteArray<FStaticMeshSection>(Out, Data.Sections);
	WriteMaterialSlots(Out, Data.MaterialSlots);
	WritePod<FBox>(Out, Data.AABB);
}

bool FStaticMeshBake::ReadBody(std::ifstream& In, FStaticMeshData& Data, FString& OutError)
{
	if (!ReadArray<FVertexPNCT>(In, Data.Vertices) ||
		!ReadArray<FLODSourceVertex>(In, Data.LODSourceVertices) ||
		!ReadArray<uint32>(In, Data.Indices) ||
		!ReadArray<FStaticMeshSection>(In, Data.Sections) ||
		!ReadMaterialSlots(In, Data.MaterialSlots) ||
		!ReadPod<FBox>(In, Data.AABB))
	{
		OutError = "truncated data";
		return false;
	}

	// 바이트 수는 맞아도 내용이 깨졌을 수 있으므로 GPU에 올리기 전에 검사
	return Data.Validate(OutError);
}

TUniquePtr<FStaticMeshData> FStaticMeshBake::ReadBaked(const FString& BinPath)
{
	std::ifstream In(BinPath, std::ios::binary);
	if (!In)
	{
		return nullptr;   // .bin이 없음 (처음 로드)
	}

	// 우리 형식이 맞는지, 같은 구조로 저장했는지 확인
	FBakeHeader Header;
	const FBakeHeader Expected;
	if (!ReadPod<FBakeHeader>(In, Header) ||
		std::memcmp(Header.Magic, Expected.Magic, sizeof(Header.Magic)) != 0 ||
		Header.Version != Expected.Version)
	{
		HTR_LOG(Warning, "[BAKE] {}: invalid header or version, re-cooking", BinPath);
		return nullptr;
	}

	TUniquePtr<FStaticMeshData> Data = MakeUnique<FStaticMeshData>();
	FString Error;
	if (!ReadBody(In, *Data, Error))
	{
		HTR_LOG(Warning, "[BAKE] {}: {}, re-cooking", BinPath, Error);
		return nullptr;
	}

	HTR_LOG(Info, "[BAKE] {}: loaded", BinPath);
	return Data;
}

void FStaticMeshBake::WriteBaked(const FString& BinPath, const FStaticMeshData& Data)
{
	std::ofstream Out(BinPath, std::ios::binary);
	if (!Out)
	{
		HTR_LOG(Warning, "[BAKE] {}: cannot open for writing", BinPath);
		return;
	}

	WritePod<FBakeHeader>(Out, FBakeHeader());
	WriteBody(Out, Data);

	// 스트림은 한 번 실패하면 상태가 유지되므로 마지막에 한 번만 확인
	if (!Out)
	{
		HTR_LOG(Warning, "[BAKE] {}: write failed", BinPath);
	}
}
