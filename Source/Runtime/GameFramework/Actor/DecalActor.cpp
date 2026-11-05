#include "EnginePCH.h"
#include "DecalActor.h"
#include "Core/ResourcePaths.h"

ADecalActor::ADecalActor()
{
	DecalComponent = CreateDefaultSubobject<UDecalComponent>("DecalComponent");

	DecalComponent->SetupAttachment(GetRootComponent());
	BillboardComponent = CreateDefaultSubobject<UBillboardComponent>("UBillboardComponent");
	BillboardComponent->SetupAttachment(DecalComponent);
	BillboardComponent->SetHiddenInDetails(true);
	BillboardComponent->SetTexture(ResourcePaths::DecalIcon);
}

void ADecalActor::Serialize(FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record);
	if (Record.GetUnderlyingArchive().IsLoading())
	{
		BillboardComponent = Cast<UBillboardComponent>(FindComponentByName(FName("UBillboardComponent")));
		// The SSD branch used a different default-subobject name.
		if (UDecalComponent* Loaded = Cast<UDecalComponent>(FindComponentByName(FName("UDecalComponent"))))
		{
			if (Loaded != DecalComponent)
			{
				if (BillboardComponent) BillboardComponent->SetupAttachment(Loaded);
				// Binary duplication may already have removed the constructor's component.
				if (GetComponents().Find(DecalComponent) != INDEX_NONE)
					DestroyComponent(DecalComponent);
				DecalComponent = Loaded;
			}
		}
	}
}
