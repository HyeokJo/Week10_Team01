#include "EnginePCH.h"
#include "MeshAssetFile.h"

#include <cstring>
#include <filesystem>

#include "Asset/BakeStream.h"
#include "Asset/FbxImporter/SkeletalMeshBake.h"
#include "Asset/ObjImporter/StaticMeshBake.h"
#include "Core/EngineLog.h"

namespace fs = std::filesystem;
using namespace BakeStream;

namespace
{
	// 파일 구조나 Cook 결과가 달라지면 올린다 (구조체가 그대로여도 축 변환 같은 Cook 코드를 고치면 올려야 한다)
	// 읽을 수 있는 범위 밖의 파일은 원본에서 다시 임포트해야 한다
	// 2: 원본 해시 추가
	constexpr uint32 AssetVersion = 2;
	constexpr uint32 MinReadableAssetVersion = 1;

	static_assert(std::is_trivially_copyable_v<FFbxImportOptions>);

	struct FAssetHeader
	{
		char Magic[4] = { 'H', 'A', 'S', 'T' };
		uint32 Version = AssetVersion;
		EMeshAssetType Type = EMeshAssetType::Static;
	};

	// 메시 데이터 앞까지 읽는다. 끝나면 스트림은 메시 데이터 위치에 있다
	bool ReadHeader(std::ifstream& In, const FString& AssetPath, FMeshAssetInfo& OutInfo)
	{
		FAssetHeader Header;
		const FAssetHeader Expected;
		if (!ReadPod(In, Header) || std::memcmp(Header.Magic, Expected.Magic, sizeof(Header.Magic)) != 0)
		{
			HTR_LOG(Warning, "[ASSET] {}: not a mesh asset", AssetPath);
			return false;
		}
		if (Header.Version < MinReadableAssetVersion || Header.Version > AssetVersion)
		{
			HTR_LOG(Warning, "[ASSET] {}: version {} is not supported (expected {}~{}). Import the source again", AssetPath, Header.Version, MinReadableAssetVersion, AssetVersion);
			return false;
		}

		OutInfo.Type = Header.Type;
		OutInfo.SourceHash = 0;
		if (!ReadPod(In, OutInfo.Options) ||
			!ReadString(In, OutInfo.SourceRelativePath) ||
			!ReadString(In, OutInfo.SourceAbsolutePath) ||
			(Header.Version >= 2 && !ReadPod(In, OutInfo.SourceHash)))
		{
			HTR_LOG(Warning, "[ASSET] {}: truncated header", AssetPath);
			return false;
		}
		return true;
	}

	template<typename TData, typename TWriteBody>
	bool WriteAsset(const FString& AssetPath, const FString& SourcePath, const FFbxImportOptions& Options, const EMeshAssetType Type, const TData& Data, TWriteBody WriteBody)
	{
		const FString TempPath = AssetPath + ".tmp";
		{
			std::ofstream Out(TempPath, std::ios::binary);
			if (!Out)
			{
				HTR_LOG(Warning, "[ASSET] {}: cannot open for writing", TempPath);
				return false;
			}

			FAssetHeader Header;
			Header.Type = Type;
			WritePod(Out, Header);
			WritePod(Out, Options);

			std::error_code Error;
			const fs::path Absolute = fs::absolute(SourcePath, Error);
			const FString AbsolutePath = Error ? FString() : Absolute.generic_string();
			const fs::path Relative = fs::relative(SourcePath, fs::current_path(), Error);
			const FString RelativePath = Error ? FString() : Relative.generic_string();
			WriteString(Out, RelativePath);
			WriteString(Out, AbsolutePath);
			WritePod(Out, FMeshAssetFile::ComputeFileHash(SourcePath));

			WriteBody(Out, Data);

			// 스트림은 한 번 실패하면 상태가 유지되므로 마지막에 한 번만 확인
			if (!Out)
			{
				HTR_LOG(Warning, "[ASSET] {}: write failed", TempPath);
				Out.close();
				fs::remove(TempPath, Error);
				return false;
			}
		}

		std::error_code Error;
		fs::rename(TempPath, AssetPath, Error);
		if (Error)
		{
			HTR_LOG(Warning, "[ASSET] {}: cannot replace ({})", AssetPath, Error.message());
			fs::remove(TempPath, Error);
			return false;
		}

		HTR_LOG(Info, "[ASSET] {}: saved", AssetPath);
		return true;
	}
}

FString FMeshAssetFile::StripExtension(const FString& AssetPath)
{
	const size_t Length = std::strlen(Extension);
	return AssetPath.size() > Length ? AssetPath.substr(0, AssetPath.size() - Length) : AssetPath;
}

bool FMeshAssetFile::Exists(const FString& AssetPath)
{
	std::error_code Error;
	return fs::is_regular_file(AssetPath, Error);
}

bool FMeshAssetFile::Read(const FString& AssetPath, FMeshAssetContents& OutContents)
{
	std::ifstream In(AssetPath, std::ios::binary);
	if (!In)
	{
		return false;
	}

	if (!ReadHeader(In, AssetPath, OutContents.Info))
	{
		return false;
	}

	FString Error;
	bool bRead = false;
	switch (OutContents.Info.Type)
	{
	case EMeshAssetType::Static:
		OutContents.StaticMesh = MakeUnique<FStaticMeshData>();
		bRead = FStaticMeshBake::ReadBody(In, *OutContents.StaticMesh, Error);
		break;
	case EMeshAssetType::Skeletal:
		OutContents.SkeletalMesh = MakeUnique<FSkeletalMeshData>();
		bRead = FSkeletalMeshBake::ReadBody(In, *OutContents.SkeletalMesh, Error);
		break;
	default:
		Error = std::format("unknown asset type {}", static_cast<uint32>(OutContents.Info.Type));
		break;
	}

	if (!bRead)
	{
		HTR_LOG(Warning, "[ASSET] {}: {}", AssetPath, Error);
		return false;
	}

	HTR_LOG(Info, "[ASSET] {}: loaded", AssetPath);
	return true;
}

bool FMeshAssetFile::ReadInfo(const FString& AssetPath, FMeshAssetInfo& OutInfo)
{
	std::ifstream In(AssetPath, std::ios::binary);
	return In && ReadHeader(In, AssetPath, OutInfo);
}

FString FMeshAssetFile::FindSource(const FMeshAssetInfo& Info)
{
	for (const FString* Candidate : { &Info.SourceRelativePath, &Info.SourceAbsolutePath })
	{
		if (!Candidate->empty() && Exists(*Candidate))
		{
			return *Candidate;
		}
	}
	return FString();
}

uint64 FMeshAssetFile::ComputeFileHash(const FString& Path)
{
	std::ifstream In(Path, std::ios::binary);
	if (!In)
	{
		return 0;
	}

	// FNV-1a. 같은 파일인지 알아보는 용도라 암호학적 해시는 필요 없다
	constexpr uint64 Prime = 1099511628211ull;
	uint64 Hash = 14695981039346656037ull;
	char Buffer[64 * 1024];
	while (In)
	{
		In.read(Buffer, sizeof(Buffer));
		const std::streamsize Count = In.gcount();
		for (std::streamsize i = 0; i < Count; ++i)
		{
			Hash = (Hash ^ static_cast<uint8>(Buffer[i])) * Prime;
		}
	}
	return Hash;
}

bool FMeshAssetFile::Write(const FString& AssetPath, const FString& SourcePath, const FFbxImportOptions& Options, const FStaticMeshData& Data)
{
	return WriteAsset(AssetPath, SourcePath, Options, EMeshAssetType::Static, Data, &FStaticMeshBake::WriteBody);
}

bool FMeshAssetFile::Write(const FString& AssetPath, const FString& SourcePath, const FFbxImportOptions& Options, const FSkeletalMeshData& Data)
{
	return WriteAsset(AssetPath, SourcePath, Options, EMeshAssetType::Skeletal, Data, &FSkeletalMeshBake::WriteBody);
}
