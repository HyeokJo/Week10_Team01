#include "EnginePCH.h"
#include "DecalActor.h"

ADecalActor::ADecalActor()
{
	DecalComponent = CreateDefaultSubobject<UDecalComponent>("DecalComponent");

	DecalComponent->SetupAttachment(GetRootComponent());
}