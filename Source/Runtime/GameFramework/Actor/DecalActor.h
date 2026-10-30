#pragma once

#include "GameFramework/Actor.h"
#include "Component/DecalComponent.h"

class ADecalActor : public AActor
{
	DECLARE_CLASS(ADecalActor, AActor)
	
public:
	ADecalActor();
	~ADecalActor() override = default;
	UDecalComponent* GetDecalComponent() const { return DecalComponent; }

private:
	UDecalComponent* DecalComponent = nullptr;
};