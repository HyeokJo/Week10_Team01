#pragma once

#include <fstream>

#include "Core/Types.h"
#include "Core/EngineString.h"

struct FSkeletalMeshData;

// Cook이 끝난 FSkeletalMeshData의 본문을 읽고 쓴다. 파일 헤더는 FMeshAssetFile이 맡는다
class FSkeletalMeshBake
{
public:
	static void WriteBody(std::ofstream& Out, const FSkeletalMeshData& Data);
	// NameToIndex는 저장하지 않고 뼈 이름으로 다시 만든다
	static bool ReadBody(std::ifstream& In, FSkeletalMeshData& Data, FString& OutError);
};
