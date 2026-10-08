#pragma once

#include "CoreMinimal.h"
#include "Assembly/NormalBoneTypes.h"
#include "AssemblyTypes.generated.h"

UENUM(BlueprintType)
enum class EAssemblyPhase : uint8 { Assembly, Playing };

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

USTRUCT(BlueprintType)
struct JGD2026_API FAssemblyPlacementResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Assembly")
	bool bValid = false;
	UPROPERTY(BlueprintReadOnly, Category = "Assembly")
	FText Reason;
	UPROPERTY(BlueprintReadOnly, Category = "Assembly")
	FIntPoint AnchorCell = FIntPoint::ZeroValue;
	UPROPERTY(BlueprintReadOnly, Category = "Assembly")
	int32 QuarterTurns = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Assembly")
	TArray<FIntPoint> OccupiedCells;
	UPROPERTY(BlueprintReadOnly, Category = "Assembly")
	FTransform LocalVisualTransform = FTransform::Identity;
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
