#pragma once

#include "GameFramework/Actor.h"
#include "Component/DecalComponent.h"
#include "Component/BillboardComponent.h"

class ADecalActor : public AActor
{
	DECLARE_CLASS(ADecalActor, AActor)
	
public:
	ADecalActor();
	~ADecalActor() override = default;
	UDecalComponent* GetDecalComponent() const { return DecalComponent; }
	using Super::Serialize;
	void Serialize(FStructuredArchive::FRecord Record) override;

private:
	UDecalComponent* DecalComponent = nullptr;
	UBillboardComponent* BillboardComponent = nullptr;
};
