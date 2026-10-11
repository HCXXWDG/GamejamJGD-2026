#include "Assembly/AssemblyPhysicsBridgeComponent.h"

#include "Assembly/AssemblyComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "MessageBus/JGDMessageSubsystem.h"

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

bool UAssemblyPhysicsBridgeComponent::IsBridgeInitialized() const
{
	return bInitialized && !bClearing && BoundAssembly.IsValid() && MessageBus.IsValid()
		&& BoundAssembly->IsInitialized() && BoundAssembly->GetWorld() == GetWorld();
}

bool UAssemblyPhysicsBridgeComponent::InitializeBridge(UAssemblyComponent* Assembly,
	UPrimitiveComponent* InCoreBody, FText& OutReason)
{
	OutReason = FText::GetEmpty();
	if (bClearing)
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
		SetCoreBody(InCoreBody);
		return true;
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
	ActiveBridges.Add(Assembly, this);
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
	// Binding during Playing must forward any inputs already held by the model.
	for (const FAssemblyMuscleInstance& Muscle : Assembly->GetMuscles())
	{
		if (!IsBridgeInitialized() || SourceId != BindingId)
		{
			return false;
		}
		if (Muscle.bContracting)
		{
			HandleMuscleDriveRequested(Muscle.InstanceId, true);
		}
	}
	if (IsBridgeInitialized() && SourceId == BindingId && Assembly->IsCoreRotationHeld())
	{
		HandleCoreRotationRequested(true);
	}
	return IsBridgeInitialized() && SourceId == BindingId;
}

void UAssemblyPhysicsBridgeComponent::UnbindModel()
{
	if (UAssemblyComponent* Assembly = BoundAssembly.Get())
	{
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
	if (bClearing)
	{
		return;
	}
	TGuardValue<bool> Guard(bClearing, true);
	UJGDMessageSubsystem* Bus = MessageBus.Get();
	FAssemblyPhysicsSnapshotMessage Reset;
	const bool bHadBinding = bInitialized && GetPhysicsSnapshot(Reset);
	Reset.bClearPhysicalState = true;
	FAssemblyPhysicsCoreRotationMessage CoreStop;
	CoreStop.Context = GetPhysicsContext();
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
	if (bHeld && IsBridgeInitialized())
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
		++Revision;
		bRebuildPending = true;
		BroadcastSnapshot(AssemblyMessageTags::AssemblyChanged);
	}
}

void UAssemblyPhysicsBridgeComponent::HandleAssemblyReset()
{
	if (IsBridgeInitialized())
	{
		++RoundGeneration;
		++Revision;
		PendingJointConfirmations.Reset();
		OutstandingJointBreaks.Reset();
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
		++Revision;
		bRebuildPending = true;
		BroadcastSnapshot(AssemblyMessageTags::AssemblyPhaseChanged);
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
	FAssemblyPhysicsMuscleDriveMessage Request;
	if (!BoundAssembly->GetMuscle(MuscleId, Request.Muscle))
	{
		return;
	}
	Request.Context = GetPhysicsContext();
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
	FAssemblyPhysicsCoreRotationMessage Request;
	Request.Context = GetPhysicsContext();
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
