// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "JGDMessageBusTypes.h"
#include "JGDMessageSubsystem.generated.h"

/**
 * 基于 GameplayTag 频道的事件总线。
 *
 * 发送方只认频道 Tag，不认识接收方；接收方也只认频道 Tag，不认识发送方。
 * 频道定义在 Config/DefaultGameplayTags.ini。
 *
 * 负载是 FInstancedStruct，可以装任意 USTRUCT：
 *   蓝图打包 → Make Instanced Struct
 *   蓝图解包 → Get Instanced Struct Value（Valid 分支后才有值）
 * 不需要负载时传一个空的 InstancedStruct 即可，当作纯通知。
 */
UCLASS()
class JGD2026_API UJGDMessageSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** C++ 侧取总线：UJGDMessageSubsystem::Get(this) */
	static UJGDMessageSubsystem* Get(const UObject* WorldContextObject);

	/** 蓝图侧取总线（等价于 Get Game Instance Subsystem 节点） */
	UFUNCTION(BlueprintCallable, Category = "JGD|MessageBus",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get JGD Message Subsystem"))
	static UJGDMessageSubsystem* GetMessageSubsystem(const UObject* WorldContextObject);

	/**
	 * 往频道广播一条消息。
	 * 广播瞬间订阅该频道的监听会被同步调用，顺序 = 注册顺序。
	 *
	 * Message 可以留空：蓝图里不连这个引脚、C++ 里不传这个参数都行。
	 * 留空时就是一条不带负载的纯通知，接收端拿到的 Message 是空的
	 * （IsValid() 为 false）。
	 */
	UFUNCTION(BlueprintCallable, Category = "JGD|MessageBus", meta = (AutoCreateRefTerm = "Message"))
	void BroadcastMessage(FGameplayTag Channel, const FInstancedStruct& Message = FInstancedStruct());

	/**
	 * 订阅频道。返回的句柄存进变量，退出时（比如 EndPlay）用 UnregisterListener 退订。
	 */
	UFUNCTION(BlueprintCallable, Category = "JGD|MessageBus")
	FJGDMessageListenerHandle RegisterListener(FGameplayTag Channel, const FJGDMessageReceived& Delegate);

	/** 按句柄退订。重复调用是安全的。 */
	UFUNCTION(BlueprintCallable, Category = "JGD|MessageBus")
	void UnregisterListener(UPARAM(ref) FJGDMessageListenerHandle& Handle);

	/** 退订某个对象在本总线上的全部监听。 */
	UFUNCTION(BlueprintCallable, Category = "JGD|MessageBus")
	void UnregisterAllListeners(const UObject* Listener);

	/** 该频道当前是否有人订阅（用来省掉无谓的负载构造）。 */
	UFUNCTION(BlueprintPure, Category = "JGD|MessageBus")
	bool HasListeners(FGameplayTag Channel) const;

	virtual void Deinitialize() override;

private:
	struct FListenerEntry
	{
		int32 Id = INDEX_NONE;

		// 动态委托内部持有弱对象引用：监听对象被销毁后 IsBound() 会变 false，
		// 所以这里既不会拖住 GC，也不需要监听方显式退订。
		FJGDMessageReceived Delegate;
	};

	/** 清掉监听对象已销毁的条目，顺带删掉空频道 */
	void PruneDeadListeners();

	TMap<FGameplayTag, TArray<FListenerEntry>> ListenersByChannel;

	int32 NextListenerId = 0;
};
