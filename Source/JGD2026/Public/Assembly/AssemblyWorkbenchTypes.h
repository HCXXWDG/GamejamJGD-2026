#pragma once

#include "CoreMinimal.h"
#include "AssemblyWorkbenchTypes.generated.h"

UENUM(BlueprintType)
enum class EAssemblyWorkbenchTool : uint8 { None, Bone, MuscleFirst, MuscleSecond, JointPart };

UENUM(BlueprintType)
enum class EAssemblySelectionKind : uint8 { None, Bone, Muscle, Joint, JointPart };

/** Shelf artwork and drag routing; physical joint behavior is configured separately. */
UENUM(BlueprintType)
enum class EAssemblyShelfPartKind : uint8 { Bone, Muscle, JointPart };

USTRUCT(BlueprintType)
struct JGD2026_API FAssemblySelection
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "Assembly")
	EAssemblySelectionKind Kind = EAssemblySelectionKind::None;
	UPROPERTY(BlueprintReadOnly, Category = "Assembly")
	FGuid InstanceId;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FAssemblyWorkbenchChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAssemblySelectionChanged, FAssemblySelection, Selection);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAssemblyWorkbenchMessage, FText, Message);
