#pragma once

#include "GameFramework/Actor.h"
#include "Component/SkeletalMeshComponent.h"


class ASkeletalMeshActor : public AActor
{
	DECLARE_CLASS(ASkeletalMeshActor, AActor)
public:
	ASkeletalMeshActor();
	virtual ~ASkeletalMeshActor() = default;

	USkeletalMeshComponent* GetSkeletalMeshComponent() const { return SkeletalMeshComponent; }

private:
	USkeletalMeshComponent* SkeletalMeshComponent = nullptr;
};
