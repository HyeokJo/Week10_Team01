#pragma once

#include "Core/Types.h"
#include "Core/EngineStatics.h"
#include "Core/NameTypes.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectGlobals.h"
#include "Serialization/Archive.h"
#include "Serialization/StructuredArchive.h"
#include "Serialization/StructuredArchiveSlots.h"

class UClass;
struct FProperty;
// Property Reflection

#define INVALID_OBJECT ((UObject*)-1)

#define REFLECT_START(ClassName) \
public: \
	inline static void RegisterProperties(UClass* InClass) \
	{

#define PROPERTY(PropertyName) \
    InClass->AddProperty<decltype(ThisClass::PropertyName)>(#PropertyName, offsetof(ThisClass, PropertyName));

#define PROPERTY_TYPE(PropertyName, PropertyType) \
    InClass->AddProperty<decltype(ThisClass::PropertyName)>(#PropertyName, offsetof(ThisClass, PropertyName), EPropertyType::##PropertyType);

// Flags: EPropertyFlags 조합 (예: CPF_VisibleAnywhere, CPF_EditAnywhere | CPF_Transient)
#define PROPERTY_FLAGS(PropertyName, Flags) \
    InClass->AddProperty<decltype(ThisClass::PropertyName)>(#PropertyName, offsetof(ThisClass, PropertyName), Flags);

#define PROPERTY_TYPE_FLAGS(PropertyName, PropertyType, Flags) \
    InClass->AddProperty<decltype(ThisClass::PropertyName)>(#PropertyName, offsetof(ThisClass, PropertyName), EPropertyType::##PropertyType, Flags);

#define REFLECT_END()\
	};\
private:

// 추상 클래스는 생성자를 등록하지 않는다.
// 템플릿이어야 버려진 if constexpr 분기의 new T()가 컴파일되지 않고,
// 멤버여야 private 생성자(싱글턴 등)에 접근할 수 있다.
#define DECLARE_CLASS(ClassName, SuperClassName)                        \
public:                                                                 \
    using Super = SuperClassName;                                       \
    using ThisClass = ClassName;		                                \
    template <typename T = ClassName>                                   \
	static UObject* InternalConstructInstance(void* Memory)				\
	{																	\
		if constexpr (std::is_abstract_v<T>) { return nullptr; }		\
		else { return new (Memory) T(); }								\
	}																	\
    static UClass* StaticClass()                                        \
    {                                                                   \
        static UClass c;                                                \
        static bool bIsInit = false;                                    \
        if (!bIsInit)                                                   \
        {                                                               \
            c.Name  = #ClassName;                                       \
            c.Super = Super::StaticClass();								\
            c.ClassSize = sizeof(ClassName);							\
			c.Constructor = std::is_abstract_v<ClassName> ? nullptr : &InternalConstructInstance<ClassName>;\
			if (&ClassName::RegisterProperties != &Super::RegisterProperties) \
			{															\
				ClassName::RegisterProperties(&c);						\
			}															\
			RegisterClass(&c);											\
            bIsInit = true;                                             \
        }                                                               \
        return &c;                                                      \
    }                                                                   \
    struct FAutoRegister {FAutoRegister() { ThisClass::StaticClass();}};\
    inline static FAutoRegister AutoRegister;							\
private:																

// 씬을 열 때 못 찾은 에셋의 키를 잃지 않기 위한 통로. 참조를 경로로 읽고 쓰는 JSON 포매터가 쓴다
// 키를 버리면 기본값(큐브 등)이 그대로 저장되어, 에셋을 다시 임포트해도 씬이 원래 메시로 돌아오지 못한다
class UObject;
namespace AssetReferenceSerialization
{
	void ReportMissing(const FString& AssetPath);
	const FString* GetPendingSavePath();
	// 에셋이 뒤늦게 생겼을 때(임포트) 그 키를 기다리던 프로퍼티에 연결한다
	void ResolveMissing(const FString& AssetPath, UObject* Asset);
}

class UObject
{
	friend UObject* StaticConstructObject_Internal(const struct FStaticConstructObjectParameters& Params);
public:
	UObject();
	UObject(bool bRegister);
	virtual ~UObject();

	static UClass* StaticClass();
	UClass* GetClass() const { return ClassPrivate; }

	template <typename T>
	bool IsA() { return IsA(T::StaticClass()); }
	bool IsA(const UClass* Class);

	uint32 GetUUID() const { return ObjectUUID; }
	void SetUUID(uint32 Uid) { ObjectUUID = Uid; }

	const FName& GetFName() const { return Name; }         // FName 비교용
	FString GetName() const { return Name.ToString(); }    // Name 출력용

	UObject* GetOuter() const { return Outer; }
	void SetOuter(UObject* InOuter) { Outer = InOuter; }

	void SetName(const FName& InName) { Name = FName(InName); }

	inline static void RegisterProperties(UClass* InClass) {};

	inline void SetFlags(EObjectFlags NewFlags) { Flags |= NewFlags; }
	inline void ClearFlags(EObjectFlags FlagsToClear) { Flags &= ~FlagsToClear; }
	inline bool HasAnyFlags(EObjectFlags FlagsToCheck) const { return HasFlag(Flags, FlagsToCheck); }
	inline bool HasAllFlags(EObjectFlags FlagsToCheck) const { return (Flags & FlagsToCheck) == FlagsToCheck; }
	inline EObjectFlags GetFlags() const { return Flags; }
	EObjectFlags GetMaskedFlags(EObjectFlags Mask = EObjectFlags::RF_AllFlags) const{return EObjectFlags(GetFlags() & Mask);}

	virtual void Serialize(json& Handle, bool bIsLoading);
	virtual void Serialize(FArchive& Ar);
	virtual void Serialize(FStructuredArchive::FRecord Record);

	virtual void PostEditChangeProperty(const FProperty& Property) {};

	void* operator new(uint64 Size)
	{
		void* Ptr = malloc(Size);
		if (!Ptr)
			throw std::bad_alloc();

		FEngineStatics::TotalAllocationBytes += static_cast<uint64>(Size);
		FEngineStatics::TotalAllocationCount += 1;
		return Ptr;
	}

	void operator delete(void* Ptr, uint64 Size)
	{
		FEngineStatics::TotalAllocationBytes -= static_cast<uint64>(Size);
		FEngineStatics::TotalAllocationCount -= 1;
		free(Ptr);
	}

	void* operator new(uint64, void* Ptr) { return Ptr; }
	void operator delete(void*, void*) {}

	template <typename T>
	T* CreateDefaultSubobject(FName SubobjectName)
	{
		T* Subobject = NewObject<T>(this, SubobjectName, EObjectFlags::RF_DefaultSubObject);
		OnDefaultSubobjectCreated(Subobject);
		return Subobject;
	}

	virtual void PostDuplicate(bool bDuplicateForPIE) {}

	virtual class UWorld* GetWorld() const { return nullptr; }

protected:
	virtual void OnDefaultSubobjectCreated(UObject* Subobject) {}
private:
	void SerializeScriptProperties(FStructuredArchive::FSlot Slot);

	uint32 ObjectUUID;
	uint32 InternalIndex;

	FName Name = FName("None");
	UObject* Outer = nullptr;
	UClass* ClassPrivate = nullptr;

	EObjectFlags Flags = EObjectFlags::RF_NoFlags;

	bool bIsRegistered = true;
};

extern TArray<UObject*> GUObjectArray;

inline bool IsValid(const UObject* Test)
{
	return Test != nullptr;
}