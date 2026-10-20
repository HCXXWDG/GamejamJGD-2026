#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Assembly/AssemblyPhysicsLifecycle.h"
#include "Assembly/AssemblyMuscleRouting.h"
#include "MessageBus/JGDMessageBusTypes.h"
#include "PhysicsEngine/BodyInstance.h"
#include "UObject/StrongObjectPtr.h"
#include "JGDAssemblyPhysicsExecutorComponent.generated.h"

class UAssemblyPhysicsBridgeComponent;
class UAssemblySessionComponent;
class UJGDMessageSubsystem;
class UJGDPhysicsRigidBodyProfile;
class UJGDAssemblyPhysicsDriveProfile;
class UPrimitiveComponent;
class UPhysicsConstraintComponent;
class UPhysicalMaterial;
class USceneComponent;

/** Owns runtime constraints and drive state; borrows every core and part body from the assembly views. */
UCLASS(BlueprintType, ClassGroup = (Physics), meta = (BlueprintSpawnableComponent))
class JGD2026_API UJGDAssemblyPhysicsExecutorComponent : public UActorComponent, public IAssemblyPhysicsLifecycle
{
	GENERATED_BODY()

public:
	UJGDAssemblyPhysicsExecutorComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Physics")
	TObjectPtr<UJGDPhysicsRigidBodyProfile> BaseRigidBodyProfile;
	/** Optional per-executor override; runtime-created players load the project default when this is unset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Physics")
	TObjectPtr<UJGDAssemblyPhysicsDriveProfile> DriveProfile;

	bool InitializeExecutor(UAssemblyPhysicsBridgeComponent* InBridge, UAssemblySessionComponent* InSession, FText& OutReason);
	UFUNCTION(BlueprintCallable, Category = "Assembly|Physics|Core")
	void SetCoreRotationDriveInput(float Input);
	UFUNCTION(BlueprintCallable, Category = "Assembly|Physics|Core")
	void ApplyCoreRotationDrive(float DeltaSeconds);
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	virtual bool PrepareForPlaying_Implementation(const FAssemblyPhysicsSnapshotMessage& Snapshot, FText& OutReason) override;
	virtual void AbortPreparedPlaying_Implementation(const FAssemblyPhysicsSnapshotMessage& Snapshot) override;
	virtual bool StopBeforeReset_Implementation(const FAssemblyPhysicsSnapshotMessage& Snapshot, FText& OutReason) override;
	virtual bool ApplyAssemblyPhase_Implementation(const FAssemblyPhysicsSnapshotMessage& Snapshot, FText& OutReason) override;
	virtual void ShutdownPhysics_Implementation(const FAssemblyPhysicsSnapshotMessage& Snapshot) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;

private:
	friend struct FJGDAssemblyPhysicsExecutorTestAccessor;

	struct FBodyState
	{
		TWeakObjectPtr<UPrimitiveComponent> Body;
		TWeakObjectPtr<USceneComponent> AttachParent;
		FName AttachSocket = NAME_None;
		FTransform WorldTransform = FTransform::Identity;
		FTransform RelativeTransform = FTransform::Identity;
		FVector LinearVelocity = FVector::ZeroVector;
		FVector AngularVelocity = FVector::ZeroVector;
		FName CollisionProfile = NAME_None;
		TEnumAsByte<ECollisionEnabled::Type> CollisionEnabled = ECollisionEnabled::QueryOnly;
		TEnumAsByte<ECollisionChannel> CollisionObjectType = ECC_WorldDynamic;
		FCollisionResponseContainer CollisionResponses;
		TStrongObjectPtr<UPhysicalMaterial> OriginalPhysicalMaterialOverride;
		TStrongObjectPtr<UPhysicalMaterial> PlayingContactMaterial;
		TEnumAsByte<EDOFMode::Type> DOFMode = EDOFMode::Default;
		FVector CustomDOFPlaneNormal = FVector::YAxisVector;
		bool bSimulate = false;
		bool bGravity = false;
		bool bGenerateOverlaps = false;
		bool bHasDOFMode = false;
		float LinearDamping = 0.0f;
		float AngularDamping = 0.0f;
	};
	struct FMuscleDrive
	{
		TWeakObjectPtr<UPrimitiveComponent> BodyA;
		TWeakObjectPtr<UPrimitiveComponent> BodyB;
		FVector2D LocalA = FVector2D::ZeroVector;
		FVector2D LocalB = FVector2D::ZeroVector;
		float RestLength = 0.0f;
		float TargetLength = 0.0f;
		FVector LastDirection = FVector::ZeroVector;
		AssemblyMuscleRouting::FGuide Guide;
		AssemblyMuscleRouting::FState RouteState;
		TWeakObjectPtr<UPrimitiveComponent> GuideBody;
		FVector PullA = FVector::ZeroVector;
		FVector PullB = FVector::ZeroVector;
		FVector TouchA = FVector::ZeroVector;
		FVector TouchB = FVector::ZeroVector;
		double PassiveScale = 1.0;
		bool bWrapped = false;
		bool bContracting = false;
	};
	struct FMuscleRestState
	{
		float Length = 0.0f;
		FVector Direction = FVector::ZeroVector;
	};

	TWeakObjectPtr<UAssemblyPhysicsBridgeComponent> Bridge;
	TWeakObjectPtr<UAssemblySessionComponent> Session;
	TWeakObjectPtr<UJGDMessageSubsystem> MessageBus;
	FAssemblyPhysicsSnapshotMessage ActiveSnapshot;
	TMap<TWeakObjectPtr<UPrimitiveComponent>, FBodyState> BodyStates;
	TSet<TWeakObjectPtr<UPrimitiveComponent>> RegisteredBodies;
	UPROPERTY(Transient)
	TMap<FGuid, TObjectPtr<UPhysicsConstraintComponent>> Constraints;
	TMap<FGuid, FMuscleDrive> MuscleDrives;
	TMap<FGuid, FMuscleRestState> MuscleRestStates;
	TSet<FGuid> ReportedSameBodyMuscles;
	TSet<FGuid> ReportedBrokenJoints;
	FJGDMessageListenerHandle SnapshotListeners[4];
	FJGDMessageListenerHandle MuscleListener;
	FJGDMessageListenerHandle CoreRotationListener;
	FJGDMessageListenerHandle JointBreakListener;
	float CoreRotationInput = 0.0f;
	FVector CoreRotationWorldAxis = FVector::ZeroVector;
	FAssemblyPhysicsContext CoreRotationDriveContext;
	int64 LastAcceptedCoreDriveRevision = INDEX_NONE;
	bool bPrepared = false;
	bool bPlaying = false;
	bool bShuttingDown = false;
	bool bInitialized = false;
	bool bCoreRotationDriveContextValid = false;

	bool IsCurrentContext(const FAssemblyPhysicsContext& Context) const;
	bool IsCurrentLifecycleBinding(const FAssemblyPhysicsContext& Context, bool bAllowNewerRevision) const;
	bool IsProviderBoundToBridge() const;
	UFUNCTION()
	void HandleSnapshotMessage(FGameplayTag Channel, const FInstancedStruct& Message);
	UFUNCTION()
	void HandleMuscleDriveMessage(FGameplayTag Channel, const FInstancedStruct& Message);
	UFUNCTION()
	void HandleCoreRotationMessage(FGameplayTag Channel, const FInstancedStruct& Message);
	UFUNCTION()
	void HandleJointBreakMessage(FGameplayTag Channel, const FInstancedStruct& Message);
	void UnregisterMessageListeners();
	void ShutdownExecutor();
	void ClearCoreRotationDrive();
	bool CaptureAndValidateBodies(const FAssemblyPhysicsSnapshotMessage& Snapshot, FText& OutReason);
	void CaptureBodyState(UPrimitiveComponent* Body);
	void ApplyContactMaterial(UPrimitiveComponent* Body, FBodyState& State);
	void RestoreContactMaterial(UPrimitiveComponent* Body, FBodyState& State);
	void RestoreBodyAttachment(UPrimitiveComponent* Body, const FBodyState& State, bool bRestoreTransform);
	void RestoreBodyState(UPrimitiveComponent* Body, FBodyState& State, bool bRestoreTransform, bool bRestoreSimulation);
	void ReconcileAssemblyBodies(const FAssemblyPhysicsSnapshotMessage& Snapshot);
	bool BuildConstraints(const FAssemblyPhysicsSnapshotMessage& Snapshot, FText& OutReason);
	void DestroyConstraints();
	void ApplyAssemblyState();
	void RollbackPreparedState(bool bRestoreOriginalState);
	void ApplyMuscleDrives(float DeltaSeconds);
	float ComputeMuscleForceOnB(const FMuscleDrive& Drive, float Length, float RelativeSpeed, float DeltaSeconds = 0.0f) const;
	void BreakJoint(FGuid JointId);
	UPrimitiveComponent* ResolveBody(FGuid BoneId) const;
	FVector ResolveAnchorWorld(FGuid BoneId, FVector2D LocalAnchor) const;
	static FVector LocalPointToWorld(const UPrimitiveComponent* Body, FVector2D LocalPoint);
};
