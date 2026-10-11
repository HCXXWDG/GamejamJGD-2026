#pragma once

#include "CoreMinimal.h"
#include "AssemblyWorkbenchTypes.generated.h"

UENUM(BlueprintType)
enum class EAssemblyWorkbenchTool : uint8 { None, Bone, MuscleFirst, MuscleSecond };

UENUM(BlueprintType)
enum class EAssemblySelectionKind : uint8 { None, Bone, Muscle, Joint };

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
