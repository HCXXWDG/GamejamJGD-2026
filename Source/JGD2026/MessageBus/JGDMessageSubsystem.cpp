// Copyright Epic Games, Inc. All Rights Reserved.

#include "JGDMessageSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogJGDMessageBus, Log, All);

UJGDMessageSubsystem* UJGDMessageSubsystem::Get(const UObject* WorldContextObject)
{
	if (WorldContextObject == nullptr)
	{
		return nullptr;
	}

	const UWorld* World = GEngine
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: nullptr;

	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;

	return GameInstance ? GameInstance->GetSubsystem<UJGDMessageSubsystem>() : nullptr;
}

UJGDMessageSubsystem* UJGDMessageSubsystem::GetMessageSubsystem(const UObject* WorldContextObject)
{
	return Get(WorldContextObject);
}

void UJGDMessageSubsystem::BroadcastMessage(FGameplayTag Channel, const FInstancedStruct& Message)
{
	if (!Channel.IsValid())
	{
		UE_LOG(LogJGDMessageBus, Warning, TEXT("BroadcastMessage 收到无效频道，已忽略。"));
		return;
	}

	const TArray<FListenerEntry>* Entries = ListenersByChannel.Find(Channel);
	if (Entries == nullptr)
	{
		return;
	}

	// 先拷一份再分发：回调里可能订阅/退订，直接遍历原数组会失效。
	const TArray<FListenerEntry> Snapshot = *Entries;

	for (const FListenerEntry& Entry : Snapshot)
	{
		if (Entry.Delegate.IsBound())
		{
			Entry.Delegate.Execute(Channel, Message);
		}
	}

	// 回调可能把整个频道都删了，所以重新查一次，不要复用上面的指针。
	if (TArray<FListenerEntry>* Live = ListenersByChannel.Find(Channel))
	{
		Live->RemoveAll([](const FListenerEntry& Entry) { return !Entry.Delegate.IsBound(); });

		if (Live->Num() == 0)
		{
			ListenersByChannel.Remove(Channel);
		}
	}
}

FJGDMessageListenerHandle UJGDMessageSubsystem::RegisterListener(FGameplayTag Channel, const FJGDMessageReceived& Delegate)
{
	FJGDMessageListenerHandle Handle;
	Handle.Channel = Channel;

	if (!Channel.IsValid())
	{
		UE_LOG(LogJGDMessageBus, Warning, TEXT("RegisterListener 失败：频道无效。"));
		return Handle;
	}

	if (!Delegate.IsBound())
	{
		UE_LOG(LogJGDMessageBus, Warning, TEXT("RegisterListener 失败：频道 [%s] 的回调没有绑定。"), *Channel.ToString());
		return Handle;
	}

	FListenerEntry Entry;
	Entry.Id = NextListenerId++;
	Entry.Delegate = Delegate;

	ListenersByChannel.FindOrAdd(Channel).Add(Entry);

	Handle.ListenerId = Entry.Id;
	Handle.bValid = true;
	return Handle;
}

void UJGDMessageSubsystem::UnregisterListener(FJGDMessageListenerHandle& Handle)
{
	if (!Handle.bValid)
	{
		return;
	}

	if (TArray<FListenerEntry>* Entries = ListenersByChannel.Find(Handle.Channel))
	{
		const int32 NumRemoved = Entries->RemoveAll(
			[&Handle](const FListenerEntry& Entry) { return Entry.Id == Handle.ListenerId; });

		if (NumRemoved > 0 && Entries->Num() == 0)
		{
			ListenersByChannel.Remove(Handle.Channel);
		}
	}

	Handle.Invalidate();
}

void UJGDMessageSubsystem::UnregisterAllListeners(const UObject* Listener)
{
	if (Listener == nullptr)
	{
		return;
	}

	for (auto It = ListenersByChannel.CreateIterator(); It; ++It)
	{
		It.Value().RemoveAll(
			[Listener](const FListenerEntry& Entry) { return Entry.Delegate.GetUObject() == Listener; });

		if (It.Value().Num() == 0)
		{
			It.RemoveCurrent();
		}
	}
}

bool UJGDMessageSubsystem::HasListeners(FGameplayTag Channel) const
{
	const TArray<FListenerEntry>* Entries = ListenersByChannel.Find(Channel);

	return Entries != nullptr
		&& Entries->ContainsByPredicate([](const FListenerEntry& Entry) { return Entry.Delegate.IsBound(); });
}

void UJGDMessageSubsystem::Deinitialize()
{
	ListenersByChannel.Empty();
	NextListenerId = 0;

	Super::Deinitialize();
}

void UJGDMessageSubsystem::PruneDeadListeners()
{
	for (auto It = ListenersByChannel.CreateIterator(); It; ++It)
	{
		It.Value().RemoveAll([](const FListenerEntry& Entry) { return !Entry.Delegate.IsBound(); });

		if (It.Value().Num() == 0)
		{
			It.RemoveCurrent();
		}
	}
}
