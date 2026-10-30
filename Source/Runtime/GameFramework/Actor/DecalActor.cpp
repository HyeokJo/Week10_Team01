#include "EnginePCH.h"
#include "DecalActor.h"

ADecalActor::ADecalActor()
{
	DecalComponent = CreateDefaultSubobject<UDecalComponent>("UDecalComponent");
	DecalComponent->SetupAttachment(GetRootComponent());
}
