#pragma once

#include "CoreMinimal.h"
#include "Assembly/NormalBoneTypes.h"
#include "Assembly/AssemblyJointPartTypes.h"
#include "AssemblyTypes.generated.h"

UENUM(BlueprintType)
enum class EAssemblyPhase : uint8 { Assembly, Playing };

UENUM(BlueprintType)
enum class EAssemblyPlacementMode : uint8 { Free, Grid };

UENUM(BlueprintType)
enum class EAssemblyJointKind : uint8 { Hinge, Fixed, BreakableHinge, BreakableFixed };

/** Physical connection policy. The reference grid never limits free placement. */
USTRUCT(BlueprintType)
struct JGD2026_API FAssemblyFreeSettings
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	FVector2D CoreLocalPosition = FVector2D::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly", meta = (ClampMin = "0.01"))
	float CoreRadius = 32.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly", meta = (ClampMin = "0"))
	float ConnectionDistance = 16.0f;
	// Drag acquisition uses ConnectionDistance; a larger release distance prevents contact jitter.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Snap")
	bool bEnableSurfaceSnapping = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Snap", meta = (ClampMin = "0"))
	float SnapReleaseDistance = 32.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	bool bAllowBoneOverlap = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	bool bRejectDisconnectingRemoval = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	float DefaultJointMinAngle = -60.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	float DefaultJointMaxAngle = 60.0f;
};

// Only muscles have a key binding. Space is a separate core rotation intent.
UENUM(BlueprintType)
enum class EAssemblyMuscleKey : uint8 { None, W, A, S, D };

USTRUCT(BlueprintType)
struct JGD2026_API FAssemblyGridSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly", meta = (ClampMin = "1", ClampMax = "64"))
	int32 Columns = 6;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly", meta = (ClampMin = "1", ClampMax = "64"))
	int32 Rows = 6;
	// Unreal units, independent of the source texture's pixel resolution.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly", meta = (ClampMin = "0.01"))
	float CellSize = 64.0f;
	// Boundary of the top-left cell in GridFrame's local XZ plane; rows run along -Z.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	FVector TopLeftLocal = FVector(-160.0, 0.0, 160.0);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	TArray<FIntPoint> CoreCells = { FIntPoint(2, 2) };
};

USTRUCT(BlueprintType)
struct JGD2026_API FAssemblyBoneDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	FName TypeId = TEXT("Bone");
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	FText DisplayName;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly", meta = (ClampMin = "0"))
	int32 InitialQuantity = 4;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	FNormalBoneParameters BoneParameters;
	// Unrotated, four-connected shape. Normalized internally to a top-left origin.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	TArray<FIntPoint> Footprint = { FIntPoint(0, 0), FIntPoint(1, 0) };
	// Independent joint pieces share the rigid instance/pose path; Joints remains the constraint-edge list.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	EAssemblyRigidPartKind PartKind = EAssemblyRigidPartKind::Bone;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|JointPart")
	EAssemblyJointPartShape JointShape = EAssemblyJointPartShape::Round;
	// Full body width and height in the existing local XZ plane, independent of texture pixels.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|JointPart")
	FVector2D JointPartSize = FVector2D(64.0, 64.0);
	// Legacy marker positions for Blueprint compatibility; free surface connections no longer use slots.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|JointPart")
	TArray<FVector2D> ConnectionPoints;
	// Optional simple closed polygon in body-local XZ units. Empty uses the standard shape.
	// No repeated closing vertex. This is assembly geometry, independent of texture resolution.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Surface")
	TArray<FVector2D> SurfaceOutline;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|JointPart")
	EAssemblyJointKind DefaultJointKind = EAssemblyJointKind::Hinge;
};

USTRUCT(BlueprintType)
struct JGD2026_API FAssemblyMuscleDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	FName TypeId = TEXT("Muscle");
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	FText DisplayName;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly", meta = (ClampMin = "0"))
	int32 InitialQuantity = 3;
};

USTRUCT(BlueprintType)
struct JGD2026_API FAssemblyBoneInstance
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Assembly")
	FGuid InstanceId;
	UPROPERTY(BlueprintReadOnly, Category = "Assembly")
	FName TypeId;
	// Local X/Z center and angle about local +Y; positive rotation is clockwise.
	UPROPERTY(BlueprintReadOnly, Category = "Assembly")
	FVector2D LocalPosition = FVector2D::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "Assembly")
	float RotationDegrees = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Assembly")
	FIntPoint AnchorCell = FIntPoint::ZeroValue;
	UPROPERTY(BlueprintReadOnly, Category = "Assembly")
	int32 QuarterTurns = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Assembly")
	TArray<FIntPoint> OccupiedCells;
	// Sprite/body pivot: center of its unrotated footprint's bounding rectangle.
	UPROPERTY(BlueprintReadOnly, Category = "Assembly")
	FTransform LocalVisualTransform = FTransform::Identity;
};

USTRUCT(BlueprintType)
struct JGD2026_API FAssemblyMuscleEndpoint
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	FGuid BoneId;
	// Continuous bone-local position: X = local X, Y = local Z. Never a grid cell.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly")
	FVector2D BoneLocalPoint = FVector2D::ZeroVector;
};

USTRUCT(BlueprintType)
struct JGD2026_API FAssemblyMuscleInstance
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Assembly")
	FGuid InstanceId;
	UPROPERTY(BlueprintReadOnly, Category = "Assembly")
	FName TypeId;
	UPROPERTY(BlueprintReadOnly, Category = "Assembly")
	FAssemblyMuscleEndpoint EndpointA;
	UPROPERTY(BlueprintReadOnly, Category = "Assembly")
	FAssemblyMuscleEndpoint EndpointB;
	UPROPERTY(BlueprintReadOnly, Category = "Assembly")
	EAssemblyMuscleKey BindingKey = EAssemblyMuscleKey::None;
	// Input intent, not confirmation that a physical muscle has reached its length.
	UPROPERTY(BlueprintReadOnly, Category = "Assembly")
	bool bContracting = false;
};

/** An invalid BoneId denotes the spherical core; muscles never use this convention. */
USTRUCT(BlueprintType)
struct JGD2026_API FAssemblyJointInstance
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "Assembly")
	FGuid InstanceId;
	UPROPERTY(BlueprintReadOnly, Category = "Assembly")
	FGuid BoneA;
	UPROPERTY(BlueprintReadOnly, Category = "Assembly")
	FGuid BoneB;
	UPROPERTY(BlueprintReadOnly, Category = "Assembly")
	FVector2D LocalAnchorA = FVector2D::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "Assembly")
	FVector2D LocalAnchorB = FVector2D::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "Assembly")
	EAssemblyJointKind Kind = EAssemblyJointKind::Hinge;
	UPROPERTY(BlueprintReadOnly, Category = "Assembly")
	float MinAngleDegrees = -60.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Assembly")
	float MaxAngleDegrees = 60.0f;
};

/** Drag-only latch. Invalid TargetId is the core only when bSnapped and bCore are true. */
USTRUCT(BlueprintType)
struct JGD2026_API FAssemblySurfaceSnapState
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "Assembly|Snap")
	bool bSnapped = false;
	UPROPERTY(BlueprintReadOnly, Category = "Assembly|Snap")
	bool bCore = false;
	UPROPERTY(BlueprintReadOnly, Category = "Assembly|Snap")
	FGuid TargetId;
	UPROPERTY(BlueprintReadOnly, Category = "Assembly|Snap")
	FVector2D SourceLocalAnchor = FVector2D::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "Assembly|Snap")
	FVector2D TargetLocalAnchor = FVector2D::ZeroVector;
	// Drag-only calibration: radial bone alignment must not overwrite the unsnapped pointer or angle.
	UPROPERTY(BlueprintReadOnly, Category = "Assembly|Snap")
	bool bRadialAligned = false;
	// Pointer contact radius minus target circle radius; retained while sliding around this circle.
	UPROPERTY(BlueprintReadOnly, Category = "Assembly|Snap")
	double RadialPointerOffset = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Assembly|Snap")
	float RadialInputAngle = 0.0f;
	// Preserve the contacted end while the user moves around the circle without manually rotating.
	UPROPERTY(BlueprintReadOnly, Category = "Assembly|Snap")
	bool bRadialXOutward = true;
};

USTRUCT(BlueprintType)
struct JGD2026_API FAssemblyPlacementResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Assembly")
	bool bValid = false;
	UPROPERTY(BlueprintReadOnly, Category = "Assembly")
	FText Reason;
	UPROPERTY(BlueprintReadOnly, Category = "Assembly")
	FVector2D LocalPosition = FVector2D::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "Assembly")
	float RotationDegrees = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Assembly")
	FIntPoint AnchorCell = FIntPoint::ZeroValue;
	UPROPERTY(BlueprintReadOnly, Category = "Assembly")
	int32 QuarterTurns = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Assembly")
	TArray<FIntPoint> OccupiedCells;
	UPROPERTY(BlueprintReadOnly, Category = "Assembly")
	FTransform LocalVisualTransform = FTransform::Identity;
	UPROPERTY(BlueprintReadOnly, Category = "Assembly|Snap")
	FAssemblySurfaceSnapState SurfaceSnap;
};

USTRUCT(BlueprintType)
struct JGD2026_API FAssemblyValidationResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Assembly")
	bool bValid = false;
	UPROPERTY(BlueprintReadOnly, Category = "Assembly")
	FText Reason;
};
