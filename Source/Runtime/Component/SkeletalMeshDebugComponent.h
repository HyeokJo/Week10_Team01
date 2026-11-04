#pragma once

#include "SkeletalMeshComponent.h"

class FLineBatcher;

// 뼈를 선으로 그려 보는 디버그용 컴포넌트
// 에디터가 매 프레임 DrawDebug를 부른다 (UShapeComponent::DrawDebug와 같은 자리)
class USkeletalMeshDebugComponent : public USkeletalMeshComponent
{
	DECLARE_CLASS(USkeletalMeshDebugComponent, USkeletalMeshComponent)

	REFLECT_START(USkeletalMeshDebugComponent)
		PROPERTY(bDrawBones)
		PROPERTY(bDrawBoneAxes)
		PROPERTY(BoneAxisLength)
	REFLECT_END()
public:
	USkeletalMeshDebugComponent();
	virtual ~USkeletalMeshDebugComponent();

	bool ShouldDrawDebug() const { return bDrawBones || bDrawBoneAxes; }

	// 부모 관절 -> 자식 관절 선, 관절마다 X/Y/Z 축 선
	void DrawDebug(FLineBatcher* LineBatcher) const;

private:
	bool bDrawBones = true;
	bool bDrawBoneAxes = true;
	float BoneAxisLength = 0.05f;
};
