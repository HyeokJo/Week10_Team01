#include "EnginePCH.h"
#include "DecalActor.h"
#include "Core/ResourcePaths.h"

ADecalActor::ADecalActor()
{
	DecalComponent = CreateDefaultSubobject<UDecalComponent>("UDecalComponent");
	DecalComponent->SetupAttachment(GetRootComponent());

	BillboardComponent = CreateDefaultSubobject<UBillboardComponent>("UBillboardComponent");
	BillboardComponent->SetupAttachment(DecalComponent);
	BillboardComponent->SetHiddenInDetails(true);
	BillboardComponent->SetTexture(ResourcePaths::DecalIcon);
}
