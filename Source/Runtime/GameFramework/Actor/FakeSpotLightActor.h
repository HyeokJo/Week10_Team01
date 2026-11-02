#pragma once

#include "GameFramework/Actor.h"
#include "Component/DecalComponent.h"
#include "Component/BillboardComponent.h"

class AFakeSpotLightActor final : public AActor
{
	DECLARE_CLASS(AFakeSpotLightActor, AActor)
	REFLECT_START(AFakeSpotLightActor)
		PROPERTY(LightColor)
		PROPERTY(Intensity)
		PROPERTY(Range)
		PROPERTY(Radius)
	REFLECT_END()
public:
	AFakeSpotLightActor();
	virtual ~AFakeSpotLightActor() override = default;

	virtual void PostInitializeComponents() override;
	virtual void PostEditChangeProperty(const FProperty& Property) override;

	UDecalComponent* GetDecalComponent() const { return DecalComponent; }
private:
	void UpdateFakeSpotLight();
	UDecalComponent* DecalComponent = nullptr;
	UBillboardComponent* BillboardComponent = nullptr;

	FColor LightColor = FColor(255, 255, 255);
	float Intensity = 0.7f;
	float Range = 500.0f;
	float Radius = 120.0f;
};