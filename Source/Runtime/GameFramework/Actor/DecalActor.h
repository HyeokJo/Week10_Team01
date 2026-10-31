#pragma once

#include "GameFramework/Actor.h"
#include "Component/DecalComponent.h"
#include "Component/BillboardComponent.h"

class ADecalActor : public AActor
{
	DECLARE_CLASS(ADecalActor, AActor)
public:
	ADecalActor();
	virtual ~ADecalActor() override = default;
	UDecalComponent* GetDecalComponent() const { return DecalComponent; }
private:
	UDecalComponent* DecalComponent = nullptr;
	UBillboardComponent* BillboardComponent = nullptr;
};