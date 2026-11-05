#include "EnginePCH.h"
#include "PrimitiveSceneProxy.h"
#include "Component/PrimitiveComponent.h"
#include "Component/SkinnedMeshComponent.h"
#include "Render/MeshRenderResource.h"
#include "Render/Renderer.h"
#include "Engine/Scene.h"

FPrimitiveSceneProxy::~FPrimitiveSceneProxy()
{
    if (Mesh) Mesh->UnregisterProxy(this);
}

void FPrimitiveSceneProxy::OnMeshRenderDataChanged()
{
    if (Scene)
    {
        Scene->MarkRenderStateDirty(this);
        Scene->MarkDirty(this);
    }
}

void FPrimitiveSceneProxy::OnMeshBoundsChanged()
{
    if (Scene)
    {
        Scene->MarkDirty(this);
    }
}

void FPrimitiveSceneProxy::OnMeshDestroyed()
{
    Mesh = nullptr;
    RenderState.reset();
}

void FPrimitiveSceneProxy::UpdateTransform()
{
    LocalToWorld = Component->GetWorldMatrix();
    WorldToLocal = LocalToWorld.Inverse();
    NormalMatrix = MakeNormalMatrix(LocalToWorld);
    const FBox LocalBounds = Mesh ? Mesh->GetRenderBounds() : Component->CalcLocalBounds();
    Bounds = MakeWorldBounds(LocalBounds.GetWorldAABB(LocalToWorld));
    LODSphere = Mesh ? Mesh->GetLocalLODSphere().Transform(LocalToWorld) : FLODSphere{};
}

void FPrimitiveSceneProxy::UpdateRenderState()
{
    FMeshRenderResource* PreviousMesh = Mesh;
    Mesh = nullptr;
    RenderState.reset();
    bVisible = Component->IsVisible();
    // 렌더·그림자·오클루전·피킹이 모두 이 값을 보므로 ShowFlag를 여기서 한 번만 반영한다
    if (Scene && !Scene->ShouldShowSkeletalMeshes() && Cast<USkinnedMeshComponent>(Component))
    {
        bVisible = false;
    }
    bCastShadow = Component->IsCastShadow();
    bReceiveShadow = Component->IsReceiveShadow();
	bReceiveDecal = Component->IsReceiveDecal();
	bHiddenInGame = Component->IsHiddenInGame();

    // 컴포넌트 종류를 묻지 않는다. 그릴 자원이 있으면 받고, 없으면(빌보드·텍스트) 느린 경로로 간다
    Mesh = Component->GetRenderResource();
    if (Mesh != PreviousMesh)
    {
        if (PreviousMesh) PreviousMesh->UnregisterProxy(this);
        if (Mesh) Mesh->RegisterProxy(this);
        if (Scene) Scene->MarkDirty(this);
    }
    if (!Mesh) return;

    // 슬롯마다 실제로 쓸 머티리얼 (덮어쓰기가 있으면 그것, 없으면 메시 기본값)
    const uint32 SlotCount = Mesh->GetNumMaterialSlots();
    std::vector<UMaterial*> Materials;
    Materials.reserve(SlotCount);
    for (uint32 Slot = 0; Slot < SlotCount; ++Slot)
        Materials.push_back(Component->GetMaterial(static_cast<int32>(Slot)));
    RenderState = Mesh->GetRenderState(Materials);
}
