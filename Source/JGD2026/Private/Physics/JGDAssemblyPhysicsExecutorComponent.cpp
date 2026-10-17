#include "Physics/JGDAssemblyPhysicsExecutorComponent.h"

#include "Assembly/AssemblyComponent.h"
#include "Assembly/AssemblyPhysicsBridgeComponent.h"
#include "Assembly/AssemblySessionComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "MessageBus/JGDMessageSubsystem.h"
#include "Physics/JGDPhysicsMessages.h"
#include "Physics/JGDPhysicsWorldSubsystem.h"
#include "Physics/JGDPhysicsProfiles.h"
#include "Physics/JGDPhysicsSystemSettings.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"

#define LOCTEXT_NAMESPACE "JGDAssemblyPhysicsExecutor"

UJGDAssemblyPhysicsExecutorComponent::UJGDAssemblyPhysicsExecutorComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

bool UJGDAssemblyPhysicsExecutorComponent::InitializeExecutor(
	UAssemblyPhysicsBridgeComponent* InBridge, UAssemblySessionComponent* InSession, FText& OutReason)
{
	OutReason = FText::GetEmpty();
	AActor* Owner = GetOwner();
	if (!IsValid(Owner) || !IsValid(InBridge) || !IsValid(InSession)
		|| InBridge->GetOwner() != Owner || InSession->GetOwner() != Owner
		|| InBridge->GetWorld() != GetWorld() || InSession->GetWorld() != GetWorld())
	{
		OutReason = LOCTEXT("InvalidLifecycleOwner", "Assembly Physics Executor、Bridge 和 Session 必须属于同一 Owner 与 World。");
		return false;
	}
	if (bInitialized && !bShuttingDown && Bridge.Get() == InBridge && Session.Get() == InSession
		&& MessageBus.IsValid() && CoreRotationListener.IsValid()
		&& InBridge->GetPhysicsLifecycleProvider() == this)
	{
		return true;
	}
	if (bPlaying || bPrepared)
	{
		OutReason = LOCTEXT("ReinitializeWhileActive", "物理执行器在准备或模拟期间不能更换 Bridge/Session 绑定。");
		return false;
	}
	if (UObject* ExistingProvider = InBridge->GetPhysicsLifecycleProvider(); ExistingProvider && ExistingProvider != this)
	{
		OutReason = LOCTEXT("DifferentProviderAlreadyBound", "Physics Bridge 已绑定其他生命周期执行器；不会自动替换其所有者。");
		return false;
	}
	if (!IsValid(DriveProfile.Get()))
	{
		DriveProfile = GetDefault<UJGDPhysicsSystemSettings>()->DefaultAssemblyDriveProfile.LoadSynchronous();
	}
	if (!IsValid(DriveProfile.Get()) || !DriveProfile->HasValidParameters())
	{
		OutReason = LOCTEXT("InvalidDriveProfile", "物理执行器需要有效的 Assembly 驱动 DataAsset；请检查项目默认配置或组件覆盖。");
		return false;
	}
	UnregisterMessageListeners();
	Bridge = InBridge;
	Session = InSession;
	bShuttingDown = false;
	MessageBus = UJGDMessageSubsystem::Get(this);
	if (!MessageBus.IsValid())
	{
		OutReason = LOCTEXT("MissingBus", "Assembly Physics Executor 找不到消息总线。");
		return false;
	}

	FJGDMessageReceived Receiver;
	Receiver.BindDynamic(this, &ThisClass::HandleSnapshotMessage);
	SnapshotListeners[0] = MessageBus->RegisterListener(AssemblyMessageTags::AssemblyChanged, Receiver);
	SnapshotListeners[1] = MessageBus->RegisterListener(AssemblyMessageTags::AssemblyReset, Receiver);
	SnapshotListeners[2] = MessageBus->RegisterListener(AssemblyMessageTags::AssemblyPhaseChanged, Receiver);
	SnapshotListeners[3] = MessageBus->RegisterListener(AssemblyMessageTags::PhysicsRebuildRequested, Receiver);
	Receiver.BindDynamic(this, &ThisClass::HandleMuscleDriveMessage);
	MuscleListener = MessageBus->RegisterListener(AssemblyMessageTags::MuscleDriveRequested, Receiver);
	Receiver.BindDynamic(this, &ThisClass::HandleCoreRotationMessage);
	CoreRotationListener = MessageBus->RegisterListener(AssemblyMessageTags::CoreRotationRequested, Receiver);
	Receiver.BindDynamic(this, &ThisClass::HandleJointBreakMessage);
	JointBreakListener = MessageBus->RegisterListener(AssemblyMessageTags::JointBreakRequested, Receiver);
	bInitialized = true;
	FAssemblyPhysicsSnapshotMessage Snapshot;
	if (InBridge->GetPhysicsSnapshot(Snapshot))
	{
		if (Snapshot.Phase != EAssemblyPhase::Assembly)
		{
			OutReason = LOCTEXT("InitializeDuringPlaying", "Assembly Physics Executor 必须在模型处于 Assembly 时首次绑定。");
			UnregisterMessageListeners();
			bInitialized = false;
			MessageBus.Reset();
			return false;
		}
		ActiveSnapshot = Snapshot;
		ApplyAssemblyState();
	}
	if (!InBridge->SetPhysicsLifecycleProvider(this, OutReason))
	{
		UnregisterMessageListeners();
		bInitialized = false;
		MessageBus.Reset();
		return false;
	}
	return true;
}

void UJGDAssemblyPhysicsExecutorComponent::BeginPlay()
{
	Super::BeginPlay();
	AActor* Owner = GetOwner();
	if (!IsValid(Owner))
	{
		UE_LOG(LogTemp, Error, TEXT("Assembly Physics Executor has no valid owning actor."));
		return;
	}
	TArray<UAssemblyPhysicsBridgeComponent*> Bridges;
	TArray<UAssemblySessionComponent*> Sessions;
	Owner->GetComponents(Bridges);
	Owner->GetComponents(Sessions);
	if (Bridges.Num() != 1 || Sessions.Num() != 1)
	{
		UE_LOG(LogTemp, Error, TEXT("Assembly Physics Executor requires exactly one Bridge and one Session on its owner; found %d and %d."),
			Bridges.Num(), Sessions.Num());
		return;
	}
	FText Reason;
	if (!InitializeExecutor(Bridges[0], Sessions[0], Reason))
	{
		UE_LOG(LogTemp, Error, TEXT("Assembly Physics Executor initialization failed: %s"), *Reason.ToString());
	}
}

bool UJGDAssemblyPhysicsExecutorComponent::IsCurrentContext(const FAssemblyPhysicsContext& Context) const
{
	return Context.Assembly == ActiveSnapshot.Context.Assembly
		&& Context.World == GetWorld()
		&& Context.SourceId == ActiveSnapshot.Context.SourceId
		&& Context.RoundGeneration == ActiveSnapshot.Context.RoundGeneration;
}

bool UJGDAssemblyPhysicsExecutorComponent::IsCurrentLifecycleBinding(
	const FAssemblyPhysicsContext& Context, bool bAllowNewerRevision) const
{
	const UAssemblyPhysicsBridgeComponent* ActiveBridge = Bridge.Get();
	const UAssemblySessionComponent* ActiveSession = Session.Get();
	if (!IsValid(ActiveBridge) || !IsValid(ActiveSession) || !ActiveBridge->IsBridgeInitialized()
		|| ActiveBridge->GetPhysicsLifecycleProvider() != this || Context.Assembly != ActiveSnapshot.Context.Assembly
		|| Context.World != GetWorld() || Context.SourceId != ActiveSnapshot.Context.SourceId
		|| Context.RoundGeneration != ActiveSnapshot.Context.RoundGeneration
		|| !ActiveSession->CanOwnPhysicsLifecycle(Context.Assembly))
	{
		return false;
	}
	const FAssemblyPhysicsContext Current = ActiveBridge->GetPhysicsContext();
	return Current.Assembly == Context.Assembly && Current.World == Context.World
		&& Current.SourceId == Context.SourceId && Current.RoundGeneration == Context.RoundGeneration
		&& (bAllowNewerRevision ? Current.Revision >= Context.Revision : Current.Revision == Context.Revision);
}

bool UJGDAssemblyPhysicsExecutorComponent::IsProviderBoundToBridge() const
{
	const UAssemblyPhysicsBridgeComponent* ActiveBridge = Bridge.Get();
	return IsValid(ActiveBridge) && ActiveBridge->IsBridgeInitialized()
		&& ActiveBridge->GetPhysicsLifecycleProvider() == this;
}

bool UJGDAssemblyPhysicsExecutorComponent::CaptureAndValidateBodies(
	const FAssemblyPhysicsSnapshotMessage& Snapshot, FText& OutReason)
{
	OutReason = FText::GetEmpty();
	if (Snapshot.Context.World != GetWorld() || Snapshot.Context.Assembly == nullptr
		|| Snapshot.Context.Assembly->GetWorld() != GetWorld() || Snapshot.Phase != EAssemblyPhase::Assembly
		|| !IsValid(Snapshot.CoreBody) || Snapshot.CoreBody->GetWorld() != GetWorld() || !Snapshot.CoreBody->IsRegistered())
	{
		OutReason = LOCTEXT("InvalidCore", "Playing 准备需要当前 World 中已注册的核心刚体和 Assembly 快照。");
		return false;
	}
	TSet<UPrimitiveComponent*> UniqueBodies;
	TArray<UPrimitiveComponent*> Bodies;
	Bodies.Add(Snapshot.CoreBody);
	UniqueBodies.Add(Snapshot.CoreBody);
	for (const FAssemblyPhysicsBonePose& Pose : Snapshot.BonePoses)
	{
		if (!IsValid(Pose.Body) || Pose.Body->GetWorld() != GetWorld() || !Pose.Body->IsRegistered()
			|| !Pose.BoneId.IsValid() || UniqueBodies.Contains(Pose.Body))
		{
			OutReason = LOCTEXT("InvalidPartBody", "每个已安装部件必须提供唯一、有效且属于当前 World 的刚体。");
			return false;
		}
		UniqueBodies.Add(Pose.Body);
		Bodies.Add(Pose.Body);
	}
	if (Snapshot.Bones.Num() != Snapshot.BonePoses.Num())
	{
		OutReason = LOCTEXT("MissingPartPose", "已安装部件与物理姿态源数量不一致。");
		return false;
	}

	for (UPrimitiveComponent* Body : Bodies)
	{
		CaptureBodyState(Body);
		if (Body->IsSimulatingPhysics())
		{
			OutReason = LOCTEXT("AssemblyBodyAlreadySimulating", "Assembly 阶段的核心和部件刚体必须处于非模拟状态。");
			return false;
		}
	}
	return true;
}

void UJGDAssemblyPhysicsExecutorComponent::CaptureBodyState(UPrimitiveComponent* Body)
{
	if (!IsValid(Body) || BodyStates.Contains(Body))
	{
		return;
	}
	FBodyState State;
	State.Body = Body;
	State.AttachParent = Body->GetAttachParent();
	State.AttachSocket = Body->GetAttachSocketName();
	State.WorldTransform = Body->GetComponentTransform();
	State.RelativeTransform = Body->GetRelativeTransform();
	State.LinearVelocity = Body->GetPhysicsLinearVelocity();
	State.AngularVelocity = Body->GetPhysicsAngularVelocityInDegrees();
	State.CollisionProfile = Body->GetCollisionProfileName();
	State.CollisionEnabled = Body->GetCollisionEnabled();
	State.CollisionObjectType = Body->GetCollisionObjectType();
	State.CollisionResponses = Body->GetCollisionResponseToChannels();
	State.bSimulate = Body->IsSimulatingPhysics();
	State.bGravity = Body->IsGravityEnabled();
	State.bGenerateOverlaps = Body->GetGenerateOverlapEvents();
	State.LinearDamping = Body->GetLinearDamping();
	State.AngularDamping = Body->GetAngularDamping();
	if (FBodyInstance* BodyInstance = Body->GetBodyInstance())
	{
		State.DOFMode = BodyInstance->DOFMode;
		State.CustomDOFPlaneNormal = BodyInstance->CustomDOFPlaneNormal;
		State.bHasDOFMode = true;
	}
	BodyStates.Add(Body, State);
}

void UJGDAssemblyPhysicsExecutorComponent::RestoreBodyAttachment(
	UPrimitiveComponent* Body, const FBodyState& State, bool bRestoreTransform)
{
	if (!IsValid(Body))
	{
		return;
	}
	if (USceneComponent* Parent = State.AttachParent.Get())
	{
		if (Body->GetAttachParent() != Parent || Body->GetAttachSocketName() != State.AttachSocket)
		{
			Body->AttachToComponent(Parent, FAttachmentTransformRules::KeepWorldTransform, State.AttachSocket);
		}
		if (bRestoreTransform)
		{
			Body->SetRelativeTransform(State.RelativeTransform, false, nullptr, ETeleportType::TeleportPhysics);
		}
	}
	else
	{
		if (Body->GetAttachParent())
		{
			Body->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
		}
		if (bRestoreTransform)
		{
			Body->SetWorldTransform(State.WorldTransform, false, nullptr, ETeleportType::TeleportPhysics);
		}
	}
}

void UJGDAssemblyPhysicsExecutorComponent::RestoreBodyState(
	UPrimitiveComponent* Body, const FBodyState& State, bool bRestoreTransform, bool bRestoreSimulation)
{
	if (!IsValid(Body))
	{
		return;
	}
	Body->SetSimulatePhysics(false);
	Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
	Body->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
	Body->SetEnableGravity(State.bGravity);
	Body->SetLinearDamping(State.LinearDamping);
	Body->SetAngularDamping(State.AngularDamping);
	Body->SetGenerateOverlapEvents(State.bGenerateOverlaps);
	if (State.bHasDOFMode)
	{
		if (FBodyInstance* BodyInstance = Body->GetBodyInstance())
		{
			BodyInstance->CustomDOFPlaneNormal = State.CustomDOFPlaneNormal;
		}
		Body->SetConstraintMode(State.DOFMode);
	}
	if (State.CollisionProfile != NAME_None)
	{
		Body->SetCollisionProfileName(State.CollisionProfile);
	}
	Body->SetCollisionObjectType(State.CollisionObjectType);
	Body->SetCollisionResponseToChannels(State.CollisionResponses);
	Body->SetCollisionEnabled(State.CollisionEnabled);
	RestoreBodyAttachment(Body, State, bRestoreTransform);
	if (bRestoreSimulation && State.bSimulate)
	{
		Body->SetSimulatePhysics(true);
		Body->SetPhysicsLinearVelocity(State.LinearVelocity);
		Body->SetPhysicsAngularVelocityInDegrees(State.AngularVelocity);
	}
}

bool UJGDAssemblyPhysicsExecutorComponent::BuildConstraints(
	const FAssemblyPhysicsSnapshotMessage& Snapshot, FText& OutReason)
{
	OutReason = FText::GetEmpty();
	DestroyConstraints();
	if (!IsValid(DriveProfile.Get()) || !DriveProfile->HasValidParameters())
	{
		OutReason = LOCTEXT("InvalidConstraintDriveProfile", "创建关节约束需要有效的 Assembly 驱动 DataAsset。");
		return false;
	}
	for (const FAssemblyJointInstance& Joint : Snapshot.Joints)
	{
		UPrimitiveComponent* BodyA = ResolveBody(Joint.BoneA);
		UPrimitiveComponent* BodyB = ResolveBody(Joint.BoneB);
		if (!IsValid(BodyA) || !IsValid(BodyB) || BodyA == BodyB || !Joint.InstanceId.IsValid())
		{
			OutReason = LOCTEXT("InvalidJointEndpoint", "关节必须连接两个不同且有效的核心/部件刚体。");
			DestroyConstraints();
			return false;
		}

		UPhysicsConstraintComponent* Constraint = NewObject<UPhysicsConstraintComponent>(GetOwner());
		if (!Constraint)
		{
			OutReason = LOCTEXT("ConstraintCreateFailed", "无法创建运行时关节约束。");
			DestroyConstraints();
			return false;
		}
		GetOwner()->AddInstanceComponent(Constraint);
		Constraint->RegisterComponent();
		const FVector AnchorA = ResolveAnchorWorld(Joint.BoneA, Joint.LocalAnchorA);
		const FVector AnchorB = ResolveAnchorWorld(Joint.BoneB, Joint.LocalAnchorB);
		Constraint->SetWorldLocation((AnchorA + AnchorB) * 0.5f);
		const bool bHinge = Joint.Kind == EAssemblyJointKind::Hinge || Joint.Kind == EAssemblyJointKind::BreakableHinge;
		if (bHinge)
		{
			// The assembly is authored in XZ; align the constraint's local twist (X) axis to world Y.
			Constraint->SetWorldRotation(FQuat(FVector::ZAxisVector, HALF_PI).Rotator());
		}
		Constraint->SetConstrainedComponents(BodyA, NAME_None, BodyB, NAME_None);
		Constraint->SetDisableCollision(true);
		Constraint->SetLinearXLimit(ELinearConstraintMotion::LCM_Locked, 0.0f);
		Constraint->SetLinearYLimit(ELinearConstraintMotion::LCM_Locked, 0.0f);
		Constraint->SetLinearZLimit(ELinearConstraintMotion::LCM_Locked, 0.0f);
		Constraint->SetAngularSwing1Limit(EAngularConstraintMotion::ACM_Locked, 0.0f);
		Constraint->SetAngularSwing2Limit(EAngularConstraintMotion::ACM_Locked, 0.0f);
		Constraint->SetAngularTwistLimit(bHinge ? EAngularConstraintMotion::ACM_Limited : EAngularConstraintMotion::ACM_Locked,
			bHinge ? FMath::Abs(Joint.MaxAngleDegrees - Joint.MinAngleDegrees) * 0.5f : 0.0f);
		const bool bBreakable = Joint.Kind == EAssemblyJointKind::BreakableHinge || Joint.Kind == EAssemblyJointKind::BreakableFixed;
		Constraint->SetLinearBreakable(bBreakable, DriveProfile->JointBreakForce);
		Constraint->SetAngularBreakable(bBreakable, DriveProfile->JointBreakTorque);
		Constraint->SetComponentTickEnabled(false);
		Constraints.Add(Joint.InstanceId, Constraint);
	}
	return true;
}

UPrimitiveComponent* UJGDAssemblyPhysicsExecutorComponent::ResolveBody(FGuid BoneId) const
{
	if (!BoneId.IsValid())
	{
		return ActiveSnapshot.CoreBody;
	}
	const FAssemblyPhysicsBonePose* Pose = ActiveSnapshot.BonePoses.FindByPredicate(
		[BoneId](const FAssemblyPhysicsBonePose& Item) { return Item.BoneId == BoneId; });
	return Pose ? Pose->Body : nullptr;
}

FVector UJGDAssemblyPhysicsExecutorComponent::ResolveAnchorWorld(FGuid BoneId, FVector2D LocalAnchor) const
{
	UPrimitiveComponent* Body = ResolveBody(BoneId);
	return LocalPointToWorld(Body, LocalAnchor);
}

FVector UJGDAssemblyPhysicsExecutorComponent::LocalPointToWorld(const UPrimitiveComponent* Body, FVector2D LocalPoint)
{
	return IsValid(Body) ? Body->GetComponentTransform().TransformPosition(FVector(LocalPoint.X, 0.0f, LocalPoint.Y)) : FVector::ZeroVector;
}

bool UJGDAssemblyPhysicsExecutorComponent::PrepareForPlaying_Implementation(
	const FAssemblyPhysicsSnapshotMessage& Snapshot, FText& OutReason)
{
	OutReason = FText::GetEmpty();
	if (bShuttingDown || Snapshot.Phase != EAssemblyPhase::Assembly || bPlaying)
	{
		OutReason = LOCTEXT("WrongPrepareState", "物理执行器当前状态不允许准备进入 Playing。");
		return false;
	}
	if (bPrepared && IsCurrentContext(Snapshot.Context))
	{
		return true;
	}
	ActiveSnapshot = Snapshot;
	UAssemblySessionComponent* ActiveSession = Session.Get();
	if (!bInitialized || !ActiveSession || !IsCurrentLifecycleBinding(Snapshot.Context, true))
	{
		OutReason = LOCTEXT("InvalidSessionBinding", "物理准备需要当前 Bridge、Session 和唯一执行器绑定仍然有效。");
		return false;
	}
	if (!IsValid(DriveProfile.Get()) || !DriveProfile->HasValidParameters())
	{
		OutReason = LOCTEXT("InvalidPrepareDriveProfile", "进入 Playing 前 Assembly 驱动 DataAsset 已失效或含有非法参数。");
		return false;
	}
	if (!ActiveSession->ValidatePhysicsProxyReady(Snapshot.Context.Assembly, Snapshot, OutReason))
	{
		return false;
	}
	ClearCoreRotationDrive();
	if (!CaptureAndValidateBodies(Snapshot, OutReason) || !BuildConstraints(Snapshot, OutReason))
	{
		RollbackPreparedState(false);
		return false;
	}
	bPrepared = true;
	return true;
}

bool UJGDAssemblyPhysicsExecutorComponent::ApplyAssemblyPhase_Implementation(
	const FAssemblyPhysicsSnapshotMessage& Snapshot, FText& OutReason)
{
	OutReason = FText::GetEmpty();
	if (bShuttingDown || !bInitialized || !IsCurrentLifecycleBinding(Snapshot.Context, true))
	{
		OutReason = LOCTEXT("LifecycleBindingLost", "阶段切换期间 Bridge、Session 或物理执行器绑定已失效。");
		return false;
	}
	UAssemblySessionComponent* ActiveSession = Session.Get();
	if (!ActiveSession || !ActiveSession->ValidatePhysicsProxyReady(Snapshot.Context.Assembly, Snapshot, OutReason))
	{
		return false;
	}

	if (Snapshot.Phase == EAssemblyPhase::Assembly)
	{
		ClearCoreRotationDrive();
		ActiveSnapshot = Snapshot;
		RollbackPreparedState(false);
		ActiveSnapshot = Snapshot;
		ApplyAssemblyState();
		return true;
	}

	if (Snapshot.Phase != EAssemblyPhase::Playing || !bPrepared
		|| !IsCurrentContext(Snapshot.Context)
		|| Snapshot.CoreBody != ActiveSnapshot.CoreBody
		|| Snapshot.BonePoses.Num() != ActiveSnapshot.BonePoses.Num()
		|| Snapshot.Joints.Num() != ActiveSnapshot.Joints.Num())
	{
		OutReason = LOCTEXT("NotPrepared", "没有匹配的 Prepared Assembly 刚体与约束可提交为 Playing。");
		return false;
	}
	for (const FAssemblyPhysicsBonePose& PreparedPose : ActiveSnapshot.BonePoses)
	{
		const FAssemblyPhysicsBonePose* CurrentPose = Snapshot.BonePoses.FindByPredicate(
			[&PreparedPose](const FAssemblyPhysicsBonePose& Candidate) { return Candidate.BoneId == PreparedPose.BoneId; });
		if (!CurrentPose || CurrentPose->Body != PreparedPose.Body || CurrentPose->PoseSource != PreparedPose.PoseSource)
		{
			OutReason = LOCTEXT("PreparedBodyChanged", "Playing 提交时通用刚体或姿态源与准备阶段不一致。");
			return false;
		}
	}
	for (const FAssemblyJointInstance& PreparedJoint : ActiveSnapshot.Joints)
	{
		if (!Snapshot.Joints.ContainsByPredicate([&PreparedJoint](const FAssemblyJointInstance& Candidate)
			{ return Candidate.InstanceId == PreparedJoint.InstanceId && Candidate.Kind == PreparedJoint.Kind
				&& Candidate.BoneA == PreparedJoint.BoneA && Candidate.BoneB == PreparedJoint.BoneB
				&& Candidate.LocalAnchorA.Equals(PreparedJoint.LocalAnchorA, UE_KINDA_SMALL_NUMBER)
				&& Candidate.LocalAnchorB.Equals(PreparedJoint.LocalAnchorB, UE_KINDA_SMALL_NUMBER)
				&& FMath::IsNearlyEqual(Candidate.MinAngleDegrees, PreparedJoint.MinAngleDegrees)
				&& FMath::IsNearlyEqual(Candidate.MaxAngleDegrees, PreparedJoint.MaxAngleDegrees); }))
		{
			OutReason = LOCTEXT("PreparedJointChanged", "Playing 提交时关节边、锚点或限角与准备阶段不一致。");
			return false;
		}
	}

	UJGDPhysicsWorldSubsystem* PhysicsSubsystem = GetWorld()->GetSubsystem<UJGDPhysicsWorldSubsystem>();
	if (!PhysicsSubsystem)
	{
		OutReason = LOCTEXT("MissingWorldSubsystem", "当前 World 缺少刚体 Profile Subsystem。");
		return false;
	}

	TArray<UPrimitiveComponent*> Bodies;
	Bodies.Add(Snapshot.CoreBody);
	for (const FAssemblyPhysicsBonePose& Pose : Snapshot.BonePoses)
	{
		Bodies.Add(Pose.Body);
	}
	for (UPrimitiveComponent* Body : Bodies)
	{
		FText RegisterReason;
		if (!PhysicsSubsystem->RegisterRigidBody(Body, Snapshot.Context.SourceId, BaseRigidBodyProfile, RegisterReason))
		{
			OutReason = RegisterReason.IsEmpty() ? LOCTEXT("RegisterFailed", "无法注册 Assembly 刚体。"): RegisterReason;
			RollbackPreparedState(false);
			return false;
		}
		RegisteredBodies.Add(Body);
	}

	for (UPrimitiveComponent* Body : Bodies)
	{
		Body->SetCollisionProfileName(TEXT("PhysicsActor"));
		Body->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Body->SetGenerateOverlapEvents(true);
		Body->SetConstraintMode(EDOFMode::XZPlane);
		Body->SetSimulatePhysics(true);
		if (!Body->IsSimulatingPhysics())
		{
			OutReason = LOCTEXT("SimulationStartFailed", "至少一个 Assembly 刚体无法启动物理模拟。");
			RollbackPreparedState(false);
			return false;
		}
	}
	for (TPair<FGuid, TObjectPtr<UPhysicsConstraintComponent>>& Pair : Constraints)
	{
		if (UPhysicsConstraintComponent* Constraint = Pair.Value.Get())
		{
			Constraint->SetComponentTickEnabled(true);
			Constraint->InitComponentConstraint();
			if (!Constraint->ConstraintInstance.IsValidConstraintInstance())
			{
				OutReason = LOCTEXT("ConstraintInitFailed", "至少一个组装关节未能创建物理约束状态。Playing 提交已回滚。");
				RollbackPreparedState(false);
				return false;
			}
		}
	}
	ActiveSnapshot = Snapshot;
	ClearCoreRotationDrive();
	MuscleDrives.Reset();
	bPlaying = true;
	bPrepared = false;
	SetComponentTickEnabled(true);
	if (!IsCurrentLifecycleBinding(Snapshot.Context, true)
		|| !ActiveSession->ValidatePhysicsProxyReady(Snapshot.Context.Assembly, Snapshot, OutReason))
	{
		if (OutReason.IsEmpty())
		{
			OutReason = LOCTEXT("PlayingCommitCanceled", "刚体启用后物理绑定或角色代理保护已失效。");
		}
		RollbackPreparedState(false);
		return false;
	}
	return true;
}

void UJGDAssemblyPhysicsExecutorComponent::AbortPreparedPlaying_Implementation(
	const FAssemblyPhysicsSnapshotMessage& Snapshot)
{
	if (Snapshot.Context.World != GetWorld() || Snapshot.Context.Assembly != ActiveSnapshot.Context.Assembly
		|| Snapshot.Context.SourceId != ActiveSnapshot.Context.SourceId
		|| Snapshot.Context.RoundGeneration != ActiveSnapshot.Context.RoundGeneration)
	{
		return;
	}
	ClearCoreRotationDrive();
	RollbackPreparedState(false);
	if (IsValid(Snapshot.Context.Assembly))
	{
		FAssemblyPhysicsSnapshotMessage CurrentSnapshot;
		if (Bridge.IsValid() && Bridge->GetPhysicsSnapshot(CurrentSnapshot))
		{
			ActiveSnapshot = CurrentSnapshot;
			ApplyAssemblyState();
		}
	}
}

bool UJGDAssemblyPhysicsExecutorComponent::StopBeforeReset_Implementation(
	const FAssemblyPhysicsSnapshotMessage& Snapshot, FText& OutReason)
{
	OutReason = FText::GetEmpty();
	if (!IsCurrentLifecycleBinding(Snapshot.Context, true))
	{
		OutReason = LOCTEXT("ResetBindingLost", "重开前物理绑定已失效，不能确认旧刚体已安全停止。");
		return false;
	}
	ClearCoreRotationDrive();
	ActiveSnapshot = Snapshot;
	RollbackPreparedState(false);
	ApplyAssemblyState();
	for (TPair<TWeakObjectPtr<UPrimitiveComponent>, FBodyState>& Pair : BodyStates)
	{
		UPrimitiveComponent* Body = Pair.Key.Get();
		if (IsValid(Body) && (Body->IsSimulatingPhysics() || Body->IsGravityEnabled()))
		{
			OutReason = LOCTEXT("ResetBodyNotStopped", "至少一个旧刚体仍在模拟或受重力。");
			return false;
		}
	}
	if (!RegisteredBodies.IsEmpty() || !Constraints.IsEmpty() || CoreRotationInput != 0.0f || !CoreRotationWorldAxis.IsNearlyZero()
		|| !MuscleDrives.IsEmpty())
	{
		OutReason = LOCTEXT("ResetResourcesNotStopped", "重开前仍有物理驱动或运行时约束未清理。");
		return false;
	}
	return true;
}

void UJGDAssemblyPhysicsExecutorComponent::RollbackPreparedState(bool bRestoreOriginalState)
{
	ClearCoreRotationDrive();
	MuscleDrives.Reset();
	MuscleRestLengths.Reset();
	ReportedSameBodyMuscles.Reset();
	UJGDPhysicsWorldSubsystem* PhysicsSubsystem = GetWorld() ? GetWorld()->GetSubsystem<UJGDPhysicsWorldSubsystem>() : nullptr;
	if (PhysicsSubsystem)
	{
		PhysicsSubsystem->UnregisterRigidBodiesOwnedBy(ActiveSnapshot.Context.SourceId);
	}
	RegisteredBodies.Reset();
	for (TPair<TWeakObjectPtr<UPrimitiveComponent>, FBodyState>& Pair : BodyStates)
	{
		UPrimitiveComponent* Body = Pair.Key.Get();
		if (!IsValid(Body))
		{
			continue;
		}
		if (bRestoreOriginalState)
		{
			RestoreBodyState(Body, Pair.Value, true, true);
			continue;
		}
		Body->SetSimulatePhysics(false);
		Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
		Body->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
		Body->SetEnableGravity(false);
		Body->SetLinearDamping(0.0f);
		Body->SetAngularDamping(0.0f);
		Body->SetGenerateOverlapEvents(false);
		if (Pair.Value.bHasDOFMode)
		{
			if (FBodyInstance* BodyInstance = Body->GetBodyInstance())
			{
				BodyInstance->CustomDOFPlaneNormal = Pair.Value.CustomDOFPlaneNormal;
			}
			Body->SetConstraintMode(Pair.Value.DOFMode);
		}
		if (Pair.Value.CollisionProfile != NAME_None)
		{
			Body->SetCollisionProfileName(Pair.Value.CollisionProfile);
		}
		Body->SetCollisionObjectType(Pair.Value.CollisionObjectType);
		Body->SetCollisionResponseToChannels(Pair.Value.CollisionResponses);
		Body->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		RestoreBodyAttachment(Body, Pair.Value, true);
	}
	DestroyConstraints();
	bPrepared = false;
	bPlaying = false;
	SetComponentTickEnabled(false);
}

void UJGDAssemblyPhysicsExecutorComponent::ApplyAssemblyState()
{
	ClearCoreRotationDrive();
	ReconcileAssemblyBodies(ActiveSnapshot);
	MuscleDrives.Reset();
	MuscleRestLengths.Reset();
	ReportedSameBodyMuscles.Reset();
	for (TPair<TWeakObjectPtr<UPrimitiveComponent>, FBodyState>& Pair : BodyStates)
	{
		if (UPrimitiveComponent* Body = Pair.Key.Get())
		{
			Body->SetSimulatePhysics(false);
			Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
			Body->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
			Body->SetEnableGravity(false);
			Body->SetLinearDamping(0.0f);
			Body->SetAngularDamping(0.0f);
			Body->SetGenerateOverlapEvents(false);
			if (Pair.Value.bHasDOFMode)
			{
				if (FBodyInstance* BodyInstance = Body->GetBodyInstance())
				{
					BodyInstance->CustomDOFPlaneNormal = Pair.Value.CustomDOFPlaneNormal;
				}
				Body->SetConstraintMode(Pair.Value.DOFMode);
			}
			Body->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		}
	}
}

void UJGDAssemblyPhysicsExecutorComponent::ReconcileAssemblyBodies(const FAssemblyPhysicsSnapshotMessage& Snapshot)
{
	TSet<TWeakObjectPtr<UPrimitiveComponent>> ActiveBodies;
	if (IsValid(Snapshot.CoreBody))
	{
		ActiveBodies.Add(Snapshot.CoreBody);
	}
	for (const FAssemblyPhysicsBonePose& Pose : Snapshot.BonePoses)
	{
		if (IsValid(Pose.Body))
		{
			ActiveBodies.Add(Pose.Body);
		}
	}

	TArray<TWeakObjectPtr<UPrimitiveComponent>> RemovedBodies;
	for (TPair<TWeakObjectPtr<UPrimitiveComponent>, FBodyState>& Pair : BodyStates)
	{
		UPrimitiveComponent* Body = Pair.Key.Get();
		if (!IsValid(Body) || !ActiveBodies.Contains(Pair.Key))
		{
			if (IsValid(Body))
			{
				// A detached body is no longer owned by this assembly; restore its entry state
				// without teleporting it back to an obsolete assembly pose.
				RestoreBodyState(Body, Pair.Value, false, true);
			}
			RemovedBodies.Add(Pair.Key);
			continue;
		}
		Pair.Value.WorldTransform = Body->GetComponentTransform();
		Pair.Value.RelativeTransform = Body->GetRelativeTransform();
		Pair.Value.AttachParent = Body->GetAttachParent();
		Pair.Value.AttachSocket = Body->GetAttachSocketName();
	}
	for (const TWeakObjectPtr<UPrimitiveComponent>& Body : RemovedBodies)
	{
		BodyStates.Remove(Body);
	}

	CaptureBodyState(Snapshot.CoreBody);
	for (const FAssemblyPhysicsBonePose& Pose : Snapshot.BonePoses)
	{
		CaptureBodyState(Pose.Body);
	}
	for (TPair<TWeakObjectPtr<UPrimitiveComponent>, FBodyState>& Pair : BodyStates)
	{
		if (UPrimitiveComponent* Body = Pair.Key.Get(); IsValid(Body))
		{
			Pair.Value.WorldTransform = Body->GetComponentTransform();
			Pair.Value.RelativeTransform = Body->GetRelativeTransform();
			Pair.Value.AttachParent = Body->GetAttachParent();
			Pair.Value.AttachSocket = Body->GetAttachSocketName();
		}
	}
}

void UJGDAssemblyPhysicsExecutorComponent::HandleSnapshotMessage(FGameplayTag Channel, const FInstancedStruct& Message)
{
	if (Channel != AssemblyMessageTags::AssemblyChanged && Channel != AssemblyMessageTags::AssemblyReset
		&& Channel != AssemblyMessageTags::AssemblyPhaseChanged && Channel != AssemblyMessageTags::PhysicsRebuildRequested)
	{
		return;
	}
	const FAssemblyPhysicsSnapshotMessage* Snapshot = Message.GetPtr<FAssemblyPhysicsSnapshotMessage>();
	if (!Snapshot || Snapshot->Context.World != GetWorld() || !IsValid(Snapshot->Context.Assembly)
		|| Snapshot->Context.Assembly->GetWorld() != GetWorld())
	{
		return;
	}
	if (ActiveSnapshot.Context.SourceId.IsValid() && Snapshot->Context.SourceId != ActiveSnapshot.Context.SourceId)
	{
		return;
	}
	if (Snapshot->Context.RoundGeneration < ActiveSnapshot.Context.RoundGeneration
		|| (Snapshot->Context.RoundGeneration == ActiveSnapshot.Context.RoundGeneration
			&& Snapshot->Context.Revision < ActiveSnapshot.Context.Revision))
	{
		return;
	}
	const bool bNewRound = Snapshot->Context.RoundGeneration > ActiveSnapshot.Context.RoundGeneration;
	if (bNewRound)
	{
		ClearCoreRotationDrive();
		MuscleDrives.Reset();
		MuscleRestLengths.Reset();
		ReportedSameBodyMuscles.Reset();
	}
	if (Snapshot->Phase == EAssemblyPhase::Assembly)
	{
		ClearCoreRotationDrive();
		ActiveSnapshot = *Snapshot;
		if (bPlaying || bPrepared || !RegisteredBodies.IsEmpty() || !Constraints.IsEmpty())
		{
			RollbackPreparedState(false);
		}
		ApplyAssemblyState();
		return;
	}
	ActiveSnapshot = *Snapshot;
}

void UJGDAssemblyPhysicsExecutorComponent::HandleMuscleDriveMessage(FGameplayTag Channel, const FInstancedStruct& Message)
{
	if (Channel != AssemblyMessageTags::MuscleDriveRequested)
	{
		return;
	}
	const FAssemblyPhysicsMuscleDriveMessage* Request = Message.GetPtr<FAssemblyPhysicsMuscleDriveMessage>();
	if (!Request || !bPlaying || ActiveSnapshot.Phase != EAssemblyPhase::Playing || !IsCurrentContext(Request->Context))
	{
		return;
	}
	if (!Request->bContracting)
	{
		MuscleDrives.Remove(Request->Muscle.InstanceId);
		return;
	}
	UPrimitiveComponent* BodyA = Request->EndpointBoneA.Body;
	UPrimitiveComponent* BodyB = Request->EndpointBoneB.Body;
	if (!IsValid(BodyA) || !IsValid(BodyB))
	{
		return;
	}
	if (BodyA == BodyB)
	{
		if (!ReportedSameBodyMuscles.Contains(Request->Muscle.InstanceId))
		{
			ReportedSameBodyMuscles.Add(Request->Muscle.InstanceId);
			UE_LOG(LogTemp, Warning, TEXT("Muscle %s resolves to one rigid body; drive is intentionally a no-op."),
				*Request->Muscle.InstanceId.ToString());
		}
		return;
	}
	FMuscleDrive Drive;
	Drive.BodyA = BodyA;
	Drive.BodyB = BodyB;
	Drive.LocalA = Request->Muscle.EndpointA.BoneLocalPoint;
	Drive.LocalB = Request->Muscle.EndpointB.BoneLocalPoint;
	Drive.RestLength = MuscleRestLengths.FindRef(Request->Muscle.InstanceId);
	if (Drive.RestLength <= UE_SMALL_NUMBER)
	{
		Drive.RestLength = FVector::Distance(Request->EndpointWorldA, Request->EndpointWorldB);
		MuscleRestLengths.Add(Request->Muscle.InstanceId, Drive.RestLength);
	}
	Drive.bContracting = true;
	MuscleDrives.Add(Request->Muscle.InstanceId, Drive);
}

void UJGDAssemblyPhysicsExecutorComponent::HandleCoreRotationMessage(
	FGameplayTag Channel, const FInstancedStruct& Message)
{
	if (Channel != AssemblyMessageTags::CoreRotationRequested)
	{
		return;
	}
	const FAssemblyPhysicsCoreRotationMessage* Request = Message.GetPtr<FAssemblyPhysicsCoreRotationMessage>();
	if (!Request)
	{
		return;
	}

	auto SameIdentity = [](const FAssemblyPhysicsContext& A, const FAssemblyPhysicsContext& B)
	{
		return A.World == B.World && A.Assembly == B.Assembly && A.SourceId == B.SourceId
			&& A.RoundGeneration == B.RoundGeneration;
	};

	if (!Request->bHeld)
	{
		if (!bCoreRotationDriveContextValid || !SameIdentity(Request->Context, CoreRotationDriveContext)
			|| Request->Context.Revision < LastAcceptedCoreDriveRevision)
		{
			return;
		}
		ClearCoreRotationDrive();
		return;
	}

	if (Request->Context.World != GetWorld() || Request->Context.Assembly != ActiveSnapshot.Context.Assembly
		|| Request->Context.SourceId != ActiveSnapshot.Context.SourceId
		|| Request->Context.RoundGeneration != ActiveSnapshot.Context.RoundGeneration)
	{
		return;
	}

	if (Request->Phase != EAssemblyPhase::Playing || ActiveSnapshot.Phase != EAssemblyPhase::Playing || !bPlaying)
	{
		return;
	}
	if (Request->Context.Revision != ActiveSnapshot.Context.Revision
		|| Request->Context.Revision < LastAcceptedCoreDriveRevision
		|| !IsCurrentLifecycleBinding(Request->Context, false))
	{
		return;
	}
	UAssemblySessionComponent* ActiveSession = Session.Get();
	FText ProxyReason;
	if (!ActiveSession || !ActiveSession->ValidatePhysicsProxyReady(
		ActiveSnapshot.Context.Assembly, ActiveSnapshot, ProxyReason))
	{
		ClearCoreRotationDrive();
		return;
	}
	if (!IsValid(Request->CoreBody) || Request->CoreBody != ActiveSnapshot.CoreBody
		|| Request->CoreBody->GetWorld() != GetWorld() || !Request->CoreBody->IsRegistered()
		|| !Request->CoreBody->IsSimulatingPhysics())
	{
		ClearCoreRotationDrive();
		UE_LOG(LogTemp, Warning, TEXT("Rejected core rotation input because the active core body is invalid or not simulating."));
		return;
	}
	const FVector Axis = Request->WorldClockwiseAxis;
	if (!FMath::IsFinite(Axis.X) || !FMath::IsFinite(Axis.Y) || !FMath::IsFinite(Axis.Z)
		|| !FMath::IsFinite(Axis.SizeSquared()) || Axis.SizeSquared() <= FMath::Square(UE_SMALL_NUMBER))
	{
		ClearCoreRotationDrive();
		UE_LOG(LogTemp, Warning, TEXT("Rejected core rotation input because WorldClockwiseAxis is zero or non-finite."));
		return;
	}
	const FVector NormalizedAxis = Axis.GetSafeNormal();
	if (NormalizedAxis.IsNearlyZero() || NormalizedAxis.ContainsNaN())
	{
		ClearCoreRotationDrive();
		UE_LOG(LogTemp, Warning, TEXT("Rejected core rotation input because WorldClockwiseAxis could not be normalized."));
		return;
	}

	CoreRotationDriveContext = Request->Context;
	bCoreRotationDriveContextValid = true;
	LastAcceptedCoreDriveRevision = Request->Context.Revision;
	CoreRotationWorldAxis = NormalizedAxis;
	SetCoreRotationDriveInput(1.0f);
}

void UJGDAssemblyPhysicsExecutorComponent::HandleJointBreakMessage(FGameplayTag Channel, const FInstancedStruct& Message)
{
	if (Channel != AssemblyMessageTags::JointBreakRequested)
	{
		return;
	}
	const FAssemblyPhysicsJointBreakMessage* Request = Message.GetPtr<FAssemblyPhysicsJointBreakMessage>();
	if (!Request || !bPlaying || !IsCurrentContext(Request->Context))
	{
		return;
	}
	BreakJoint(Request->Joint.InstanceId);
}

void UJGDAssemblyPhysicsExecutorComponent::BreakJoint(FGuid JointId)
{
	if (!bPlaying || ReportedBrokenJoints.Contains(JointId))
	{
		return;
	}
	TObjectPtr<UPhysicsConstraintComponent>* Found = Constraints.Find(JointId);
	if (!Found || !IsValid(Found->Get()))
	{
		return;
	}
	if (UPhysicsConstraintComponent* Constraint = Found->Get())
	{
		Constraint->BreakConstraint();
		Constraint->DestroyComponent();
	}
	Constraints.Remove(JointId);
	ReportedBrokenJoints.Add(JointId);
	if (UJGDMessageSubsystem* Bus = MessageBus.Get())
	{
		FAssemblyPhysicsJointBrokenMessage Confirmation;
		Confirmation.Context = ActiveSnapshot.Context;
		Confirmation.JointId = JointId;
		Bus->BroadcastMessage(AssemblyMessageTags::JointBroken, FInstancedStruct::Make(Confirmation));
	}
}

void UJGDAssemblyPhysicsExecutorComponent::DestroyConstraints()
{
	for (TPair<FGuid, TObjectPtr<UPhysicsConstraintComponent>>& Pair : Constraints)
	{
		if (IsValid(Pair.Value.Get()))
		{
			Pair.Value->BreakConstraint();
			Pair.Value->DestroyComponent();
		}
	}
	Constraints.Reset();
	ReportedBrokenJoints.Reset();
}

void UJGDAssemblyPhysicsExecutorComponent::SetCoreRotationDriveInput(float Input)
{
	const float SafeInput = bPlaying && FMath::IsFinite(Input) ? FMath::Clamp(Input, -1.0f, 1.0f) : 0.0f;
	if (FMath::IsNearlyZero(SafeInput))
	{
		CoreRotationInput = 0.0f;
		CoreRotationWorldAxis = FVector::ZeroVector;
		bCoreRotationDriveContextValid = false;
		CoreRotationDriveContext = FAssemblyPhysicsContext();
		LastAcceptedCoreDriveRevision = INDEX_NONE;
		return;
	}
	CoreRotationInput = SafeInput;
}

void UJGDAssemblyPhysicsExecutorComponent::ClearCoreRotationDrive()
{
	CoreRotationInput = 0.0f;
	CoreRotationWorldAxis = FVector::ZeroVector;
	CoreRotationDriveContext = FAssemblyPhysicsContext();
	LastAcceptedCoreDriveRevision = INDEX_NONE;
	bCoreRotationDriveContextValid = false;
}

void UJGDAssemblyPhysicsExecutorComponent::ApplyCoreRotationDrive(float DeltaSeconds)
{
	(void)DeltaSeconds;
	if (!bPlaying || ActiveSnapshot.Phase != EAssemblyPhase::Playing || !bCoreRotationDriveContextValid
		|| FMath::IsNearlyZero(CoreRotationInput))
	{
		return;
	}
	UAssemblySessionComponent* ActiveSession = Session.Get();
	FText ProxyReason;
	if (!IsCurrentContext(CoreRotationDriveContext) || !IsCurrentLifecycleBinding(ActiveSnapshot.Context, true)
		|| !ActiveSession || !ActiveSession->ValidatePhysicsProxyReady(
			ActiveSnapshot.Context.Assembly, ActiveSnapshot, ProxyReason)
		|| !IsValid(ActiveSnapshot.CoreBody) || ActiveSnapshot.CoreBody->GetWorld() != GetWorld()
		|| !ActiveSnapshot.CoreBody->IsRegistered() || !ActiveSnapshot.CoreBody->IsSimulatingPhysics()
		|| !IsValid(DriveProfile.Get()) || !DriveProfile->HasValidParameters()
		|| !FMath::IsFinite(CoreRotationWorldAxis.X) || !FMath::IsFinite(CoreRotationWorldAxis.Y)
		|| !FMath::IsFinite(CoreRotationWorldAxis.Z) || CoreRotationWorldAxis.IsNearlyZero())
	{
		ClearCoreRotationDrive();
		return;
	}
	ActiveSnapshot.CoreBody->AddTorqueInRadians(
		CoreRotationWorldAxis * CoreRotationInput * DriveProfile->CoreRotationTorque, NAME_None, false);
}

void UJGDAssemblyPhysicsExecutorComponent::ApplyMuscleDrives(float DeltaSeconds)
{
	(void)DeltaSeconds;
	if (!IsValid(DriveProfile.Get()) || !DriveProfile->HasValidParameters())
	{
		MuscleDrives.Reset();
		return;
	}
	for (TPair<FGuid, FMuscleDrive>& Pair : MuscleDrives)
	{
		FMuscleDrive& Drive = Pair.Value;
		UPrimitiveComponent* BodyA = Drive.BodyA.Get();
		UPrimitiveComponent* BodyB = Drive.BodyB.Get();
		if (!IsValid(BodyA) || !IsValid(BodyB) || BodyA == BodyB || !BodyA->IsSimulatingPhysics() || !BodyB->IsSimulatingPhysics())
		{
			continue;
		}
		const FVector PointA = LocalPointToWorld(BodyA, Drive.LocalA);
		const FVector PointB = LocalPointToWorld(BodyB, Drive.LocalB);
		FVector Direction = PointB - PointA;
		const float Length = Direction.Size();
		if (Length <= UE_SMALL_NUMBER || Drive.RestLength <= UE_SMALL_NUMBER)
		{
			continue;
		}
		Direction /= Length;
		const float TargetLength = Drive.RestLength * (1.0f - DriveProfile->MuscleContractionRatio);
		const float RelativeSpeed = FVector::DotProduct(
			BodyB->GetPhysicsLinearVelocityAtPoint(PointB) - BodyA->GetPhysicsLinearVelocityAtPoint(PointA), Direction);
		const float Magnitude = FMath::Clamp(DriveProfile->MuscleStiffness * (Length - TargetLength)
			+ DriveProfile->MuscleDamping * RelativeSpeed, 0.0f, DriveProfile->MuscleMaxForce);
		BodyA->AddForceAtLocation(Direction * Magnitude, PointA);
		BodyB->AddForceAtLocation(-Direction * Magnitude, PointB);
	}
}

void UJGDAssemblyPhysicsExecutorComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!bPlaying)
	{
		return;
	}
	ApplyMuscleDrives(DeltaTime);
	ApplyCoreRotationDrive(DeltaTime);
	TArray<FGuid> Broken;
	for (const TPair<FGuid, TObjectPtr<UPhysicsConstraintComponent>>& Pair : Constraints)
	{
		if (IsValid(Pair.Value.Get()) && Pair.Value->IsBroken())
		{
			Broken.Add(Pair.Key);
		}
	}
	for (const FGuid Id : Broken)
	{
		BreakJoint(Id);
	}
}

void UJGDAssemblyPhysicsExecutorComponent::ShutdownPhysics_Implementation(const FAssemblyPhysicsSnapshotMessage& Snapshot)
{
	ClearCoreRotationDrive();
	// Bridge shutdown is also used by Session::ShutdownSession and is therefore
	// reusable. bClearPhysicalState describes the Bridge notification, not the
	// lifetime of this provider component.
	RollbackPreparedState(false);
	ActiveSnapshot = Snapshot;
	bShuttingDown = false;
}

void UJGDAssemblyPhysicsExecutorComponent::ShutdownExecutor()
{
	if (bShuttingDown)
	{
		return;
	}
	FAssemblyPhysicsSnapshotMessage FinalSnapshot = ActiveSnapshot;
	FinalSnapshot.bClearPhysicalState = true;
	ShutdownPhysics_Implementation(FinalSnapshot);
	bShuttingDown = true;
	BodyStates.Reset();
	UnregisterMessageListeners();
	MessageBus.Reset();
	Bridge.Reset();
	Session.Reset();
	bInitialized = false;
}

void UJGDAssemblyPhysicsExecutorComponent::UnregisterMessageListeners()
{
	if (UJGDMessageSubsystem* Bus = MessageBus.Get())
	{
		for (FJGDMessageListenerHandle& Handle : SnapshotListeners)
		{
			Bus->UnregisterListener(Handle);
		}
		Bus->UnregisterListener(MuscleListener);
		Bus->UnregisterListener(CoreRotationListener);
		Bus->UnregisterListener(JointBreakListener);
	}
	for (FJGDMessageListenerHandle& Handle : SnapshotListeners)
	{
		Handle.Invalidate();
	}
	MuscleListener.Invalidate();
	CoreRotationListener.Invalidate();
	JointBreakListener.Invalidate();
}

void UJGDAssemblyPhysicsExecutorComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ShutdownExecutor();
	Super::EndPlay(EndPlayReason);
}

void UJGDAssemblyPhysicsExecutorComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	ShutdownExecutor();
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

#undef LOCTEXT_NAMESPACE
