// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "StructUtils/InstancedStruct.h"
#include "JGDMessageBusTypes.generated.h"

/**
 * 监听回调签名：频道 + 负载。
 *
 * 蓝图里用法：RegisterListener 的 Delegate 引脚上右键 → Create Event，
 * 会自动生成一个签名匹配的自定义事件，在里面处理消息。
 */
DECLARE_DYNAMIC_DELEGATE_TwoParams(FJGDMessageReceived, FGameplayTag, Channel, const FInstancedStruct&, Message);

/**
 * RegisterListener 返回的句柄。存进变量，退订时原样传回 UnregisterListener。
 */
USTRUCT(BlueprintType)
struct JGD2026_API FJGDMessageListenerHandle
{
	GENERATED_BODY()

	/** 注册时用的频道 */
	UPROPERTY(BlueprintReadOnly, Category = "JGD|MessageBus")
	FGameplayTag Channel;

	/** 总线内部分配的监听编号 */
	UPROPERTY(BlueprintReadOnly, Category = "JGD|MessageBus")
	int32 ListenerId = INDEX_NONE;

	/** 是否指向一个仍然有效的注册。退订后自动变 false。 */
	UPROPERTY(BlueprintReadOnly, Category = "JGD|MessageBus")
	bool bValid = false;

	bool IsValid() const { return bValid; }

	void Invalidate()
	{
		ListenerId = INDEX_NONE;
		bValid = false;
	}
};
