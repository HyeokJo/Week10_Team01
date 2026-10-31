#include "EnginePCH.h"
#include "FakeSpotLightActor.h"
#include "Core/ResourcePaths.h"
#include "Asset/AssetManager.h"
#include "Render/Material.h"
#include "Render/ColorWorkflow.h"

AFakeSpotLightActor::AFakeSpotLightActor()
{
    DecalComponent = CreateDefaultSubobject<UDecalComponent>("UDecalComponent");
    DecalComponent->SetupAttachment(GetRootComponent());

    BillboardComponent = CreateDefaultSubobject<UBillboardComponent>("UBillboardComponent");
    BillboardComponent->SetupAttachment(GetRootComponent());
    BillboardComponent->SetHiddenInDetails(true);
    BillboardComponent->SetTexture(ResourcePaths::SpotLightIcon);

    UMaterial* DefaultMaterial = UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial");
    UTexture2D* MaskTexture =  UAssetManager::Get().LoadTexture(ResourcePaths::FakeSpotLightMask, true, false);

    if (DefaultMaterial && MaskTexture)
    {
        UMaterial* Material = UMaterial::CreateInstance(DefaultMaterial);

        if (Material)
        {
            if (Material->Textures.Num() > 0)
            {
                Material->Textures[0] = MaskTexture;
            }
            else
            {
                Material->Textures.Add(MaskTexture);
            }
            Material->BaseColor = FLinearColor::White;
            Material->SamplerState = ESamplerState::LinearClamp;
            DecalComponent->SetDecalMaterial(Material);
        }
    }
    else
    {
        HTR_LOG(Error, "[FakeSpotLight] Default material or mask texture not found.");
    }

    UpdateFakeSpotLight();
}

void AFakeSpotLightActor::PostInitializeComponents()
{
    Super::PostInitializeComponents();
    UpdateFakeSpotLight();
}

void AFakeSpotLightActor::PostEditChangeProperty(const FProperty& Property)
{
    Super::PostEditChangeProperty(Property);
    UpdateFakeSpotLight();
}

void AFakeSpotLightActor::UpdateFakeSpotLight()
{
    if (!DecalComponent)
    {
        return;
    }

    Range = FMath::Max(Range, 1.0f);
    Radius = FMath::Max(Radius, 1.0f);
    Intensity = FMath::Clamp(Intensity, 0.0f, 1.0f);

    const float HalfRange = Range * 0.5f;

    DecalComponent->SetRelativeLocation(FVector(HalfRange, 0.0f, 0.0f));

    DecalComponent->SetDecalSize(FVector(HalfRange, Radius, Radius));

    DecalComponent->SetOpacity(Intensity);

    // 투영 끝부분에서 부드럽게 사라지도록 설정한다.
    DecalComponent->SetProjectionEndFade(0.15f);

    // Light Color를 데칼 머티리얼에 적용한다.
    UMaterial* Material = DecalComponent->GetDecalMaterial();

    if (!Material || !Material->bIsInstance)
    {
        UMaterial* Source = Material;

        if (!Source)
        {
            Source = UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial");
        }

        if (Source)
        {
            Material = UMaterial::CreateInstance(Source);
            DecalComponent->SetDecalMaterial(Material);
        }
    }

    if (Material)
    {
        Material->BaseColor = ColorWorkflow::ToShaderColor(FLinearColor(LightColor));
    }
}
