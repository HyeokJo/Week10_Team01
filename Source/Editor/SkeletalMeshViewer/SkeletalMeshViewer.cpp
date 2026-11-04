#include "EnginePCH.h"
#include "Editor/SkeletalMeshViewer/SkeletalMeshViewer.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor/DirectionalLightActor.h"
#include "GameFramework/Actor/AmbientLightActor.h"

namespace
{
	const FRotator PreviewDirectionalLightRotation(46.0f, 0.0f, 0.0f);   // Pitch = Y축 (디테일 패널의 Y 칸)
	constexpr float PreviewAmbientIntensity = 0.2f;

	// 라이트 아이콘이 메시를 가리지 않게 한다
	void HideBillboards(const AActor& Actor)
	{
		for (UActorComponent* Component : Actor.GetComponents())
		{
			if (UBillboardComponent* Billboard = Cast<UBillboardComponent>(Component))
			{
				Billboard->SetVisible(false);
			}
		}
	}
}

ASkeletalMeshPreviewActor::ASkeletalMeshPreviewActor()
{
	MeshComponent = CreateDefaultSubobject<USkeletalMeshDebugComponent>("USkeletalMeshDebugComponent");
	SetRootComponent(MeshComponent);
}

FSkeletalMeshViewer::~FSkeletalMeshViewer()
{
	Close();
}

bool FSkeletalMeshViewer::Open(USkeletalMesh* Mesh)
{
	if (!Mesh)
	{
		return false;
	}

	if (!PreviewWorld)
	{
		CreatePreviewWorld();
	}

	MeshComponent->SetSkeletalMesh(Mesh);
	return true;
}

void FSkeletalMeshViewer::Close()
{
	if (!PreviewWorld)
	{
		return;
	}

	MeshComponent = nullptr;
	GEngine->DestroyWorldContext(PreviewWorld);
	PreviewWorld = nullptr;
}

void FSkeletalMeshViewer::Tick(const float DeltaTime)
{
	if (PreviewWorld)
	{
		PreviewWorld->Tick(DeltaTime);
	}
}

void FSkeletalMeshViewer::CreatePreviewWorld()
{
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::EditorPreview);
	Context.SetCurrentWorld(UWorld::CreateWorld(EWorldType::EditorPreview, true));
	PreviewWorld = Context.World();

	MeshComponent = PreviewWorld->SpawnActor<ASkeletalMeshPreviewActor>()->GetMeshComponent();

	ADirectionalLightActor* DirectionalLight = PreviewWorld->SpawnActor<ADirectionalLightActor>();
	DirectionalLight->SetActorRotation(PreviewDirectionalLightRotation);
	HideBillboards(*DirectionalLight);

	AAmbientLightActor* AmbientLight = PreviewWorld->SpawnActor<AAmbientLightActor>();
	AmbientLight->GetLightComponent()->SetIntensity(PreviewAmbientIntensity);
	HideBillboards(*AmbientLight);
}
