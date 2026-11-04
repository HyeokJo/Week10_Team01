#pragma once

#include <fstream>

#include "Core/Types.h"
#include "Core/EngineString.h"

struct FStaticMeshData;

// Cook이 끝난 FStaticMeshData를 .bin으로 저장하고 읽는다.
class FStaticMeshBake
{
public:
	static TUniquePtr<FStaticMeshData> ReadBaked(const FString& BinPath);
	static void WriteBaked(const FString& BinPath, const FStaticMeshData& Data);

	// 파일 헤더를 뺀 본문만 읽고 쓴다. .hasset도 같은 본문을 쓴다
	static void WriteBody(std::ofstream& Out, const FStaticMeshData& Data);
	static bool ReadBody(std::ifstream& In, FStaticMeshData& Data, FString& OutError);
};
