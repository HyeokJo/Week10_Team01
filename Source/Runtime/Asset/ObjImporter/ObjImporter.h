#pragma once

#include "ObjInfo.h"
#include "Containers/Array.h"
#include "Core/Types.h"
#include "Core/EngineString.h"

struct FStaticMeshData;

class FObjImporter
{
public:
	static TUniquePtr<FStaticMeshData> LoadStaticMeshData(const FString& Path);
	// .bin을 읽거나 쓰지 않고 파싱만 한다. 원본이 프로젝트 밖일 때 그 폴더에 파일을 남기지 않는다
	static TUniquePtr<FStaticMeshData> ImportStaticMeshData(const FString& Path);

	// 디버그용 출력
	static void PrintObjInfo(const FObjInfo& ObjInfo);
	static void PrintSMD(const FStaticMeshData& SMD);

private:
	// .obj 파일 경로 -> FObjInfo 변환
	static bool ParseObj(const FString& Path, FObjInfo& Out);
	// FObjInfo -> FStaticMeshData 변환
	static bool Cook(const FObjInfo& Raw, FStaticMeshData& Out);
};
