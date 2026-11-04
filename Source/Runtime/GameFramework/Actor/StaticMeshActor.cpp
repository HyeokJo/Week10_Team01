#include "EnginePCH.h"
#include "StaticMeshActor.h"

#include "Component/PrimitiveComponent.h"
#include "Asset/AssetManager.h"

AStaticMeshActor::AStaticMeshActor()
{
	StaticMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>("UStaticMeshComponent");
	SetRootComponent(StaticMeshComponent);
	//StaticMeshComponent->SetupAttachment(GetRootComponent());
}

void AStaticMeshActor::SetPrimitiveType(EPrimitiveType Type)
{

}


void AStaticMeshActor::BeginPlay()
{
	Super::BeginPlay();
}
