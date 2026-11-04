#pragma once

#include "SkeletalMeshComponent.h"

class FLineBatcher;

// 뼈를 구체와 꼬리 선으로 그려 보는 디버그용 컴포넌트. 스켈레탈 메시 뷰어가 쓴다
// 에디터가 매 프레임 DrawDebug를 부른다 (UShapeComponent::DrawDebug와 같은 자리)
class USkeletalMeshDebugComponent : public USkeletalMeshComponent
{
	DECLARE_CLASS(USkeletalMeshDebugComponent, USkeletalMeshComponent)

	REFLECT_START(USkeletalMeshDebugComponent)
		PROPERTY(bDrawBones)
		PROPERTY(bDrawBoneAxes)
		PROPERTY(BoneAxisLength)
		PROPERTY(BoneRadiusScale)
	REFLECT_END()
public:
	USkeletalMeshDebugComponent();
	virtual ~USkeletalMeshDebugComponent();

	bool ShouldDrawDebug() const { return bDrawBones || bDrawBoneAxes; }

	// 관절마다 구체, 부모 -> 자식 꼬리 선. bDrawBoneAxes를 켜면 X/Y/Z 축 선도 그린다
	void DrawDebug(FLineBatcher* LineBatcher) const;

	// 고른 뼈와 그 뼈로 들어오는 꼬리를 다른 색으로 그린다. -1이면 선택 없음
	int32 GetSelectedBone() const { return SelectedBoneIndex; }
	void SetSelectedBone(int32 BoneIndex);

	// 그려지는 구체의 월드 위치와 반지름. 그리기와 클릭 판정이 같은 값을 쓴다. 뼈 번호로 인덱싱한다
	void GetBoneSpheres(TArray<FVector>& OutJoints, TArray<float>& OutRadii) const;

protected:
	virtual void OnSkeletalMeshChanged() override;

private:
	bool bDrawBones = true;
	bool bDrawBoneAxes = false;
	float BoneAxisLength = 0.05f;
	// 구체 반지름의 상한 = 메시 바운드 대각선 길이 × 이 값. 짧은 뼈는 뼈 길이에 맞춰 더 작아진다
	float BoneRadiusScale = 0.005f;

	int32 SelectedBoneIndex = -1;
};
