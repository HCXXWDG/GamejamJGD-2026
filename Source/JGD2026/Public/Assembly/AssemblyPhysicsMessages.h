#pragma once

#include "CoreMinimal.h"
#include "NativeGameplayTags.h"
#include "Assembly/AssemblyTypes.h"
#include "AssemblyPhysicsMessages.generated.h"

class UAssemblyComponent;
class USceneComponent;
class UPrimitiveComponent;
class UWorld;

/** Each channel has one fixed payload type. Registered natively by the game module. */
namespace AssemblyMessageTags
{
	JGD2026_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(AssemblyChanged);
	JGD2026_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(AssemblyReset);
	JGD2026_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(AssemblyPhaseChanged);
	JGD2026_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(PhysicsRebuildRequested);
	JGD2026_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(MuscleDriveRequested);
	JGD2026_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(CoreRotationRequested);
	JGD2026_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(JointBreakRequested);
	JGD2026_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(JointBroken);
}

/** Copy this context into physics replies. Never address another assembly/world. */
USTRUCT(BlueprintType)
struct JGD2026_API FAssemblyPhysicsContext
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Physics")
	TObjectPtr<UAssemblyComponent> Assembly = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Physics")
	TObjectPtr<UWorld> World = nullptr;
	// Changes on binding replacement; restart increments RoundGeneration instead.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Physics")
	FGuid SourceId;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Physics")
	int32 RoundGeneration = 0;
	// An edit sequence for consumers to discard older snapshots; not an acknowledgement.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Physics")
	int64 Revision = 0;
};

USTRUCT(BlueprintType)
struct JGD2026_API FAssemblyPhysicsBonePose
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Physics")
	FGuid BoneId;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Physics")
	TObjectPtr<USceneComponent> PoseSource = nullptr;
	// May be null when the physical adapter has not yet supplied a primitive body.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Physics")
	TObjectPtr<UPrimitiveComponent> Body = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Physics")
	FTransform WorldTransform = FTransform::Identity;
};

/** Payload for Changed, Reset, PhaseChanged AND RebuildRequested. */
USTRUCT(BlueprintType)
struct JGD2026_API FAssemblyPhysicsSnapshotMessage
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Physics")
	FAssemblyPhysicsContext Context;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Physics")
	EAssemblyPhase Phase = EAssemblyPhase::Assembly;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Physics")
	EAssemblyPlacementMode PlacementMode = EAssemblyPlacementMode::Free;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Physics")
	FTransform FrameTransform = FTransform::Identity;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Physics")
	TObjectPtr<UPrimitiveComponent> CoreBody = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Physics")
	FAssemblyFreeSettings FreeSettings;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Physics")
	FAssemblyGridSettings ReferenceGrid;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Physics")
	TArray<FAssemblyBoneDefinition> BoneDefinitions;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Physics")
	TArray<FAssemblyMuscleDefinition> MuscleDefinitions;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Physics")
	TArray<FAssemblyBoneInstance> Bones;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Physics")
	TArray<FAssemblyMuscleInstance> Muscles;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Physics")
	TArray<FAssemblyJointInstance> Joints;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Physics")
	TArray<FAssemblyPhysicsBonePose> BonePoses;
	// ClearBridge also publishes Reset with this true. Consumers release old constraints/bodies.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Physics")
	bool bClearPhysicalState = false;
};

/** true = request contraction; false = request relaxation, not a completed motion. */
USTRUCT(BlueprintType)
struct JGD2026_API FAssemblyPhysicsMuscleDriveMessage
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Physics")
	FAssemblyPhysicsContext Context;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Physics")
	FAssemblyMuscleInstance Muscle;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Physics")
	bool bContracting = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Physics")
	FTransform FrameTransform = FTransform::Identity;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Physics")
	TObjectPtr<UPrimitiveComponent> CoreBody = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Physics")
	FAssemblyPhysicsBonePose EndpointBoneA;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Physics")
	FAssemblyPhysicsBonePose EndpointBoneB;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Physics")
	FVector EndpointWorldA = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Physics")
	FVector EndpointWorldB = FVector::ZeroVector;
};

/** The physics adapter alone chooses torque/angular speed and constraint propagation. */
USTRUCT(BlueprintType)
struct JGD2026_API FAssemblyPhysicsCoreRotationMessage
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Physics")
	FAssemblyPhysicsContext Context;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Physics")
	bool bHeld = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Physics")
	TObjectPtr<UPrimitiveComponent> CoreBody = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Physics")
	FTransform FrameTransform = FTransform::Identity;
	// Positive rotation about this axis is clockwise in the existing local XZ plane.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Physics")
	FVector WorldClockwiseAxis = FVector(0, 1, 0);
};

USTRUCT(BlueprintType)
struct JGD2026_API FAssemblyPhysicsJointBreakMessage
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Physics")
	FAssemblyPhysicsContext Context;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Physics")
	FAssemblyJointInstance Joint;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Physics")
	TObjectPtr<UPrimitiveComponent> CoreBody = nullptr;
	// Invalid BoneId denotes the core; its Body/PoseSource is CoreBody.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Physics")
	FAssemblyPhysicsBonePose BodyA;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Physics")
	FAssemblyPhysicsBonePose BodyB;
};

/** Physics publishes this ONLY after the requested constraint has actually broken. */
USTRUCT(BlueprintType)
struct JGD2026_API FAssemblyPhysicsJointBrokenMessage
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Physics")
	FAssemblyPhysicsContext Context;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Physics")
	FGuid JointId;
};
