#include "EnginePCH.h"
#include "Editor/SkeletalMeshViewer/SkeletalMeshViewer.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor/DirectionalLightActor.h"
#include "GameFramework/Actor/AmbientLightActor.h"
#include "Input/InputSystem.h"

namespace
{
	// Pitch = Y축, Yaw = Z축 (디테일 패널의 Y·Z 칸). Z 180은 메시 정면(+X)을 비추기 위한 값이다
	const FRotator PreviewDirectionalLightRotation(46.0f, 180.0f, 0.0f);
	constexpr float PreviewAmbientIntensity = 0.2f;
	constexpr float MinBonePickPixels = 8.0f;   // 멀리서 점처럼 작아진 구체도 누를 수 있게 하는 최소 판정 반경
	constexpr float SameSpotPixels = 0.5f;      // 이보다 중심이 가까우면 같은 자리로 본다
	constexpr float ClickSlopPixels = 3.0f;     // 이만큼 움직이기 전까지는 드래그가 아니라 클릭이다
	constexpr uint32 DefaultViewWidth = 1280;
	constexpr uint32 DefaultViewHeight = 720;
	const FQuat LookTowardNegativeX(0.0f, 0.0f, 1.0f, 0.0f);   // 어댑터의 Front 프리셋과 같은 방향

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

	BoneProxy = CreateDefaultSubobject<USceneComponent>("BoneProxy");
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
	FrameMesh(*Mesh);
	return true;
}

void FSkeletalMeshViewer::Close()
{
	if (!PreviewWorld)
	{
		return;
	}

	// 기즈모가 곧 지워질 프록시를 가리키지 않게 한다
	Gizmo.EndDrag();
	Gizmo.ClearHover();
	Gizmo.SetTarget(nullptr);
	GizmoBone = -1;
	bClickCandidate = false;
	BoneProxy = nullptr;

	MeshComponent = nullptr;
	GEngine->DestroyWorldContext(PreviewWorld);
	PreviewWorld = nullptr;
	Targets.Release();
}

void FSkeletalMeshViewer::Tick(const float DeltaTime)
{
	if (PreviewWorld)
	{
		PreviewWorld->Tick(DeltaTime);
	}
}

FTransform FSkeletalMeshViewer::DecomposeMatrix(const FMatrix& Matrix)
{
	// 행 벡터 규약이라 1~3행이 크기가 곱해진 X/Y/Z 축이고 4행이 위치다
	FVector Axes[3];
	FVector Scale;
	for (int32 i = 0; i < 3; i++)
	{
		const FVector Axis(Matrix.M[i][0], Matrix.M[i][1], Matrix.M[i][2]);
		Scale.V[i] = Axis.Length();
		Axes[i] = Axis.GetSafeNormal();
	}

	// 거울 변환이면 축이 왼손계가 되어 회전으로 표현할 수 없다. X축을 뒤집고 음수 크기로 옮긴다
	if (Matrix.Determinant() < 0.0f)
	{
		Scale.X = -Scale.X;
		Axes[0] = -Axes[0];
	}

	FMatrix Rotation = FMatrix::Identity;
	for (int32 i = 0; i < 3; i++)
	{
		Rotation.SetAxis(i, Axes[i]);
	}

	return FTransform(FVector(Matrix.M[3][0], Matrix.M[3][1], Matrix.M[3][2]), Scale, MatrixToRotator(Rotation));
}

void FSkeletalMeshViewer::UpdateBoneGizmo(const FVector2& LocalMouse, const bool bViewHovered)
{
	if (!MeshComponent)
		return;

	const int32 Selected = MeshComponent->GetSelectedBone();
	if (Selected != GizmoBone)
	{
		Gizmo.EndDrag();
		Gizmo.ClearHover();
		GizmoBone = Selected;
	}

	const bool bMouseDown = FInputSystem::IsMouseDown(EMouseButton::Left);
	const bool bMousePressed = FInputSystem::IsMousePressed(EMouseButton::Left);
	// 카메라를 돌리는 중이거나 커서가 View 밖이면 새로 잡거나 고르지 않는다
	const bool bCanInteract = bViewHovered && Adapter.GetCapturedViewIndex() == InvalidViewIndex;

	// 긴 기즈모 축이 다른 뼈를 덮고 있어도 고를 수 있도록, 누른 채 움직이지 않은 입력은 클릭으로 본다
	if (bMousePressed && bCanInteract)
	{
		bClickCandidate = true;
		ClickStartMouse = LocalMouse;
	}
	if (bClickCandidate)
	{
		const FVector2 Moved = LocalMouse - ClickStartMouse;
		if (Moved.X * Moved.X + Moved.Y * Moved.Y > ClickSlopPixels * ClickSlopPixels
			|| Adapter.GetCapturedViewIndex() != InvalidViewIndex)
		{
			bClickCandidate = false;
		}
	}

	if (Selected < 0)
	{
		Gizmo.SetTarget(nullptr);
	}
	else
	{
		// 드래그 중이 아니면 디테일 창이나 부모 뼈가 바꾼 값을 프록시가 따라간다
		if (!Gizmo.IsUsing())
		{
			BoneProxy->SetTransform(DecomposeMatrix(MeshComponent->GetBoneWorldTransform(Selected)));
		}
		Gizmo.SetTarget(BoneProxy);

		// 커서가 View 밖으로 나간 채 버튼을 놓으면 아래 Update가 불리지 않으므로 여기서 끝낸다
		if (Gizmo.IsUsing() && !bMouseDown)
		{
			Gizmo.EndDrag();
		}

		FRay Ray{};
		if ((Gizmo.IsUsing() || bCanInteract) && Adapter.TryGetActiveViewRay(LocalMouse, Ray))
		{
			// 다른 곳에서 누른 채 축 위로 지나갈 때 잡히지 않도록, 새 드래그는 누른 순간에만 시작한다
			const bool bGrab = Gizmo.IsUsing() ? bMouseDown : bMousePressed;
			const FRect& Rect = Adapter.GetViewRect(ViewIndex);
			Gizmo.Update(Ray, LocalMouse, Adapter.GetEngineViewProjection(ViewIndex),
				static_cast<int>(Rect.Width), static_cast<int>(Rect.Height), bGrab,
				Adapter.GetEngineCameraLocation(ViewIndex), Adapter.IsOrthographic(ViewIndex));

			// 클릭만으로는 뼈 값을 건드리지 않는다. 행렬을 나눴다 합치면 전단 성분이 사라질 수 있다
			if (Gizmo.IsUsing() && !bClickCandidate)
			{
				ApplyProxyToBone(Selected);
			}
		}
		else
		{
			Gizmo.ClearHover();
		}
	}

	// 버튼을 놓을 때 클릭으로 확정한다. 레벨 뷰포트처럼 빈 곳을 눌러도 선택을 유지한다
	if (bClickCandidate && !bMouseDown)
	{
		bClickCandidate = false;
		const int32 Picked = PickBone(ClickStartMouse);
		if (Picked >= 0)
		{
			MeshComponent->SetSelectedBone(Picked);
		}
	}
}

int32 FSkeletalMeshViewer::PickBone(const FVector2& LocalMouse) const
{
	TArray<FVector> Joints;
	TArray<float> Radii;
	MeshComponent->GetBoneSpheres(Joints, Radii);

	const FRect& Rect = Adapter.GetViewRect(ViewIndex);
	const int ScreenW = static_cast<int>(Rect.Width);
	const int ScreenH = static_cast<int>(Rect.Height);
	const FMatrix ViewProjection = Adapter.GetEngineViewProjection(ViewIndex);
	const FVector CameraLocation = Adapter.GetEngineCameraLocation(ViewIndex);
	const FVector CameraForward = Adapter.GetEngineCameraForward(ViewIndex);
	const FVector CameraUp = Adapter.GetEngineCameraUp(ViewIndex);
	const bool bOrthographic = Adapter.IsOrthographic(ViewIndex);

	int32 Best = -1;
	float BestDistance = FLT_MAX;
	for (int32 i = 0; i < Joints.Num(); i++)
	{
		// 카메라 뒤의 점은 화면에 뒤집혀 투영된다
		if (!bOrthographic && (Joints[i] - CameraLocation).Dot(CameraForward) <= 0.0f)
			continue;

		const FVector2 Center = WorldToScreen(Joints[i], ViewProjection, ScreenW, ScreenH);
		const FVector2 Edge = WorldToScreen(Joints[i] + CameraUp * Radii[i], ViewProjection, ScreenW, ScreenH);
		const FVector2 ToEdge = Edge - Center;
		const FVector2 ToMouse = LocalMouse - Center;
		const float ScreenRadius = sqrtf(ToEdge.X * ToEdge.X + ToEdge.Y * ToEdge.Y);
		const float Distance = sqrtf(ToMouse.X * ToMouse.X + ToMouse.Y * ToMouse.Y);
		if (Distance > std::max(ScreenRadius, MinBonePickPixels))
			continue;

		// 구체가 겹치면 중심이 커서에 가까운 뼈를 고른다
		// Dummy처럼 거의 같은 자리면 앞 번호(부모)를 유지한다. 부모를 움직이면 같은 자리의 자식도 함께 움직인다
		if (Distance < BestDistance - SameSpotPixels)
		{
			Best = i;
			BestDistance = Distance;
		}
	}
	return Best;
}

void FSkeletalMeshViewer::ApplyProxyToBone(const int32 BoneIndex)
{
	const int32 Parent = MeshComponent->GetSkeletalMesh()->GetSkeleton().Bones[BoneIndex].ParentIndex;
	const FMatrix ParentWorld = Parent >= 0 ? MeshComponent->GetBoneWorldTransform(Parent) : MeshComponent->GetWorldMatrix();
	MeshComponent->SetBoneLocalTransform(BoneIndex, BoneProxy->GetWorldMatrix() * ParentWorld.Inverse());
}

void FSkeletalMeshViewer::SetViewSize(const uint32 Width, const uint32 Height)
{
	Targets.Resize(Width, Height);
	Adapter.UpdateLayout(FVector2(static_cast<float>(Width), static_cast<float>(Height)), FVector2(0.0f, 0.0f));
}

void FSkeletalMeshViewer::FrameMesh(const USkeletalMesh& Mesh)
{
	FViewCamera Camera = Adapter.GetViewCamera(ViewIndex);
	Camera.Transform.Rotation = LookTowardNegativeX;
	Adapter.SetViewCamera(ViewIndex, Camera);

	FocusOnBounds(Mesh);
}

void FSkeletalMeshViewer::FocusOnBounds(const USkeletalMesh& Mesh)
{
	const FBox& Bounds = Mesh.GetMeshData().AABB;
	Adapter.FocusViewOn(ViewIndex, (Bounds.Min + Bounds.Max) * 0.5f, (Bounds.Max - Bounds.Min).Length() * 0.5f);
}

void FSkeletalMeshViewer::FocusOnSelectedBone()
{
	const USkeletalMesh* Mesh = MeshComponent ? MeshComponent->GetSkeletalMesh() : nullptr;
	if (!Mesh)
		return;

	const int32 Selected = MeshComponent->GetSelectedBone();
	if (Selected < 0)
	{
		FocusOnBounds(*Mesh);
		return;
	}

	// 가장 긴 자식 뼈까지 담는다. 끝 뼈는 자기 길이를 쓴다
	const auto JointOf = [this](const int32 BoneIndex)
	{
		return MeshComponent->GetBoneWorldTransform(BoneIndex).TransformPosition(FVector(0.f, 0.f, 0.f));
	};
	const TArray<FBoneInfo>& Bones = Mesh->GetSkeleton().Bones;
	const FVector Joint = JointOf(Selected);
	float Radius = 0.0f;
	for (int32 i = 0; i < Bones.Num(); i++)
	{
		if (Bones[i].ParentIndex == Selected)
		{
			Radius = std::max(Radius, (JointOf(i) - Joint).Length());
		}
	}
	if (Radius <= 0.0f && Bones[Selected].ParentIndex >= 0)
	{
		Radius = (Joint - JointOf(Bones[Selected].ParentIndex)).Length();
	}

	Adapter.FocusViewOn(ViewIndex, Joint, Radius);   // 너무 작은 값은 FocusViewOn이 최소 반지름으로 올린다
}

void FSkeletalMeshViewer::CreatePreviewWorld()
{
	// 지난번 월드의 컴포넌트 포인터와 우클릭 캡처 상태를 남기지 않는다
	Adapter = FMultipleViewportsAdapter{};
	Adapter.InitializeDefaultViews();
	Adapter.SetLayoutMode(ELayoutMode::Single);
	Adapter.SetSingleViewIndex(ViewIndex);
	SetViewSize(DefaultViewWidth, DefaultViewHeight);

	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::EditorPreview);
	Context.SetCurrentWorld(UWorld::CreateWorld(EWorldType::EditorPreview, true));
	PreviewWorld = Context.World();

	ASkeletalMeshPreviewActor* PreviewActor = PreviewWorld->SpawnActor<ASkeletalMeshPreviewActor>();
	MeshComponent = PreviewActor->GetMeshComponent();
	BoneProxy = PreviewActor->GetBoneProxy();

	ADirectionalLightActor* DirectionalLight = PreviewWorld->SpawnActor<ADirectionalLightActor>();
	DirectionalLight->SetActorRotation(PreviewDirectionalLightRotation);
	HideBillboards(*DirectionalLight);

	AAmbientLightActor* AmbientLight = PreviewWorld->SpawnActor<AAmbientLightActor>();
	AmbientLight->GetLightComponent()->SetIntensity(PreviewAmbientIntensity);
	HideBillboards(*AmbientLight);
}
