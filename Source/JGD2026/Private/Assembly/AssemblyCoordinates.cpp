#include "Assembly/AssemblyComponent.h"
#include "AssemblyRules.h"
#include "AssemblyFreeRules.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"

#define LOCTEXT_NAMESPACE "AssemblyCoordinates"

namespace
{
	bool CanInvert(const FTransform& Transform)
	{
		const FVector Scale = Transform.GetScale3D();
		return !Transform.ContainsNaN() && !FMath::IsNearlyZero(Scale.X)
			&& !FMath::IsNearlyZero(Scale.Y) && !FMath::IsNearlyZero(Scale.Z);
	}
}

void UAssemblyComponent::SetGridFrame(USceneComponent* Frame)
{
	FText Reason;
	TrySetAssemblyFrame(Frame, Reason);
}

bool UAssemblyComponent::TrySetAssemblyFrame(USceneComponent* Frame, FText& OutReason)
{
	OutReason = FText::GetEmpty();
	if (Frame && (!IsValid(Frame) || Frame->GetWorld() != GetWorld()))
	{
		OutReason = LOCTEXT("DifferentFrameWorld", "组装坐标参考必须属于同一个游戏世界。");
		return false;
	}
	GridFrame = Frame;
	return true;
}

FTransform UAssemblyComponent::GetGridFrameTransform() const
{
	if (const USceneComponent* Frame = GridFrame.Get())
	{
		return Frame->GetComponentTransform();
	}
	return GetOwner() ? GetOwner()->GetActorTransform() : FTransform::Identity;
}

FVector UAssemblyComponent::CellToLocal(FIntPoint Cell) const
{
	const FAssemblyGridSettings& Grid = bInitialized ? RoundGrid : DefaultGrid;
	return Grid.TopLeftLocal + FVector((static_cast<double>(Cell.X) + 0.5) * Grid.CellSize,
		0.0, -(static_cast<double>(Cell.Y) + 0.5) * Grid.CellSize);
}

FIntPoint UAssemblyComponent::LocalToCell(FVector LocalPosition) const
{
	const FAssemblyGridSettings& Grid = bInitialized ? RoundGrid : DefaultGrid;
	if (LocalPosition.ContainsNaN() || !FMath::IsFinite(Grid.CellSize) || Grid.CellSize <= 0.0f)
	{
		return FIntPoint(-1, -1);
	}
	const double Column = (LocalPosition.X - Grid.TopLeftLocal.X) / Grid.CellSize;
	const double Row = (Grid.TopLeftLocal.Z - LocalPosition.Z) / Grid.CellSize;
	if (!FMath::IsFinite(Column) || !FMath::IsFinite(Row))
	{
		return FIntPoint(-1, -1);
	}
	return FIntPoint(FMath::FloorToInt32(FMath::Clamp(Column, static_cast<double>(MIN_int32), static_cast<double>(MAX_int32))),
		FMath::FloorToInt32(FMath::Clamp(Row, static_cast<double>(MIN_int32), static_cast<double>(MAX_int32))));
}

FVector UAssemblyComponent::CellToWorld(FIntPoint Cell) const
{
	return GetGridFrameTransform().TransformPosition(CellToLocal(Cell));
}

FIntPoint UAssemblyComponent::WorldToCell(FVector WorldPosition) const
{
	const FTransform Frame = GetGridFrameTransform();
	return CanInvert(Frame) && !WorldPosition.ContainsNaN()
		? LocalToCell(Frame.InverseTransformPosition(WorldPosition)) : FIntPoint(-1, -1);
}

bool UAssemblyComponent::IsCellInBounds(FIntPoint Cell) const
{
	const FAssemblyGridSettings& Grid = bInitialized ? RoundGrid : DefaultGrid;
	return Cell.X >= 0 && Cell.Y >= 0 && Cell.X < Grid.Columns && Cell.Y < Grid.Rows;
}

bool UAssemblyComponent::RayToGrid(FVector RayOrigin, FVector RayDirection, FVector& OutLocalPosition, FIntPoint& OutCell) const
{
	OutLocalPosition = FVector::ZeroVector;
	OutCell = FIntPoint(-1, -1);
	const FTransform Frame = GetGridFrameTransform();
	if (!CanInvert(Frame) || RayOrigin.ContainsNaN() || RayDirection.ContainsNaN() || !RayDirection.Normalize())
	{
		return false;
	}
	const FAssemblyGridSettings& Grid = bInitialized ? RoundGrid : DefaultGrid;
	const FVector Normal = Frame.TransformVectorNoScale(FVector(0.0, 1.0, 0.0));
	const double Denominator = FVector::DotProduct(RayDirection, Normal);
	if (FMath::IsNearlyZero(Denominator, UE_DOUBLE_SMALL_NUMBER))
	{
		return false;
	}
	const FVector PlanePoint = Frame.TransformPosition(Grid.TopLeftLocal);
	const double Distance = FVector::DotProduct(PlanePoint - RayOrigin, Normal) / Denominator;
	if (!FMath::IsFinite(Distance) || Distance < 0.0)
	{
		return false;
	}
	OutLocalPosition = Frame.InverseTransformPosition(RayOrigin + RayDirection * Distance);
	if (OutLocalPosition.ContainsNaN())
	{
		return false;
	}
	OutCell = LocalToCell(OutLocalPosition);
	// A plane hit may be outside the grid; keep that candidate for an invalid placement preview.
	return true;
}

bool UAssemblyComponent::RayToAssemblyPlane(FVector Origin, FVector Direction, FVector2D& OutPosition) const
{
	OutPosition = FVector2D::ZeroVector;
	const FTransform Frame = GetGridFrameTransform();
	if (!CanInvert(Frame) || Origin.ContainsNaN() || Direction.ContainsNaN() || !Direction.Normalize()) { return false; }
	const FVector Normal = Frame.TransformVectorNoScale(FVector::YAxisVector);
	const double Denominator = FVector::DotProduct(Direction, Normal);
	if (FMath::IsNearlyZero(Denominator, UE_DOUBLE_SMALL_NUMBER)) { return false; }
	const double Distance = FVector::DotProduct(Frame.GetLocation() - Origin, Normal) / Denominator;
	if (!FMath::IsFinite(Distance) || Distance < 0.0) { return false; }
	const FVector Local = Frame.InverseTransformPosition(Origin + Direction * Distance);
	if (Local.ContainsNaN()) { return false; }
	OutPosition = FVector2D(Local.X, Local.Z);
	return true;
}

bool UAssemblyComponent::RegisterBonePoseSource(FGuid BoneId, USceneComponent* PoseSource, FText& OutReason)
{
	OutReason = FText::GetEmpty();
	if (!FindBone(BoneId) || !IsValid(PoseSource) || PoseSource->GetWorld() != GetWorld())
	{
		OutReason = LOCTEXT("InvalidPoseSource", "请提供已安装骨头的实例 ID 和有效的骨头场景组件。");
		return false;
	}
	BonePoseSources.Add(BoneId, PoseSource);
	return true;
}

USceneComponent* UAssemblyComponent::GetBonePoseSource(FGuid BoneId) const
{
	const TWeakObjectPtr<USceneComponent>* Source = BonePoseSources.Find(BoneId);
	return FindBone(BoneId) && Source ? Source->Get() : nullptr;
}

bool UAssemblyComponent::UnregisterBonePoseSourceIfMatches(FGuid BoneId, const USceneComponent* ExpectedSource)
{
	const TWeakObjectPtr<USceneComponent>* Source = BonePoseSources.Find(BoneId);
	if (!ExpectedSource || !Source || Source->Get() != ExpectedSource)
	{
		return false;
	}
	BonePoseSources.Remove(BoneId);
	return true;
}

void UAssemblyComponent::UnregisterBonePoseSource(FGuid BoneId)
{
	BonePoseSources.Remove(BoneId);
}

bool UAssemblyComponent::GetBoneWorldTransform(FGuid BoneId, FTransform& OutTransform) const
{
	OutTransform = FTransform::Identity;
	const FAssemblyBoneInstance* Bone = FindBone(BoneId);
	if (!Bone)
	{
		return false;
	}
	if (const TWeakObjectPtr<USceneComponent>* Source = BonePoseSources.Find(BoneId); Source && Source->IsValid())
	{
		OutTransform = Source->Get()->GetComponentTransform();
	}
	else
	{
		OutTransform = Bone->LocalVisualTransform * GetGridFrameTransform();
	}
	return CanInvert(OutTransform);
}

bool UAssemblyComponent::GetEndpointWorldPosition(const FAssemblyMuscleEndpoint& Endpoint, FVector& OutWorldPosition) const
{
	OutWorldPosition = FVector::ZeroVector;
	const FAssemblyBoneInstance* Bone = FindBone(Endpoint.BoneId);
	const FAssemblyBoneDefinition* Definition = Bone ? FindBoneDefinition(Bone->TypeId) : nullptr;
	FTransform BoneTransform;
	const bool bOnBone = Definition && (RoundPlacementMode == EAssemblyPlacementMode::Free
		? AssemblyFreeRules::IsPointOnBone(Definition->BoneParameters, Endpoint.BoneLocalPoint)
		: AssemblyRules::IsPointOnBone(Definition->Footprint, RoundGrid.CellSize, Endpoint.BoneLocalPoint));
	if (!bOnBone
		|| !GetBoneWorldTransform(Endpoint.BoneId, BoneTransform))
	{
		return false;
	}
	OutWorldPosition = BoneTransform.TransformPosition(FVector(Endpoint.BoneLocalPoint.X, 0.0, Endpoint.BoneLocalPoint.Y));
	return !OutWorldPosition.ContainsNaN();
}

bool UAssemblyComponent::FindBoneAtWorldPosition(FVector WorldPosition, FAssemblyMuscleEndpoint& OutEndpoint) const
{
	OutEndpoint = FAssemblyMuscleEndpoint();
	if (!bInitialized || WorldPosition.ContainsNaN())
	{
		return false;
	}
	for (int32 Index = InstalledBones.Num() - 1; Index >= 0; --Index)
	{
		const FAssemblyBoneInstance& Bone = InstalledBones[Index];
		const FAssemblyBoneDefinition* Definition = FindBoneDefinition(Bone.TypeId);
		FTransform Transform;
		if (!Definition || !GetBoneWorldTransform(Bone.InstanceId, Transform))
		{
			continue;
		}
		const FVector Local = Transform.InverseTransformPosition(WorldPosition);
		const FVector2D Point(Local.X, Local.Z);
		const bool bOnBone = RoundPlacementMode == EAssemblyPlacementMode::Free
			? AssemblyFreeRules::IsPointOnBone(Definition->BoneParameters, Point)
			: AssemblyRules::IsPointOnBone(Definition->Footprint, RoundGrid.CellSize, Point);
		if (bOnBone)
		{
			OutEndpoint.BoneId = Bone.InstanceId;
			OutEndpoint.BoneLocalPoint = Point;
			return true;
		}
	}
	return false;
}

#undef LOCTEXT_NAMESPACE
