// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "JGDAudioTypes.generated.h"

/**
 * PlayAudioAtLocation / PlayAudio2D 返回的句柄。
 * 存进变量，用来停止、改响度、改坐标。
 */
USTRUCT(BlueprintType)
struct JGD2026_API FJGDAudioHandle
{
	GENERATED_BODY()

	/** 音频总线内部分配的声音编号 */
	UPROPERTY(BlueprintReadOnly, Category = "JGD|Audio")
	int32 VoiceId = INDEX_NONE;

	/** 是否指向一个真实播放过的声音。停止后自动变 false。 */
	UPROPERTY(BlueprintReadOnly, Category = "JGD|Audio")
	bool bValid = false;

	bool IsValid() const { return bValid; }

	void Invalidate()
	{
		VoiceId = INDEX_NONE;
		bValid = false;
	}
};
