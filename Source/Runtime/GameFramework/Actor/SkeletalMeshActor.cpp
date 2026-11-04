#include "EnginePCH.h"
#include "SkeletalMeshActor.h"

ASkeletalMeshActor::ASkeletalMeshActor()
{
	// 라이트 액터들과 같은 방식으로 스켈레탈 메시 컴포넌트를 루트로 둔다.
	// 액터를 옮기면 메시가 바로 따라가고, 뼈의 Component Space가 곧 액터 기준이 된다. 메시는 디테일 패널에서 고른다
	SkeletalMeshComponent = CreateDefaultSubobject<USkeletalMeshComponent>("USkeletalMeshComponent");
	SetRootComponent(SkeletalMeshComponent);
}
