#pragma once

enum class EWorldType : uint8
{
	None,
	Game,
	Editor,
	PIE,
	EditorPreview   // 에셋 뷰어용 월드. Editor처럼 게임 로직을 돌리지 않는다
};
