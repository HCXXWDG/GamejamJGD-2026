#include "AssemblyRules.h"

#define LOCTEXT_NAMESPACE "AssemblyRules"

namespace
{
	const FIntPoint Neighbors[] = { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) };

	bool InBounds(const FAssemblyGridSettings& Grid, FIntPoint Cell)
	{
		return Cell.X >= 0 && Cell.Y >= 0 && Cell.X < Grid.Columns && Cell.Y < Grid.Rows;
	}

	bool IsConnected(const TSet<FIntPoint>& Cells, FIntPoint Start)
	{
		TSet<FIntPoint> Visited;
		TArray<FIntPoint> Queue;
		Visited.Add(Start);
		Queue.Add(Start);
		for (int32 Index = 0; Index < Queue.Num(); ++Index)
		{
			for (const FIntPoint Offset : Neighbors)
			{
				const FIntPoint Next = Queue[Index] + Offset;
				if (Cells.Contains(Next) && !Visited.Contains(Next))
				{
					Visited.Add(Next);
					Queue.Add(Next);
				}
			}
		}
		return Visited.Num() == Cells.Num();
	}

	TArray<FIntPoint> NormalizeShape(const TArray<FIntPoint>& Footprint, int32 Turns, FIntPoint& OutSize)
	{
		TArray<FIntPoint> Shape;
		FIntPoint Minimum(MAX_int32, MAX_int32);
		FIntPoint Maximum(MIN_int32, MIN_int32);
		for (FIntPoint Cell : Footprint)
		{
			for (int32 Turn = 0; Turn < Turns; ++Turn)
			{
				Cell = FIntPoint(-Cell.Y, Cell.X);
			}
			Shape.Add(Cell);
			Minimum.X = FMath::Min(Minimum.X, Cell.X);
			Minimum.Y = FMath::Min(Minimum.Y, Cell.Y);
			Maximum.X = FMath::Max(Maximum.X, Cell.X);
			Maximum.Y = FMath::Max(Maximum.Y, Cell.Y);
		}
		for (FIntPoint& Cell : Shape)
		{
			Cell -= Minimum;
		}
		OutSize = Shape.IsEmpty() ? FIntPoint::ZeroValue : Maximum - Minimum + FIntPoint(1, 1);
		return Shape;
	}
}

int32 AssemblyRules::NormalizeTurns(int32 QuarterTurns)
{
	return (QuarterTurns % 4 + 4) % 4;
}

bool AssemblyRules::IsValidKey(EAssemblyMuscleKey Key, bool bAllowNone)
{
	return (bAllowNone && Key == EAssemblyMuscleKey::None)
		|| Key == EAssemblyMuscleKey::W || Key == EAssemblyMuscleKey::A
		|| Key == EAssemblyMuscleKey::S || Key == EAssemblyMuscleKey::D;
}

bool AssemblyRules::ValidateConfiguration(const FAssemblyGridSettings& Grid,
	const TArray<FAssemblyBoneDefinition>& Bones, const TArray<FAssemblyMuscleDefinition>& Muscles, FText& OutReason)
{
	OutReason = FText::GetEmpty();
	if (Grid.Columns < 1 || Grid.Columns > 64 || Grid.Rows < 1 || Grid.Rows > 64
		|| !FMath::IsFinite(Grid.CellSize) || Grid.CellSize < 0.01f || Grid.TopLeftLocal.ContainsNaN())
	{
		OutReason = LOCTEXT("InvalidGrid", "网格行列须为 1–64，格长须为有限正数，位置须有效。");
		return false;
	}
	TSet<FIntPoint> Core;
	for (const FIntPoint Cell : Grid.CoreCells)
	{
		if (!InBounds(Grid, Cell) || Core.Contains(Cell))
		{
			OutReason = LOCTEXT("InvalidCore", "核心占格必须在网格内，且不能重复。");
			return false;
		}
		Core.Add(Cell);
	}
	if (Core.IsEmpty() || !IsConnected(Core, Grid.CoreCells[0]))
	{
		OutReason = LOCTEXT("DisconnectedCore", "核心必须具有非空、四邻连通的占格。");
		return false;
	}
	TSet<FName> TypeIds;
	for (const FAssemblyBoneDefinition& Bone : Bones)
	{
		if (Bone.TypeId.IsNone() || TypeIds.Contains(Bone.TypeId) || Bone.InitialQuantity < 0)
		{
			OutReason = LOCTEXT("InvalidBoneType", "组件类型标识不能空或重复，初始数量不能为负数。");
			return false;
		}
		TypeIds.Add(Bone.TypeId);
		FNormalBonePhysicalStats PhysicalStats;
		if (!NormalBoneMetrics::Evaluate(Bone.BoneParameters, PhysicalStats, OutReason))
		{
			return false;
		}
		TSet<FIntPoint> Shape;
		for (const FIntPoint Cell : Bone.Footprint)
		{
			// Bound offsets before rotating/normalizing to avoid integer overflow on malformed data.
			if (Cell.X < -64 || Cell.X > 64 || Cell.Y < -64 || Cell.Y > 64 || Shape.Contains(Cell))
			{
				OutReason = LOCTEXT("InvalidFootprint", "骨头占格不能重复；单格偏移须在 -64 到 64 内。");
				return false;
			}
			Shape.Add(Cell);
		}
		if (Shape.IsEmpty() || !IsConnected(Shape, Bone.Footprint[0]))
		{
			OutReason = LOCTEXT("DisconnectedFootprint", "每种骨头的占格形状必须非空且四邻连通。");
			return false;
		}
		FIntPoint Size;
		NormalizeShape(Bone.Footprint, 0, Size);
		if (!((Size.X <= Grid.Columns && Size.Y <= Grid.Rows) || (Size.Y <= Grid.Columns && Size.X <= Grid.Rows)))
		{
			OutReason = LOCTEXT("ShapeTooLarge", "骨头占格在任何旋转下都无法放进本关卡网格。");
			return false;
		}
	}
	for (const FAssemblyMuscleDefinition& Muscle : Muscles)
	{
		if (Muscle.TypeId.IsNone() || TypeIds.Contains(Muscle.TypeId) || Muscle.InitialQuantity < 0)
		{
			OutReason = LOCTEXT("InvalidMuscleType", "组件类型标识不能空或重复，初始数量不能为负数。");
			return false;
		}
		TypeIds.Add(Muscle.TypeId);
	}
	return true;
}

FAssemblyPlacementResult AssemblyRules::MakePlacement(const FAssemblyGridSettings& Grid,
	const TArray<FIntPoint>& Footprint, FIntPoint Anchor, int32 QuarterTurns)
{
	FAssemblyPlacementResult Result;
	Result.AnchorCell = Anchor;
	Result.QuarterTurns = NormalizeTurns(QuarterTurns);
	FIntPoint Size;
	const TArray<FIntPoint> Shape = NormalizeShape(Footprint, Result.QuarterTurns, Size);
	for (const FIntPoint Cell : Shape)
	{
		const int64 X = static_cast<int64>(Anchor.X) + Cell.X;
		const int64 Y = static_cast<int64>(Anchor.Y) + Cell.Y;
		if (X < MIN_int32 || X > MAX_int32 || Y < MIN_int32 || Y > MAX_int32)
		{
			Result.Reason = LOCTEXT("CellOverflow", "放置位置超出有效坐标范围。");
			return Result;
		}
		Result.OccupiedCells.Add(FIntPoint(static_cast<int32>(X), static_cast<int32>(Y)));
	}
	const FVector Center = Grid.TopLeftLocal + FVector(
		(static_cast<double>(Anchor.X) + Size.X * 0.5) * Grid.CellSize, 0.0,
		-(static_cast<double>(Anchor.Y) + Size.Y * 0.5) * Grid.CellSize);
	// +Y rotation maps +X to -Z, matching row/column clockwise quarter turns in the XZ plane.
	Result.LocalVisualTransform = FTransform(FQuat(FVector(0.0, 1.0, 0.0), Result.QuarterTurns * UE_DOUBLE_HALF_PI), Center);
	if (Result.LocalVisualTransform.ContainsNaN())
	{
		Result.Reason = LOCTEXT("InvalidTransform", "放置位置产生了无效变换。");
	}
	return Result;
}

bool AssemblyRules::ValidateLayout(const FAssemblyGridSettings& Grid, const TArray<FAssemblyBoneInstance>& Bones,
	const TArray<FIntPoint>* Candidate, FGuid IgnoreId, FText& OutReason)
{
	OutReason = FText::GetEmpty();
	TSet<FIntPoint> Cells;
	for (const FIntPoint CoreCell : Grid.CoreCells)
	{
		Cells.Add(CoreCell);
	}
	auto AddCells = [&Grid, &Cells, &OutReason](const TArray<FIntPoint>& PartCells)
	{
		if (PartCells.IsEmpty())
		{
			OutReason = LOCTEXT("EmptyPart", "骨头占格不能为空。");
			return false;
		}
		for (const FIntPoint Cell : PartCells)
		{
			if (!InBounds(Grid, Cell))
			{
				OutReason = LOCTEXT("OutOfBounds", "骨头所有占格都必须位于组装网格内。");
				return false;
			}
			if (Cells.Contains(Cell))
			{
				OutReason = LOCTEXT("Overlap", "骨头不能与核心或其他骨头重叠。");
				return false;
			}
			Cells.Add(Cell);
		}
		return true;
	};
	for (const FAssemblyBoneInstance& Bone : Bones)
	{
		if (Bone.InstanceId != IgnoreId && !AddCells(Bone.OccupiedCells))
		{
			return false;
		}
	}
	if (Candidate && !AddCells(*Candidate))
	{
		return false;
	}
	if (Grid.CoreCells.IsEmpty() || !IsConnected(Cells, Grid.CoreCells[0]))
	{
		OutReason = LOCTEXT("NotConnected", "所有骨头必须通过上下左右相邻格连接到核心；本次操作会造成断开。");
		return false;
	}
	return true;
}

bool AssemblyRules::IsPointOnBone(const TArray<FIntPoint>& Footprint, double CellSize, FVector2D Point)
{
	if (!FMath::IsFinite(Point.X) || !FMath::IsFinite(Point.Y) || Footprint.IsEmpty())
	{
		return false;
	}
	FIntPoint Size;
	const TArray<FIntPoint> Shape = NormalizeShape(Footprint, 0, Size);
	// Closed cell rectangles allow the outer edge of a bone as an attachment point.
	for (const FIntPoint Cell : Shape)
	{
		const double MinX = (Cell.X - Size.X * 0.5) * CellSize;
		const double MaxZ = (Size.Y * 0.5 - Cell.Y) * CellSize;
		if (Point.X >= MinX - UE_KINDA_SMALL_NUMBER && Point.X <= MinX + CellSize + UE_KINDA_SMALL_NUMBER
			&& Point.Y <= MaxZ + UE_KINDA_SMALL_NUMBER && Point.Y >= MaxZ - CellSize - UE_KINDA_SMALL_NUMBER)
		{
			return true;
		}
	}
	return false;
}

#undef LOCTEXT_NAMESPACE
