#pragma once

#include <fstream>
#include <type_traits>

#include "Containers/Array.h"
#include "Core/EngineString.h"
#include "Render/StaticMeshData.h"

// 메시 Bake 파일이 함께 쓰는 바이너리 읽기·쓰기 도구
namespace BakeStream
{
	// 숫자 || 숫자만 있는 구조체
	template<typename T>
	void WritePod(std::ofstream& Out, const T& Value)
	{
		static_assert(std::is_trivially_copyable_v<T>, "invalid plane old data");
		Out.write(reinterpret_cast<const char*>(&Value), sizeof(T));
	}

	template<typename T>
	bool ReadPod(std::ifstream& In, T& Value)
	{
		static_assert(std::is_trivially_copyable_v<T>);
		In.read(reinterpret_cast<char*>(&Value), sizeof(T));
		return static_cast<bool>(In);
	}

	// 배열: 개수 + 원소 전체를 한 번에
	template<typename T>
	void WriteArray(std::ofstream& Out, const TArray<T>& Array)
	{
		static_assert(std::is_trivially_copyable_v<T>);
		const uint32 Count = static_cast<uint32>(Array.Num());
		WritePod(Out, Count);
		if (Count > 0)
		{
			Out.write(reinterpret_cast<const char*>(Array.GetData()), sizeof(T) * Count);
		}
	}

	template<typename T>
	bool ReadArray(std::ifstream& In, TArray<T>& Array)
	{
		static_assert(std::is_trivially_copyable_v<T>);
		uint32 Count = 0;
		if (!ReadPod(In, Count))
		{
			return false;
		}

		// 깨진 파일의 엉터리 개수로 큰 메모리를 잡지 않도록, 남은 파일 크기보다 크면 바로 실패한다
		const std::streamoff Position = In.tellg();
		In.seekg(0, std::ios::end);
		const std::streamoff Remaining = static_cast<std::streamoff>(In.tellg()) - Position;
		In.seekg(Position);
		if (static_cast<uint64>(Count) * sizeof(T) > static_cast<uint64>(Remaining))
		{
			return false;
		}

		Array.SetNum(static_cast<int32>(Count));
		if (Count > 0)
		{
			In.read(reinterpret_cast<char*>(Array.GetData()), sizeof(T) * Count);
		}
		return static_cast<bool>(In);
	}

	// 길이 + 문자열
	inline void WriteString(std::ofstream& Out, const FString& Str)
	{
		const uint32 Length = static_cast<uint32>(Str.size());
		WritePod(Out, Length);
		Out.write(Str.data(), Length);
	}

	inline bool ReadString(std::ifstream& In, FString& Str)
	{
		uint32 Length = 0;
		if (!ReadPod(In, Length) || Length > 4096)
		{
			return false;
		}
		Str.resize(Length);
		In.read(Str.data(), Length);
		return static_cast<bool>(In);
	}

	inline void WriteMaterialSlots(std::ofstream& Out, const TArray<FStaticMaterialSlot>& Slots)
	{
		WritePod<uint32>(Out, static_cast<uint32>(Slots.Num()));
		for (const FStaticMaterialSlot& Slot : Slots)
		{
			WriteString(Out, Slot.Name);
			WritePod<FLinearColor>(Out, Slot.BaseColor);
			WriteString(Out, Slot.DiffuseTexturePath);
			WriteString(Out, Slot.NormalTexturePath);
			WritePod<float>(Out, Slot.Shininess);
			WritePod<FLinearColor>(Out, Slot.SpecularColor);
		}
	}

	inline bool ReadMaterialSlots(std::ifstream& In, TArray<FStaticMaterialSlot>& Slots)
	{
		uint32 Count = 0;
		if (!ReadPod<uint32>(In, Count))
		{
			return false;
		}

		for (uint32 i = 0; i < Count; ++i)
		{
			FStaticMaterialSlot Slot;
			if (!ReadString(In, Slot.Name) ||
				!ReadPod<FLinearColor>(In, Slot.BaseColor) ||
				!ReadString(In, Slot.DiffuseTexturePath) ||
				!ReadString(In, Slot.NormalTexturePath) ||
				!ReadPod<float>(In, Slot.Shininess) ||
				!ReadPod<FLinearColor>(In, Slot.SpecularColor))
			{
				return false;
			}
			Slots.Add(std::move(Slot));
		}
		return true;
	}
}
