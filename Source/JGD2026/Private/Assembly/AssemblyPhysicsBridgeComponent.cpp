#include "Assembly/AssemblyPhysicsBridgeComponent.h"

#include "Assembly/AssemblyComponent.h"
#include "Assembly/AssemblyPhysicsLifecycle.h"
#include "Assembly/AssemblySessionComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "MessageBus/JGDMessageSubsystem.h"
#include "Misc/ScopeExit.h"

#define LOCTEXT_NAMESPACE "AssemblyPhysicsBridge"

namespace AssemblyMessageTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(AssemblyChanged, "Event.Assembly.Changed", "FAssemblyPhysicsSnapshotMessage");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(AssemblyReset, "Event.Assembly.Reset", "FAssemblyPhysicsSnapshotMessage");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(AssemblyPhaseChanged, "Event.Assembly.PhaseChanged", "FAssemblyPhysicsSnapshotMessage");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(PhysicsRebuildRequested, "Event.Physics.Assembly.RebuildRequested", "FAssemblyPhysicsSnapshotMessage");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(MuscleDriveRequested, "Event.Physics.Muscle.DriveRequested", "FAssemblyPhysicsMuscleDriveMessage");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(CoreRotationRequested, "Event.Physics.Core.RotationRequested", "FAssemblyPhysicsCoreRotationMessage");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(JointBreakRequested, "Event.Physics.Joint.BreakRequested", "FAssemblyPhysicsJointBreakMessage");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(JointBroken, "Event.Physics.Joint.Broken", "FAssemblyPhysicsJointBrokenMessage");
}

namespace
{
	// Weak entries do not keep a model, body, world or another PIE session alive.
	TMap<TWeakObjectPtr<UAssemblyComponent>, TWeakObjectPtr<UAssemblyPhysicsBridgeComponent>> ActiveBridges;
}

UAssemblyPhysicsBridgeComponent::UAssemblyPhysicsBridgeComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

UObject* UAssemblyPhysicsBridgeComponent::GetPhysicsLifecycleProvider() const
{
	UObject* Provider = PhysicsLifecycleProvider.Get();
	return IsValid(Provider) && Provider->GetWorld() == GetWorld()
		&& Provider->GetClass()->ImplementsInterface(UAssemblyPhysicsLifecycle::StaticClass()) ? Provider : nullptr;
}

bool UAssemblyPhysicsBridgeComponent::HasPhysicsLifecycleProvider() const
{
	return GetPhysicsLifecycleProvider() != nullptr;
}

bool UAssemblyPhysicsBridgeComponent::HasSessionLifecycleOwnership() const
{
	const AActor* Owner = GetOwner();
	const UAssemblySessionComponent* Session = Owner ? Owner->FindComponentByClass<UAssemblySessionComponent>() : nullptr;
	return !IsValid(Session) || Session->CanOwnPhysicsLifecycle(BoundAssembly.Get());
}

bool UAssemblyPhysicsBridgeComponent::ResolvePhysicsLifecycleProvider(FText& OutReason)
{
	if (bExplicitLifecycleProvider)
	{
		if (!GetPhysicsLifecycleProvider())
		{
			OutReason = LOCTEXT("ProviderEnded", "指定的物理执行器已失效或不属于当前世界，请重新指定执行器。");
			return false;
		}
		return true;
	}
	TArray<UObject*> Candidates;
	if (AActor* Owner = GetOwner())
	{
		if (Owner->GetClass()->ImplementsInterface(UAssemblyPhysicsLifecycle::StaticClass())) { Candidates.Add(Owner); }
		TArray<UActorComponent*> Components;
		Owner->GetComponents(Components);
		for (UActorComponent* Component : Components)
		{
			if (IsValid(Component) && Component != this && Component->GetWorld() == GetWorld()
				&& Component->GetClass()->ImplementsInterface(UAssemblyPhysicsLifecycle::StaticClass()))
			{
				Candidates.Add(Component);
			}
		}
	}
	if (Candidates.Num() > 1)
	{
		OutReason = LOCTEXT("AmbiguousProvider", "角色上有多个物理生命周期执行器，请使用 Set Physics Lifecycle Provider 明确选择一个。");
		return false;
	}
	UObject* Candidate = Candidates.IsEmpty() ? nullptr : Candidates[0];
	if (PhysicsLifecycleProvider.IsStale() && !bLifecycleShutdown)
	{
		OutReason = LOCTEXT("ProviderEnded", "指定的物理执行器已失效或不属于当前世界，请重新指定执行器。");
		return false;
	}
	if (PhysicsLifecycleProvider.IsValid() && PhysicsLifecycleProvider.Get() != Candidate && !bLifecycleShutdown)
	{
		OutReason = LOCTEXT("ProviderChanged", "活动物理执行器发生变化，请先停止旧执行器，再明确重新绑定。");
		return false;
	}
	PhysicsLifecycleProvider = Candidate;
	if (!Candidate && bRequirePhysicsLifecycleProvider)
	{
		OutReason = LOCTEXT("ProviderRequired", "当前关卡要求真实物理执行器，请先绑定 Assembly Physics Lifecycle 接口。");
		return false;
	}
	return true;
}

bool UAssemblyPhysicsBridgeComponent::SetPhysicsLifecycleProvider(UObject* Provider, FText& OutReason)
{
	OutReason = FText::GetEmpty();
	if (bClearing || bLifecycleBusy)
	{
		OutReason = LOCTEXT("LifecycleBusy", "物理生命周期正在切换或关闭，请在同步回调结束后重试。");
		return false;
	}
	if (Provider && (!IsValid(Provider) || !GetWorld() || Provider->GetWorld() != GetWorld()
		|| !Provider->GetClass()->ImplementsInterface(UAssemblyPhysicsLifecycle::StaticClass())))
	{
		OutReason = LOCTEXT("InvalidProvider", "物理执行器必须属于当前世界，并实现 Assembly Physics Lifecycle 接口。");
		return false;
	}
	if (Provider == GetPhysicsLifecycleProvider() && (Provider != nullptr || !bExplicitLifecycleProvider))
	{
		bExplicitLifecycleProvider = Provider != nullptr;
		return true;
	}
	if (IsBridgeInitialized() && BoundAssembly->GetPhase() == EAssemblyPhase::Playing && !bLifecycleShutdown)
	{
		OutReason = LOCTEXT("ReplaceDuringPlaying", "操控阶段不能直接替换物理执行器，请先关闭物理生命周期或重开回到组装。");
		return false;
	}
	const FAssemblyPhysicsContext Binding = GetPhysicsContext();
	if (bInitialized) { ShutdownPhysicsLifecycle(); }
	if (bInitialized && (SourceId != Binding.SourceId || RoundGeneration != Binding.RoundGeneration))
	{
		OutReason = LOCTEXT("ProviderBindingCanceled", "关闭旧执行器期间组装绑定已变化，请重新绑定执行器。");
		return false;
	}
	PhysicsLifecycleProvider = Provider;
	bExplicitLifecycleProvider = Provider != nullptr;
	bLifecycleShutdown = false;
	if (bInitialized && !HasSessionLifecycleOwnership())
	{
		// Legacy Blueprint BeginPlay may configure the provider before Session claims/freezes its proxy.
		bLifecycleShutdown = true;
		return true;
	}
	return !bInitialized || RefreshPhysicsLifecycle(OutReason);
}

bool UAssemblyPhysicsBridgeComponent::IsLifecycleBindingCurrent(
	const FAssemblyPhysicsContext& Context, const UObject* Provider) const
{
	// Registering real bodies and requesting rebuilds during Prepare may advance Revision.
	return IsCurrentContext(Context) && !bClearBridgePending && !bLifecycleShutdownPending
		&& !bAbortPlayingPending && HasSessionLifecycleOwnership() && GetPhysicsLifecycleProvider() == Provider;
}

bool UAssemblyPhysicsBridgeComponent::ValidatePhysicalBodies(
	const FAssemblyPhysicsSnapshotMessage& Snapshot, FText& OutReason) const
{
	auto IsReady = [this](UPrimitiveComponent* Body)
	{
		return IsValid(Body) && Body->IsRegistered() && Body->GetWorld() == GetWorld();
	};
	if (!IsReady(Snapshot.CoreBody.Get()))
	{
		OutReason = LOCTEXT("CoreNotReady", "物理准备失败：真实核心刚体必须有效、已注册并属于当前世界。");
		return false;
	}
	for (const FAssemblyPhysicsBonePose& Pose : Snapshot.BonePoses)
	{
		if (!IsReady(Pose.Body.Get()))
		{
			OutReason = FText::Format(LOCTEXT("BodyNotReady", "物理准备失败：组件 {0} 尚未提供有效且已注册的刚体。"),
				FText::FromString(Pose.BoneId.ToString()));
			return false;
		}
	}
	if (const ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
		if (IsValid(Capsule) && Capsule->IsSimulatingPhysics() && Capsule != Snapshot.CoreBody.Get()
			&& !Snapshot.BonePoses.ContainsByPredicate([Capsule](const FAssemblyPhysicsBonePose& Pose)
				{ return Pose.Body.Get() == Capsule || Pose.PoseSource.Get() == Capsule; }))
		{
			OutReason = LOCTEXT("UnregisteredProxyPhysics", "物理准备失败：角色胶囊正在模拟物理，但未作为核心或组装刚体登记；请由物理程序统一接管。");
			return false;
		}
	}
	return true;
}

void UAssemblyPhysicsBridgeComponent::AbortPreparedState()
{
	if (!bHasPreparedPlaying) { return; }
	const TWeakObjectPtr<UObject> Provider = PreparedProvider;
	FAssemblyPhysicsSnapshotMessage Snapshot = PreparedSnapshot;
	const FAssemblyPhysicsSnapshotMessage BeforePreparation = BeforePreparationSnapshot;
	FAssemblyPhysicsSnapshotMessage Latest;
	if (GetPhysicsSnapshot(Latest) && Latest.Context.SourceId == Snapshot.Context.SourceId
		&& Latest.Context.RoundGeneration == Snapshot.Context.RoundGeneration) { Snapshot = MoveTemp(Latest); }
	bHasPreparedPlaying = false;
	PreparedProvider.Reset();
	PreparedSnapshot = FAssemblyPhysicsSnapshotMessage();
	BeforePreparationSnapshot = FAssemblyPhysicsSnapshotMessage();
	if (Provider.IsValid()) { IAssemblyPhysicsLifecycle::Execute_AbortPreparedPlaying(Provider.Get(), Snapshot); }
	// The executor owns physical cleanup; this bridge only rolls back references it let Prepare replace.
	RestorePreparationPoseSources(BeforePreparation);
}

void UAssemblyPhysicsBridgeComponent::RestorePreparationPoseSources(
	const FAssemblyPhysicsSnapshotMessage& BeforePreparation)
{
	UAssemblyComponent* Model = BoundAssembly.Get();
	const FAssemblyPhysicsContext& Binding = BeforePreparation.Context;
	if (!bInitialized || !IsValid(Model) || !Model->IsInitialized() || Model->IsEndingPlay()
		|| Binding.Assembly != Model || Binding.World != GetWorld() || Binding.SourceId != SourceId
		|| Binding.RoundGeneration != RoundGeneration) { return; }
	for (const FAssemblyPhysicsBonePose& Pose : BeforePreparation.BonePoses)
	{
		FAssemblyBoneInstance Instance;
		if (!Model->GetBone(Pose.BoneId, Instance)) { continue; }
		USceneComponent* Original = Pose.PoseSource.Get();
		if (IsValid(Original) && Original->GetWorld() == GetWorld())
		{
			FText RestoreReason;
			Model->RegisterBonePoseSource(Pose.BoneId, Original, RestoreReason);
		}
		else
		{
			// Includes an originally absent source; never retain a disposed preparation body.
			Model->UnregisterBonePoseSource(Pose.BoneId);
		}
	}
	UPrimitiveComponent* OriginalCore = BeforePreparation.CoreBody.Get();
	CoreBody = IsValid(OriginalCore) && OriginalCore->GetWorld() == GetWorld() ? OriginalCore : nullptr;
	RequestPhysicsRebuild();
}

void UAssemblyPhysicsBridgeComponent::AbortPreparedPlaying()
{
	if (bLifecycleBusy) { bAbortPlayingPending = true; return; }
	ON_SCOPE_EXIT { FlushDeferredLifecycleActions(); };
	TGuardValue<bool> Guard(bLifecycleBusy, true);
	AbortPreparedState();
}

bool UAssemblyPhysicsBridgeComponent::PrepareForPlaying(FText& OutReason)
{
	OutReason = FText::GetEmpty();
	if (bLifecycleBusy || bClearing || !IsBridgeInitialized())
	{
		OutReason = LOCTEXT("PrepareUnavailable", "物理桥未初始化或正在执行同步生命周期回调。");
		return false;
	}
	if (!HasSessionLifecycleOwnership())
	{
		OutReason = LOCTEXT("SessionNotReady", "请先初始化角色的 Assembly Session；物理阶段只能在会话接管并冻结角色移动后启用。");
		return false;
	}
	if (!ResolvePhysicsLifecycleProvider(OutReason)) { return false; }
	FAssemblyPhysicsSnapshotMessage Snapshot;
	if (!GetPhysicsSnapshot(Snapshot))
	{
		OutReason = LOCTEXT("MissingSnapshot", "无法取得当前组装的物理快照。");
		return false;
	}
	LastLifecycleSnapshot = Snapshot;
	bLifecycleShutdown = false;
	UObject* Provider = GetPhysicsLifecycleProvider();
	if (!Provider) { return true; }
	ON_SCOPE_EXIT { FlushDeferredLifecycleActions(); };
	TGuardValue<bool> Guard(bLifecycleBusy, true);
	// A manual preflight may precede the model's gate; never overwrite its rollback token.
	AbortPreparedState();
	if (!IsLifecycleBindingCurrent(Snapshot.Context, Provider))
	{
		OutReason = LOCTEXT("PrepareCanceled", "物理准备期间会话或绑定已被关闭、替换或重开。");
		return false;
	}
	// Aborting an earlier manual preparation restored the editable body's registrations.
	if (!GetPhysicsSnapshot(Snapshot))
	{
		OutReason = LOCTEXT("MissingSnapshot", "无法取得当前组装的物理快照。");
		return false;
	}
	LastLifecycleSnapshot = Snapshot;
	// Keep an abort token before invoking Prepare, including partial/failed preparations.
	PreparedProvider = Provider;
	PreparedSnapshot = Snapshot;
	BeforePreparationSnapshot = Snapshot;
	bHasPreparedPlaying = true;
	const bool bPrepared = IAssemblyPhysicsLifecycle::Execute_PrepareForPlaying(Provider, Snapshot, OutReason);
	FAssemblyPhysicsSnapshotMessage ReadySnapshot;
	const bool bStillCurrent = IsLifecycleBindingCurrent(Snapshot.Context, Provider);
	const bool bBodiesReady = bPrepared && bStillCurrent && GetPhysicsSnapshot(ReadySnapshot)
		&& ValidatePhysicalBodies(ReadySnapshot, OutReason);
	if (!bBodiesReady)
	{
		if (OutReason.IsEmpty())
		{
			OutReason = bStillCurrent ? LOCTEXT("PrepareRejected", "物理执行器拒绝开始操控。")
				: LOCTEXT("PrepareCanceled", "物理准备期间会话或绑定已被关闭、替换或重开。");
		}
		AbortPreparedState();
		return false;
	}
	PreparedSnapshot = ReadySnapshot;
	LastLifecycleSnapshot = ReadySnapshot;
	return true;
}

void UAssemblyPhysicsBridgeComponent::ReleaseHeldPhysicsIntents()
{
	UJGDMessageSubsystem* Bus = MessageBus.Get();
	const FAssemblyPhysicsContext Context = GetPhysicsContext();
	const EAssemblyPhase Phase = BoundAssembly.IsValid() ? BoundAssembly->GetPhase() : LastLifecycleSnapshot.Phase;
	TMap<FGuid, FAssemblyPhysicsMuscleDriveMessage> Stops = MoveTemp(ActiveMuscleRequests);
	const bool bReleaseCore = bCoreWasHeld;
	bCoreWasHeld = false;
	if (!Bus) { return; }
	for (auto& Pair : Stops)
	{
		Pair.Value.Context = Context;
		Pair.Value.Phase = Phase;
		Pair.Value.bContracting = false;
		Pair.Value.Muscle.bContracting = false;
		Bus->BroadcastMessage(AssemblyMessageTags::MuscleDriveRequested, FInstancedStruct::Make(Pair.Value));
	}
	if (bReleaseCore)
	{
		FAssemblyPhysicsCoreRotationMessage Stop;
		Stop.Context = Context;
		Stop.Phase = Phase;
		Stop.CoreBody = CoreBody.Get();
		Stop.FrameTransform = BoundAssembly.IsValid() ? BoundAssembly->GetGridFrameTransform() : LastLifecycleSnapshot.FrameTransform;
		Stop.WorldClockwiseAxis = Stop.FrameTransform.TransformVectorNoScale(FVector(0, 1, 0)).GetSafeNormal();
		Bus->BroadcastMessage(AssemblyMessageTags::CoreRotationRequested, FInstancedStruct::Make(Stop));
	}
}

bool UAssemblyPhysicsBridgeComponent::StopBeforeReset(FText& OutReason)
{
	OutReason = FText::GetEmpty();
	if (bLifecycleBusy || bClearing || !IsBridgeInitialized())
	{
		OutReason = LOCTEXT("StopUnavailable", "物理桥未初始化或正在执行同步生命周期回调。");
		return false;
	}
	if (bLifecycleShutdown) { return true; }
	if (!ResolvePhysicsLifecycleProvider(OutReason)) { return false; }
	FAssemblyPhysicsSnapshotMessage Snapshot;
	if (!GetPhysicsSnapshot(Snapshot))
	{
		OutReason = LOCTEXT("MissingSnapshot", "无法取得当前组装的物理快照。");
		return false;
	}
	LastLifecycleSnapshot = Snapshot;
	UObject* Provider = GetPhysicsLifecycleProvider();
	ON_SCOPE_EXIT { FlushDeferredLifecycleActions(); };
	TGuardValue<bool> Guard(bLifecycleBusy, true);
	ReleaseHeldPhysicsIntents();
	if (!IsLifecycleBindingCurrent(Snapshot.Context, Provider))
	{
		OutReason = LOCTEXT("StopCanceled", "停止输入期间物理绑定已被关闭或替换。");
		return false;
	}
	if (Provider && !IAssemblyPhysicsLifecycle::Execute_StopBeforeReset(Provider, Snapshot, OutReason))
	{
		if (OutReason.IsEmpty()) { OutReason = LOCTEXT("StopRejected", "物理执行器尚未安全停止，保留当前组装，请稍后重试。"); }
		return false;
	}
	if (!IsLifecycleBindingCurrent(Snapshot.Context, Provider))
	{
		OutReason = LOCTEXT("StopCanceled", "停止物理期间会话或绑定已被关闭、替换或重开。");
		return false;
	}
	bLifecycleShutdown = true;
	bHasPreparedPlaying = false;
	PreparedProvider.Reset();
	PreparedSnapshot = FAssemblyPhysicsSnapshotMessage();
	BeforePreparationSnapshot = FAssemblyPhysicsSnapshotMessage();
	return true;
}

void UAssemblyPhysicsBridgeComponent::ApplyPhysicsPhase()
{
	if (bLifecycleBusy || bClearing || !IsBridgeInitialized()) { return; }
	if (!HasSessionLifecycleOwnership()) { bLifecycleShutdown = true; return; }
	FAssemblyPhysicsSnapshotMessage Snapshot;
	if (!GetPhysicsSnapshot(Snapshot)) { return; }
	LastLifecycleSnapshot = Snapshot;
	bLifecycleShutdown = false;
	UObject* Provider = GetPhysicsLifecycleProvider();
	if (!Provider) { return; }
	ON_SCOPE_EXIT { FlushDeferredLifecycleActions(); };
	TGuardValue<bool> Guard(bLifecycleBusy, true);
	IAssemblyPhysicsLifecycle::Execute_ApplyAssemblyPhase(Provider, Snapshot);
	if (!IsLifecycleBindingCurrent(Snapshot.Context, Provider)) { AbortPreparedState(); return; }
	// The model has committed Playing; later restarts use Stop/Shutdown rather than Abort.
	if (Snapshot.Phase == EAssemblyPhase::Playing)
	{
		bHasPreparedPlaying = false;
		PreparedProvider.Reset();
		PreparedSnapshot = FAssemblyPhysicsSnapshotMessage();
		BeforePreparationSnapshot = FAssemblyPhysicsSnapshotMessage();
	}
}

bool UAssemblyPhysicsBridgeComponent::RefreshPhysicsLifecycle(FText& OutReason)
{
	OutReason = FText::GetEmpty();
	if (bLifecycleBusy || bClearing || !IsBridgeInitialized())
	{
		OutReason = LOCTEXT("RefreshUnavailable", "物理桥未初始化或正在执行同步生命周期回调。");
		return false;
	}
	if (!HasSessionLifecycleOwnership())
	{
		OutReason = LOCTEXT("SessionNotReady", "请先初始化角色的 Assembly Session；物理阶段只能在会话接管并冻结角色移动后启用。");
		return false;
	}
	if (!ResolvePhysicsLifecycleProvider(OutReason)) { return false; }
	const FAssemblyPhysicsContext Binding = GetPhysicsContext();
	UObject* Provider = GetPhysicsLifecycleProvider();
	const bool bPlaying = BoundAssembly->GetPhase() == EAssemblyPhase::Playing;
	if (bPlaying && !PrepareForPlaying(OutReason)) { return false; }
	ApplyPhysicsPhase();
	if (!IsLifecycleBindingCurrent(Binding, Provider) || bLifecycleShutdown)
	{
		OutReason = LOCTEXT("RefreshCanceled", "恢复物理生命周期期间绑定已被关闭或替换。");
		return false;
	}
	return ReplayHeldPhysicsIntents(Binding, OutReason);
}

bool UAssemblyPhysicsBridgeComponent::ReplayHeldPhysicsIntents(
	const FAssemblyPhysicsContext& Binding, FText& OutReason)
{
	auto IsReady = [this, &Binding]()
	{
		return IsCurrentContext(Binding) && !bLifecycleShutdown && !bLifecycleBusy
			&& !bClearBridgePending && !bLifecycleShutdownPending;
	};
	if (!IsReady())
	{
		OutReason = LOCTEXT("ReplayCanceled", "恢复按住的输入期间物理会话已关闭或替换。");
		return false;
	}
	if (BoundAssembly->GetPhase() == EAssemblyPhase::Playing)
	{
		const TArray<FAssemblyMuscleInstance> Muscles = BoundAssembly->GetMuscles();
		for (const FAssemblyMuscleInstance& Muscle : Muscles)
		{
			if (!IsReady()) { break; }
			if (!Muscle.bContracting) { continue; }
			const FAssemblyPhysicsMuscleDriveMessage* Active = ActiveMuscleRequests.Find(Muscle.InstanceId);
			// A healthy repeated initialization needs no duplicate intent; replaced bodies need a fresh payload.
			const bool bAlreadyForwarded = Active && Active->Phase == EAssemblyPhase::Playing
				&& Active->EndpointBoneA.PoseSource == BoundAssembly->GetBonePoseSource(Muscle.EndpointA.BoneId)
				&& Active->EndpointBoneB.PoseSource == BoundAssembly->GetBonePoseSource(Muscle.EndpointB.BoneId);
			if (!bAlreadyForwarded) { HandleMuscleDriveRequested(Muscle.InstanceId, true); }
		}
	}
	if (IsReady() && BoundAssembly->IsCoreRotationHeld() && !bCoreWasHeld) { HandleCoreRotationRequested(true); }
	if (!IsReady())
	{
		OutReason = LOCTEXT("ReplayCanceled", "恢复按住的输入期间物理会话已关闭或替换。");
		return false;
	}
	return true;
}

void UAssemblyPhysicsBridgeComponent::ShutdownPhysicsLifecycle()
{
	if (bLifecycleBusy) { bLifecycleShutdownPending = true; return; }
	if (bLifecycleShutdown) { return; }
	// An explicitly assigned provider is not active until this bridge has bound a model.
	if (!bInitialized && !LastLifecycleSnapshot.Context.SourceId.IsValid() && !bHasPreparedPlaying)
	{
		bLifecycleShutdown = true;
		return;
	}
	ON_SCOPE_EXIT { FlushDeferredLifecycleActions(); };
	TGuardValue<bool> Guard(bLifecycleBusy, true);
	FAssemblyPhysicsSnapshotMessage Snapshot;
	if (!GetPhysicsSnapshot(Snapshot)) { Snapshot = LastLifecycleSnapshot; }
	const TWeakObjectPtr<UObject> Provider = PhysicsLifecycleProvider;
	// Mark before callbacks, so release is idempotent even when a listener requests shutdown again.
	bLifecycleShutdown = true;
	ReleaseHeldPhysicsIntents();
	AbortPreparedState();
	BeforePreparationSnapshot = FAssemblyPhysicsSnapshotMessage();
	FAssemblyPhysicsSnapshotMessage AfterAbort;
	if (GetPhysicsSnapshot(AfterAbort) && AfterAbort.Context.SourceId == Snapshot.Context.SourceId
		&& AfterAbort.Context.RoundGeneration == Snapshot.Context.RoundGeneration) { Snapshot = MoveTemp(AfterAbort); }
	if (Provider.IsValid())
	{
		Snapshot.bClearPhysicalState = true;
		IAssemblyPhysicsLifecycle::Execute_ShutdownPhysics(Provider.Get(), Snapshot);
	}
	PendingJointConfirmations.Reset();
	OutstandingJointBreaks.Reset();
}

void UAssemblyPhysicsBridgeComponent::FlushDeferredLifecycleActions()
{
	if (bLifecycleBusy || bClearing) { return; }
	if (bAbortPlayingPending)
	{
		bAbortPlayingPending = false;
		AbortPreparedPlaying();
	}
	if (bLifecycleShutdownPending)
	{
		bLifecycleShutdownPending = false;
		ShutdownPhysicsLifecycle();
	}
	if (bClearBridgePending)
	{
		bClearBridgePending = false;
		ClearBridge();
	}
}

bool UAssemblyPhysicsBridgeComponent::IsBridgeInitialized() const
{
	return bInitialized && !bClearing && BoundAssembly.IsValid() && MessageBus.IsValid()
		&& BoundAssembly->IsInitialized() && !BoundAssembly->IsEndingPlay() && BoundAssembly->GetWorld() == GetWorld();
}

bool UAssemblyPhysicsBridgeComponent::InitializeBridge(UAssemblyComponent* Assembly,
	UPrimitiveComponent* InCoreBody, FText& OutReason)
{
	OutReason = FText::GetEmpty();
	if (bClearing || bLifecycleBusy)
	{
		OutReason = LOCTEXT("Clearing", "物理桥正在清理，请在通知回调结束后重新初始化。");
		return false;
	}
	if (!IsValid(Assembly) || !Assembly->IsInitialized() || !GetWorld() || Assembly->GetWorld() != GetWorld())
	{
		OutReason = LOCTEXT("InvalidAssembly", "物理桥需要同一世界中已经初始化的组装组件。");
		return false;
	}
	if (InCoreBody && (!IsValid(InCoreBody) || InCoreBody->GetWorld() != GetWorld()))
	{
		OutReason = LOCTEXT("InvalidCoreBody", "核心物理体必须属于当前组装组件所在的世界。");
		return false;
	}
	UJGDMessageSubsystem* Bus = UJGDMessageSubsystem::Get(this);
	if (!Bus)
	{
		OutReason = LOCTEXT("MissingBus", "当前世界没有 JGD GameInstance 消息总线。");
		return false;
	}
	if (IsBridgeInitialized() && BoundAssembly.Get() == Assembly && MessageBus.Get() == Bus)
	{
		if (!HasSessionLifecycleOwnership())
		{
			const FGuid BindingId = SourceId;
			ShutdownPhysicsLifecycle();
			if (!IsBridgeInitialized() || SourceId != BindingId)
			{
				OutReason = LOCTEXT("BindingChangedDuringInit", "消息回调清理或替换了物理桥。");
				return false;
			}
			SetCoreBody(InCoreBody);
			bLifecycleShutdown = true;
			return true;
		}
		SetCoreBody(InCoreBody);
		return RefreshPhysicsLifecycle(OutReason);
	}
	for (auto It = ActiveBridges.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid() || !It.Value().IsValid())
		{
			It.RemoveCurrent();
		}
	}
	if (const TWeakObjectPtr<UAssemblyPhysicsBridgeComponent>* Existing = ActiveBridges.Find(Assembly))
	{
		if (Existing->IsValid() && Existing->Get() != this && Existing->Get()->IsBridgeInitialized())
		{
			OutReason = LOCTEXT("DuplicateBridge", "该组装组件已有活动物理桥；不能重复发送物理请求。");
			return false;
		}
	}
	ClearBridge();
	BoundAssembly = Assembly;
	CoreBody = InCoreBody;
	MessageBus = Bus;
	SourceId = FGuid::NewGuid();
	RoundGeneration = 1;
	Revision = 1;
	bInitialized = true;
	bLifecycleShutdown = false;
	ActiveBridges.Add(Assembly, this);
	if (!Assembly->RegisterLifecycleCallbacks(this,
		FAssemblyLifecycleCheck::CreateUObject(this, &ThisClass::PrepareForPlaying),
		FAssemblyLifecycleCheck::CreateUObject(this, &ThisClass::StopBeforeReset),
		FSimpleDelegate::CreateUObject(this, &ThisClass::AbortPreparedPlaying),
		FSimpleDelegate::CreateUObject(this, &ThisClass::ShutdownPhysicsLifecycle), OutReason))
	{
		ClearBridge();
		return false;
	}
	Assembly->OnAssemblyChanged.AddUniqueDynamic(this, &ThisClass::HandleAssemblyChanged);
	Assembly->OnAssemblyReset.AddUniqueDynamic(this, &ThisClass::HandleAssemblyReset);
	Assembly->OnPhaseChanged.AddUniqueDynamic(this, &ThisClass::HandlePhaseChanged);
	Assembly->OnMuscleDriveRequested.AddUniqueDynamic(this, &ThisClass::HandleMuscleDriveRequested);
	Assembly->OnCoreRotationRequested.AddUniqueDynamic(this, &ThisClass::HandleCoreRotationRequested);
	Assembly->OnJointBreakRequested.AddUniqueDynamic(this, &ThisClass::HandleJointBreakRequested);
	FJGDMessageReceived Receiver;
	Receiver.BindDynamic(this, &ThisClass::HandlePhysicsJointBroken);
	JointBrokenListener = Bus->RegisterListener(AssemblyMessageTags::JointBroken, Receiver);
	SetComponentTickEnabled(true);
	bRebuildPending = true;
	const FGuid BindingId = SourceId;
	BroadcastSnapshot(AssemblyMessageTags::AssemblyChanged);
	if (!IsBridgeInitialized() || SourceId != BindingId)
	{
		OutReason = LOCTEXT("BindingChangedDuringInit", "消息回调清理或替换了物理桥。");
		return false;
	}
	if (!HasSessionLifecycleOwnership())
	{
		// Discover without invoking the executor: Session needs this information to freeze its proxy.
		bLifecycleShutdown = true;
		FText DiscoveryReason;
		ResolvePhysicsLifecycleProvider(DiscoveryReason);
		// Required/ambiguous providers are checked when Session activates the lifecycle, not by legacy BeginPlay.
		return true;
	}
	if (!RefreshPhysicsLifecycle(OutReason))
	{
		ClearBridge();
		return false;
	}
	// Refresh also replays model intents after preparing/applying the physical phase.
	return IsBridgeInitialized() && SourceId == BindingId;
}

void UAssemblyPhysicsBridgeComponent::UnbindModel()
{
	if (UAssemblyComponent* Assembly = BoundAssembly.Get())
	{
		Assembly->UnregisterLifecycleCallbacksIfMatches(this);
		Assembly->OnAssemblyChanged.RemoveDynamic(this, &ThisClass::HandleAssemblyChanged);
		Assembly->OnAssemblyReset.RemoveDynamic(this, &ThisClass::HandleAssemblyReset);
		Assembly->OnPhaseChanged.RemoveDynamic(this, &ThisClass::HandlePhaseChanged);
		Assembly->OnMuscleDriveRequested.RemoveDynamic(this, &ThisClass::HandleMuscleDriveRequested);
		Assembly->OnCoreRotationRequested.RemoveDynamic(this, &ThisClass::HandleCoreRotationRequested);
		Assembly->OnJointBreakRequested.RemoveDynamic(this, &ThisClass::HandleJointBreakRequested);
		if (ActiveBridges.FindRef(Assembly).Get() == this)
		{
			ActiveBridges.Remove(Assembly);
		}
	}
	if (UJGDMessageSubsystem* Bus = MessageBus.Get())
	{
		Bus->UnregisterListener(JointBrokenListener);
	}
	else
	{
		JointBrokenListener.Invalidate();
	}
}

void UAssemblyPhysicsBridgeComponent::ClearBridge()
{
	if (bLifecycleBusy)
	{
		bClearBridgePending = true;
		bLifecycleShutdownPending = true;
		return;
	}
	if (bClearing)
	{
		return;
	}
	TGuardValue<bool> Guard(bClearing, true);
	UJGDMessageSubsystem* Bus = MessageBus.Get();
	FAssemblyPhysicsSnapshotMessage Reset;
	const bool bHadBinding = bInitialized && GetPhysicsSnapshot(Reset);
	Reset.bClearPhysicalState = true;
	// Preserve the old bodies until the executor has synchronously released their constraints.
	ShutdownPhysicsLifecycle();
	FAssemblyPhysicsCoreRotationMessage CoreStop;
	CoreStop.Context = GetPhysicsContext();
	CoreStop.Phase = Reset.Phase;
	CoreStop.CoreBody = CoreBody.Get();
	CoreStop.FrameTransform = Reset.FrameTransform;
	CoreStop.WorldClockwiseAxis = Reset.FrameTransform.TransformVectorNoScale(FVector(0, 1, 0)).GetSafeNormal();
	const bool bReleaseCore = bCoreWasHeld;
	TMap<FGuid, FAssemblyPhysicsMuscleDriveMessage> Stops = MoveTemp(ActiveMuscleRequests);
	UnbindModel();
	bInitialized = false;
	bRebuildPending = false;
	bCoreWasHeld = false;
	PendingJointConfirmations.Reset();
	OutstandingJointBreaks.Reset();
	SetComponentTickEnabled(false);
	if (Bus)
	{
		for (auto& Pair : Stops)
		{
			Pair.Value.bContracting = false;
			Pair.Value.Muscle.bContracting = false;
			Bus->BroadcastMessage(AssemblyMessageTags::MuscleDriveRequested, FInstancedStruct::Make(Pair.Value));
		}
		if (bReleaseCore)
		{
			Bus->BroadcastMessage(AssemblyMessageTags::CoreRotationRequested, FInstancedStruct::Make(CoreStop));
		}
		if (bHadBinding)
		{
			Bus->BroadcastMessage(AssemblyMessageTags::AssemblyReset, FInstancedStruct::Make(Reset));
		}
	}
	BoundAssembly.Reset();
	CoreBody.Reset();
	MessageBus.Reset();
	SourceId.Invalidate();
	RoundGeneration = 0;
	Revision = 0;
	bHasPreparedPlaying = false;
	PreparedProvider.Reset();
	PreparedSnapshot = FAssemblyPhysicsSnapshotMessage();
	BeforePreparationSnapshot = FAssemblyPhysicsSnapshotMessage();
	LastLifecycleSnapshot = FAssemblyPhysicsSnapshotMessage();
	bLifecycleShutdownPending = false;
	bClearBridgePending = false;
	bAbortPlayingPending = false;
}

void UAssemblyPhysicsBridgeComponent::SetCoreBody(UPrimitiveComponent* InCoreBody)
{
	if (bClearing || (InCoreBody && (!IsValid(InCoreBody) || InCoreBody->GetWorld() != GetWorld()))
		|| CoreBody.Get() == InCoreBody)
	{
		return;
	}
	const bool bHeld = bCoreWasHeld;
	const FGuid BindingId = SourceId;
	if (bHeld)
	{
		BroadcastCoreRequest(false);
	}
	if (bInitialized && (!IsBridgeInitialized() || SourceId != BindingId))
	{
		return;
	}
	CoreBody = InCoreBody;
	RequestPhysicsRebuild();
	if (bHeld && IsBridgeInitialized() && !bLifecycleBusy && !bLifecycleShutdown)
	{
		BroadcastCoreRequest(true);
	}
}

FAssemblyPhysicsContext UAssemblyPhysicsBridgeComponent::GetPhysicsContext() const
{
	FAssemblyPhysicsContext Context;
	Context.Assembly = BoundAssembly.Get();
	Context.World = GetWorld();
	Context.SourceId = SourceId;
	Context.RoundGeneration = RoundGeneration;
	Context.Revision = Revision;
	return Context;
}

FAssemblyPhysicsBonePose UAssemblyPhysicsBridgeComponent::MakeBonePose(FGuid BoneId) const
{
	FAssemblyPhysicsBonePose Pose;
	Pose.BoneId = BoneId;
	UAssemblyComponent* Assembly = BoundAssembly.Get();
	if (!BoneId.IsValid())
	{
		Pose.PoseSource = CoreBody.Get();
		Pose.Body = CoreBody.Get();
		Pose.WorldTransform = CoreBody.IsValid() ? CoreBody->GetComponentTransform()
			: (Assembly ? Assembly->GetGridFrameTransform() : FTransform::Identity);
	}
	else if (Assembly)
	{
		Pose.PoseSource = Assembly->GetBonePoseSource(BoneId);
		Pose.Body = Cast<UPrimitiveComponent>(Pose.PoseSource.Get());
		Assembly->GetBoneWorldTransform(BoneId, Pose.WorldTransform);
	}
	return Pose;
}

bool UAssemblyPhysicsBridgeComponent::GetPhysicsSnapshot(FAssemblyPhysicsSnapshotMessage& OutSnapshot) const
{
	OutSnapshot = FAssemblyPhysicsSnapshotMessage();
	UAssemblyComponent* Assembly = BoundAssembly.Get();
	// ClearBridge captures a final snapshot while its reentrancy guard is already set.
	if (!bInitialized || !IsValid(Assembly) || !Assembly->IsInitialized() || Assembly->GetWorld() != GetWorld())
	{
		return false;
	}
	OutSnapshot.Context = GetPhysicsContext();
	OutSnapshot.Phase = Assembly->GetPhase();
	OutSnapshot.PlacementMode = Assembly->GetPlacementMode();
	OutSnapshot.FrameTransform = Assembly->GetGridFrameTransform();
	OutSnapshot.CoreBody = CoreBody.Get();
	OutSnapshot.FreeSettings = Assembly->GetFreeSettings();
	OutSnapshot.ReferenceGrid = Assembly->GetGridSettings();
	OutSnapshot.BoneDefinitions = Assembly->GetBoneDefinitions();
	OutSnapshot.MuscleDefinitions = Assembly->GetMuscleDefinitions();
	OutSnapshot.Bones = Assembly->GetBones();
	OutSnapshot.Muscles = Assembly->GetMuscles();
	OutSnapshot.Joints = Assembly->GetJoints();
	for (const FAssemblyBoneInstance& Bone : OutSnapshot.Bones)
	{
		OutSnapshot.BonePoses.Add(MakeBonePose(Bone.InstanceId));
	}
	return true;
}

void UAssemblyPhysicsBridgeComponent::BroadcastSnapshot(FGameplayTag Channel, bool bClearState)
{
	FAssemblyPhysicsSnapshotMessage Snapshot;
	if (IsBridgeInitialized() && GetPhysicsSnapshot(Snapshot))
	{
		Snapshot.bClearPhysicalState = bClearState;
		MessageBus->BroadcastMessage(Channel, FInstancedStruct::Make(Snapshot));
	}
}

void UAssemblyPhysicsBridgeComponent::RequestPhysicsRebuild()
{
	if (IsBridgeInitialized())
	{
		if (!bRebuildPending)
		{
			++Revision;
		}
		bRebuildPending = true;
	}
}

void UAssemblyPhysicsBridgeComponent::HandleAssemblyChanged()
{
	if (IsBridgeInitialized())
	{
		const FAssemblyPhysicsContext Binding = GetPhysicsContext();
		// A manually prepared structure becomes stale as soon as the player edits the assembly.
		if (BoundAssembly->GetPhase() == EAssemblyPhase::Assembly && bHasPreparedPlaying) { AbortPreparedPlaying(); }
		if (!IsCurrentContext(Binding)) { return; }
		++Revision;
		bRebuildPending = true;
		BroadcastSnapshot(AssemblyMessageTags::AssemblyChanged);
	}
}

void UAssemblyPhysicsBridgeComponent::HandleAssemblyReset()
{
	if (IsBridgeInitialized())
	{
		if (!HasSessionLifecycleOwnership()) { bLifecycleShutdown = true; }
		++RoundGeneration;
		++Revision;
		PendingJointConfirmations.Reset();
		OutstandingJointBreaks.Reset();
		bHasPreparedPlaying = false;
		PreparedProvider.Reset();
		PreparedSnapshot = FAssemblyPhysicsSnapshotMessage();
		BeforePreparationSnapshot = FAssemblyPhysicsSnapshotMessage();
		// Model ResetRound releases held inputs before emitting Reset.
		ActiveMuscleRequests.Reset();
		bCoreWasHeld = false;
		bRebuildPending = true;
		BroadcastSnapshot(AssemblyMessageTags::AssemblyReset, true);
	}
}

void UAssemblyPhysicsBridgeComponent::HandlePhaseChanged(EAssemblyPhase Phase)
{
	if (IsBridgeInitialized())
	{
		const FAssemblyPhysicsContext Binding = GetPhysicsContext();
		ApplyPhysicsPhase();
		if (!IsCurrentContext(Binding)) { return; }
		++Revision;
		bRebuildPending = true;
		BroadcastSnapshot(AssemblyMessageTags::AssemblyPhaseChanged);
		// Preserve a held Space across the transition, now with the committed physical phase.
		if (!bLifecycleShutdown && HasPhysicsLifecycleProvider() && IsCurrentContext(Binding) && BoundAssembly->IsCoreRotationHeld())
		{
			BroadcastCoreRequest(true);
		}
	}
}

void UAssemblyPhysicsBridgeComponent::BroadcastMuscleRequest(const FAssemblyPhysicsMuscleDriveMessage& Request)
{
	if (IsBridgeInitialized())
	{
		MessageBus->BroadcastMessage(AssemblyMessageTags::MuscleDriveRequested, FInstancedStruct::Make(Request));
	}
}

void UAssemblyPhysicsBridgeComponent::HandleMuscleDriveRequested(FGuid MuscleId, bool bContracting)
{
	if (!IsBridgeInitialized())
	{
		return;
	}
	if (bContracting && (bLifecycleBusy || bLifecycleShutdown || BoundAssembly->GetPhase() != EAssemblyPhase::Playing)) { return; }
	FAssemblyPhysicsMuscleDriveMessage Request;
	if (!BoundAssembly->GetMuscle(MuscleId, Request.Muscle))
	{
		return;
	}
	Request.Context = GetPhysicsContext();
	Request.Phase = BoundAssembly->GetPhase();
	Request.bContracting = bContracting;
	Request.Muscle.bContracting = bContracting;
	Request.CoreBody = CoreBody.Get();
	Request.FrameTransform = BoundAssembly->GetGridFrameTransform();
	Request.EndpointBoneA = MakeBonePose(Request.Muscle.EndpointA.BoneId);
	Request.EndpointBoneB = MakeBonePose(Request.Muscle.EndpointB.BoneId);
	BoundAssembly->GetEndpointWorldPosition(Request.Muscle.EndpointA, Request.EndpointWorldA);
	BoundAssembly->GetEndpointWorldPosition(Request.Muscle.EndpointB, Request.EndpointWorldB);
	if (bContracting)
	{
		ActiveMuscleRequests.Add(MuscleId, Request);
	}
	else
	{
		ActiveMuscleRequests.Remove(MuscleId);
	}
	BroadcastMuscleRequest(Request);
}

void UAssemblyPhysicsBridgeComponent::BroadcastCoreRequest(bool bHeld)
{
	if (!IsBridgeInitialized())
	{
		return;
	}
	if (bHeld && (bLifecycleBusy || bLifecycleShutdown)) { return; }
	FAssemblyPhysicsCoreRotationMessage Request;
	Request.Context = GetPhysicsContext();
	Request.Phase = BoundAssembly->GetPhase();
	Request.bHeld = bHeld;
	Request.CoreBody = CoreBody.Get();
	Request.FrameTransform = BoundAssembly->GetGridFrameTransform();
	Request.WorldClockwiseAxis = Request.FrameTransform.TransformVectorNoScale(FVector(0, 1, 0)).GetSafeNormal();
	bCoreWasHeld = bHeld;
	MessageBus->BroadcastMessage(AssemblyMessageTags::CoreRotationRequested, FInstancedStruct::Make(Request));
}

void UAssemblyPhysicsBridgeComponent::HandleCoreRotationRequested(bool bHeld)
{
	BroadcastCoreRequest(bHeld);
}

void UAssemblyPhysicsBridgeComponent::HandleJointBreakRequested(FGuid JointId)
{
	if (!IsBridgeInitialized())
	{
		return;
	}
	const TArray<FAssemblyJointInstance> Joints = BoundAssembly->GetJoints();
	const FAssemblyJointInstance* Joint = Joints.FindByPredicate([JointId](const FAssemblyJointInstance& Value)
		{ return Value.InstanceId == JointId; });
	if (!Joint)
	{
		return;
	}
	FAssemblyPhysicsJointBreakMessage Request;
	Request.Context = GetPhysicsContext();
	Request.Joint = *Joint;
	Request.CoreBody = CoreBody.Get();
	Request.BodyA = MakeBonePose(Joint->BoneA);
	Request.BodyB = MakeBonePose(Joint->BoneB);
	OutstandingJointBreaks.Add(JointId);
	MessageBus->BroadcastMessage(AssemblyMessageTags::JointBreakRequested, FInstancedStruct::Make(Request));
}

bool UAssemblyPhysicsBridgeComponent::IsCurrentContext(const FAssemblyPhysicsContext& Context) const
{
	return IsBridgeInitialized() && Context.Assembly == BoundAssembly.Get() && Context.World == GetWorld()
		&& Context.SourceId == SourceId && Context.RoundGeneration == RoundGeneration;
}

void UAssemblyPhysicsBridgeComponent::HandlePhysicsJointBroken(FGameplayTag Channel, const FInstancedStruct& Message)
{
	const FAssemblyPhysicsJointBrokenMessage* Confirmation = Message.GetPtr<FAssemblyPhysicsJointBrokenMessage>();
	if (Channel != AssemblyMessageTags::JointBroken || !Confirmation || !IsCurrentContext(Confirmation->Context)
		|| BoundAssembly->GetPhase() != EAssemblyPhase::Playing || !Confirmation->JointId.IsValid()
		|| !OutstandingJointBreaks.Contains(Confirmation->JointId))
	{
		return;
	}
	if (!PendingJointConfirmations.ContainsByPredicate([Confirmation](const FAssemblyPhysicsJointBrokenMessage& Value)
		{ return Value.JointId == Confirmation->JointId; }))
	{
		PendingJointConfirmations.Add(*Confirmation);
	}
}

void UAssemblyPhysicsBridgeComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!IsBridgeInitialized())
	{
		if (bInitialized)
		{
			ClearBridge();
		}
		return;
	}
	TArray<FAssemblyPhysicsJointBrokenMessage> Confirmations = MoveTemp(PendingJointConfirmations);
	for (const FAssemblyPhysicsJointBrokenMessage& Confirmation : Confirmations)
	{
		if (!IsCurrentContext(Confirmation.Context) || BoundAssembly->GetPhase() != EAssemblyPhase::Playing
			|| !OutstandingJointBreaks.Contains(Confirmation.JointId))
		{
			continue;
		}
		const TArray<FAssemblyJointInstance> Joints = BoundAssembly->GetJoints();
		if (Joints.ContainsByPredicate([&Confirmation](const FAssemblyJointInstance& Joint)
			{ return Joint.InstanceId == Confirmation.JointId; }))
		{
			FText Reason;
			if (BoundAssembly->ConfirmJointBroken(Confirmation.JointId, Reason))
			{
				OutstandingJointBreaks.Remove(Confirmation.JointId);
			}
		}
	}
	if (IsBridgeInitialized() && bRebuildPending)
	{
		bRebuildPending = false;
		BroadcastSnapshot(AssemblyMessageTags::PhysicsRebuildRequested);
	}
}

void UAssemblyPhysicsBridgeComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearBridge();
	Super::EndPlay(EndPlayReason);
}

void UAssemblyPhysicsBridgeComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	ClearBridge();
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

#undef LOCTEXT_NAMESPACE
