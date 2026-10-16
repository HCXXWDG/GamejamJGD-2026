#include "Assembly/AssemblyMuscle.h"
#include "Assembly/AssemblyComponent.h"
#include "AssemblyRenderLayers.h"
#include "Components/SceneComponent.h"
#include "Components/SplineComponent.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInterface.h"
#include "Math/RotationMatrix.h"
#include "PaperSprite.h"
#include "PaperSpriteComponent.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "AssemblyMuscle"

namespace
{
	struct FSliceCanvas
	{
		FVector2D Pixels = FVector2D::ZeroVector;
		FVector CenterInPivotSpace = FVector::ZeroVector;
		double UnitsPerPixel = 0.0;
	};

	bool HasUsableSprite(UPaperSprite* Sprite)
	{
		if (!IsValid(Sprite))
		{
			return false;
		}
		const FBoxSphereBounds Bounds = Sprite->GetRenderBounds();
		return !Bounds.Origin.ContainsNaN() && !Bounds.BoxExtent.ContainsNaN()
			&& FMath::IsFinite(Bounds.BoxExtent.X) && FMath::IsFinite(Bounds.BoxExtent.Z)
			&& Bounds.BoxExtent.X > UE_SMALL_NUMBER && Bounds.BoxExtent.Z > UE_SMALL_NUMBER;
	}

	bool HasUsableEndpoints(const FVector& A, const FVector& B)
	{
		return !A.ContainsNaN() && !B.ContainsNaN()
			&& FMath::IsFinite(FVector::DistSquared(A, B))
			&& FVector::DistSquared(A, B) > UE_KINDA_SMALL_NUMBER;
	}

	bool GetSliceCanvas(UPaperSprite* Sprite, FSliceCanvas& OutCanvas)
	{
		if (!IsValid(Sprite) || Sprite->BakedRenderData.Num() < 3
			|| Sprite->BakedRenderData.Num() % 3 != 0)
		{
			return false;
		}
		// Check vertices before asking Paper2D to build bounds; NaNs must never reach its
		// bounding-box/render-state code, including when an invalid asset replaces visible art.
		for (const FVector4& Vertex : Sprite->BakedRenderData)
		{
			if (!FMath::IsFinite(Vertex.X) || !FMath::IsFinite(Vertex.Y)
				|| !FMath::IsFinite(Vertex.Z) || !FMath::IsFinite(Vertex.W)) return false;
		}
		for (int32 Index = 0; Index < Sprite->BakedRenderData.Num(); Index += 3)
		{
			const FVector4& A = Sprite->BakedRenderData[Index];
			const FVector4& B = Sprite->BakedRenderData[Index + 1];
			const FVector4& C = Sprite->BakedRenderData[Index + 2];
			const double Area = (B.X - A.X) * (C.Y - A.Y) - (B.Y - A.Y) * (C.X - A.X);
			if (!FMath::IsFinite(Area) || FMath::Abs(Area) <= UE_SMALL_NUMBER) return false;
		}
		if (!HasUsableSprite(Sprite)) return false;
		// SourceSize/PivotPosition are editor-only APIs. The baked atlas rectangle and baked
		// XY/UV mapping also exist after cooking, and retain the complete source canvas.
		const FSlateAtlasData Atlas = Sprite->GetSlateAtlasData();
		UTexture2D* Texture = Cast<UTexture2D>(Atlas.AtlasTexture);
		if (!IsValid(Texture) || Atlas.StartUV.ContainsNaN() || Atlas.SizeUV.ContainsNaN()
			|| Atlas.SizeUV.X <= 0.0 || Atlas.SizeUV.Y <= 0.0
			|| Atlas.StartUV.X < 0.0 || Atlas.StartUV.Y < 0.0
			|| Atlas.StartUV.X + Atlas.SizeUV.X > 1.0 + UE_KINDA_SMALL_NUMBER
			|| Atlas.StartUV.Y + Atlas.SizeUV.Y > 1.0 + UE_KINDA_SMALL_NUMBER)
		{
			return false;
		}
		const FIntPoint ImportedSize = Texture->GetImportedSize();
		const double UnitsPerPixel = Sprite->GetUnrealUnitsPerPixel();
		if (ImportedSize.X <= 0 || ImportedSize.Y <= 0
			|| !FMath::IsFinite(UnitsPerPixel) || UnitsPerPixel <= UE_SMALL_NUMBER)
		{
			return false;
		}
		OutCanvas.Pixels = Atlas.SizeUV * FVector2D(ImportedSize);
		OutCanvas.UnitsPerPixel = UnitsPerPixel;
		const FVector2D CenterUV = Atlas.StartUV + Atlas.SizeUV * 0.5;
		const FVector4& FirstVertex = Sprite->BakedRenderData[0];
		OutCanvas.CenterInPivotSpace = FVector(
			FirstVertex.X + (CenterUV.X - FirstVertex.Z) * ImportedSize.X * UnitsPerPixel,
			0.0, FirstVertex.Y - (CenterUV.Y - FirstVertex.W) * ImportedSize.Y * UnitsPerPixel);
		if (OutCanvas.Pixels.ContainsNaN() || OutCanvas.CenterInPivotSpace.ContainsNaN())
		{
			return false;
		}
		// Reject mirrored/rotated atlas entries and malformed triangles instead of stretching
		// them into arbitrary geometry. Tight render bounds inside a full slice remain valid.
		for (const FVector4& Vertex : Sprite->BakedRenderData)
		{
			if (Vertex.Z < Atlas.StartUV.X - UE_KINDA_SMALL_NUMBER
				|| Vertex.Z > Atlas.StartUV.X + Atlas.SizeUV.X + UE_KINDA_SMALL_NUMBER
				|| Vertex.W < Atlas.StartUV.Y - UE_KINDA_SMALL_NUMBER
				|| Vertex.W > Atlas.StartUV.Y + Atlas.SizeUV.Y + UE_KINDA_SMALL_NUMBER)
			{
				return false;
			}
			const FVector Expected(OutCanvas.CenterInPivotSpace.X
				+ (Vertex.Z - CenterUV.X) * ImportedSize.X * UnitsPerPixel, 0.0,
				OutCanvas.CenterInPivotSpace.Z
				- (Vertex.W - CenterUV.Y) * ImportedSize.Y * UnitsPerPixel);
			if (!Expected.Equals(FVector(Vertex.X, 0.0, Vertex.Y), 0.01))
			{
				return false;
			}
		}
		return true;
	}

	bool GetSliceCanvases(const TArray<TObjectPtr<UPaperSprite>>& Sprites,
		TArray<FSliceCanvas>& OutCanvases, double& OutTotalPixels)
	{
		OutTotalPixels = 0.0;
		OutCanvases.Reset(Sprites.Num());
		for (UPaperSprite* Sprite : Sprites)
		{
			FSliceCanvas Canvas;
			if (!GetSliceCanvas(Sprite, Canvas)
				|| (!OutCanvases.IsEmpty() && !FMath::IsNearlyEqual(
					Canvas.Pixels.Y, OutCanvases[0].Pixels.Y, 0.01)))
			{
				return false;
			}
			OutTotalPixels += Canvas.Pixels.X;
			OutCanvases.Add(Canvas);
		}
		return !OutCanvases.IsEmpty() && FMath::IsFinite(OutTotalPixels)
			&& OutTotalPixels > UE_SMALL_NUMBER;
	}

	double GetCanvasSeamOverlap(const FVector& Before, const FVector& After, double HalfHeight)
	{
		const FVector BeforeDirection = Before.GetSafeNormal();
		const FVector AfterDirection = After.GetSafeNormal();
		if (BeforeDirection.IsNearlyZero() || AfterDirection.IsNearlyZero()) return 0.0;
		const double CosAngle = FMath::Clamp(FVector::DotProduct(BeforeDirection, AfterDirection), -1.0, 1.0);
		// Adjacent rigid sprite strips otherwise leave a triangular gap on the outside of a bend.
		// Extend their internal edges to the miter, using the full canvas height so transparent
		// margins and differently cropped geometry keep their common artwork baseline.
		if (1.0 - CosAngle <= UE_SMALL_NUMBER) return 0.0;
		return HalfHeight * FMath::Sqrt((1.0 - CosAngle) / FMath::Max(1.0 + CosAngle, UE_SMALL_NUMBER));
	}
}

AAssemblyMuscle::AAssemblyMuscle()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	CurveRoot = CreateDefaultSubobject<USceneComponent>(TEXT("CurveRoot"));
	SetRootComponent(CurveRoot);
	CurveRoot->SetMobility(EComponentMobility::Movable);
	CurveSpline = CreateDefaultSubobject<USplineComponent>(TEXT("CurveSpline"));
	CurveSpline->SetupAttachment(CurveRoot);
	CurveSpline->SetMobility(EComponentMobility::Movable);
	CurveSpline->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CurveSpline->SetGenerateOverlapEvents(false);
	CurveSpline->SetClosedLoop(false);
	CurveSpline->SetDrawDebug(false);
	static ConstructorHelpers::FObjectFinder<UPaperSprite> Placeholder(
		TEXT("/Game/Character/Assembly/SP_BonePlaceholder.SP_BonePlaceholder"));
	if (Placeholder.Succeeded())
	{
		CurveSprite = Placeholder.Object;
	}
}

UAssemblyComponent* AAssemblyMuscle::GetAssemblyComponent() const
{
	return BoundAssembly.Get();
}

bool AAssemblyMuscle::HasUsablePresentation() const
{
	if (!FMath::IsFinite(LineWidth) || LineWidth <= UE_SMALL_NUMBER
		|| !FMath::IsFinite(BendRatio) || !FMath::IsFinite(VisualDepthOffset))
	{
		return false;
	}
	if (!CurveSliceSprites.IsEmpty())
	{
		if (CurveSliceSprites.Num() < 4 || CurveSliceSprites.Num() > 64)
		{
			return false;
		}
		if (bUseArtworkProportions)
		{
			TArray<FSliceCanvas> Canvases;
			double TotalPixels;
			return GetSliceCanvases(CurveSliceSprites, Canvases, TotalPixels);
		}
		for (UPaperSprite* Slice : CurveSliceSprites)
		{
			if (!HasUsableSprite(Slice))
			{
				return false;
			}
		}
		return true;
	}
	return HasUsableSprite(CurveSprite);
}

void AAssemblyMuscle::EnsureSegmentSprites()
{
	const int32 Count = CurveSliceSprites.IsEmpty()
		? FMath::Clamp(CurveSegments, 4, 64) : CurveSliceSprites.Num();
	while (SegmentSprites.Num() > Count)
	{
		if (UPaperSpriteComponent* Segment = SegmentSprites.Pop())
		{
			RemoveInstanceComponent(Segment);
			Segment->DestroyComponent();
		}
	}
	while (SegmentSprites.Num() < Count)
	{
		UPaperSpriteComponent* Segment = NewObject<UPaperSpriteComponent>(this, NAME_None, RF_Transient);
		AddInstanceComponent(Segment);
		Segment->SetMobility(EComponentMobility::Movable);
		Segment->SetupAttachment(CurveRoot);
		Segment->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Segment->SetGenerateOverlapEvents(false);
		Segment->SetCastShadow(false);
		Segment->RegisterComponent();
		SegmentSprites.Add(Segment);
	}
}

void AAssemblyMuscle::HideCurve()
{
	for (UPaperSpriteComponent* Segment : SegmentSprites)
	{
		if (Segment)
		{
			Segment->SetVisibility(false);
		}
	}
	CurveSpline->ClearSplinePoints(true);
}

FLinearColor AAssemblyMuscle::GetFeedbackColor() const
{
	return IsMuscleInitialized() ? (MuscleData.bContracting ? ContractingColor : RelaxedColor)
		: (bPreviewValid ? ValidPreviewColor : InvalidPreviewColor);
}

bool AAssemblyMuscle::GetCurveEndpoints(FVector& OutWorldA, FVector& OutWorldB) const
{
	OutWorldA = FVector::ZeroVector;
	OutWorldB = FVector::ZeroVector;
	if (IsMuscleInitialized())
	{
		return BoundAssembly->GetEndpointWorldPosition(MuscleData.EndpointA, OutWorldA)
			&& BoundAssembly->GetEndpointWorldPosition(MuscleData.EndpointB, OutWorldB)
			&& HasUsableEndpoints(OutWorldA, OutWorldB);
	}
	// Unconfigured placed Actors display a local preview; drag previews retain supplied world anchors.
	OutWorldA = bHasWorldPreview ? PreviewWorldA : GetActorTransform().TransformPosition(FVector(-64.0, 0.0, 0.0));
	OutWorldB = bHasWorldPreview ? PreviewWorldB : GetActorTransform().TransformPosition(FVector(64.0, 0.0, 0.0));
	return HasUsableEndpoints(OutWorldA, OutWorldB);
}

void AAssemblyMuscle::RefreshCurve()
{
	FVector A, B;
	if (!HasUsablePresentation() || !GetCurveEndpoints(A, B))
	{
		HideCurve();
		return;
	}
	EnsureSegmentSprites();
	const FVector Chord = B - A;
	// Local XZ is the game's plane. Preserve exact anchors instead of changing any bone pose.
	const FVector Side(-Chord.Z, 0.0, Chord.X);
	const FVector Middle = (A + B) * 0.5
		+ Side.GetSafeNormal() * Chord.Size() * FMath::Clamp(BendRatio, -1.0f, 1.0f);
	const TArray<FVector> Points = { A, Middle, B };
	CurveSpline->SetSplinePoints(Points, ESplineCoordinateSpace::World, false);
	// A straight authored image needs exact arc-length slices. The curved tangent convention
	// has nonuniform parameter speed even with zero bend, so its sampled distance table drifts.
	const bool bStraightArtwork = bUseArtworkProportions && !CurveSliceSprites.IsEmpty() && FMath::IsNearlyZero(BendRatio);
	for (int32 Point = 0; Point < Points.Num(); ++Point)
	{
		const FVector Tangent = Point == 0 ? (Middle - A) * 2.0
			: Point == 2 ? (B - Middle) * 2.0 : Chord;
		CurveSpline->SetTangentsAtSplinePoint(Point, Tangent, Tangent, ESplineCoordinateSpace::World, false);
		// SetTangents switches interpolation back to CurveUser, so select Linear afterwards.
		CurveSpline->SetSplinePointType(Point, bStraightArtwork ? ESplinePointType::Linear : ESplinePointType::CurveCustomTangent, false);
	}
	CurveSpline->UpdateSpline();
	const float SplineLength = CurveSpline->GetSplineLength();
	const FLinearColor Color = GetFeedbackColor();
	const int32 Count = SegmentSprites.Num();
	const bool bUseSlices = !CurveSliceSprites.IsEmpty();
	const bool bUseCanvasProportions = bUseSlices && bUseArtworkProportions;
	TArray<FSliceCanvas> Canvases;
	double TotalPixels = 0.0;
	if (!FMath::IsFinite(SplineLength) || SplineLength <= UE_SMALL_NUMBER
		|| (bUseCanvasProportions && !GetSliceCanvases(CurveSliceSprites, Canvases, TotalPixels)))
	{
		HideCurve();
		return;
	}
	const UAssemblyComponent* RenderAssembly = BoundAssembly.Get();
	if (!RenderAssembly && GetOwner()) RenderAssembly = GetOwner()->FindComponentByClass<UAssemblyComponent>();
	const FVector DepthNormal = RenderAssembly
		? RenderAssembly->GetGridFrameTransform().GetUnitAxis(EAxis::Y) : FVector::YAxisVector;
	const AssemblyRenderLayers::FCounts Counts = RenderAssembly
		? AssemblyRenderLayers::FCounts{RenderAssembly->GetBones().Num(), RenderAssembly->GetMuscles().Num(), RenderAssembly->GetJoints().Num()}
		: AssemblyRenderLayers::FCounts{};
	const int32 Rank = IsMuscleInitialized()
		? AssemblyRenderLayers::GetRank(BoundAssembly->GetMuscles(), MuscleData.InstanceId) : 0;
	TArray<FVector> CanvasPoints;
	if (bUseCanvasProportions)
	{
		CanvasPoints.Reserve(Count + 1);
		CanvasPoints.Add(A);
		double DistancePixels = 0.0;
		for (const FSliceCanvas& Canvas : Canvases)
		{
			DistancePixels += Canvas.Pixels.X;
			CanvasPoints.Add(CurveSpline->GetLocationAtDistanceAlongSpline(
				SplineLength * DistancePixels / TotalPixels, ESplineCoordinateSpace::World));
		}
		CanvasPoints.Last() = B;
	}
	double PixelOffset = 0.0;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		UPaperSpriteComponent* Segment = SegmentSprites[Index];
		UPaperSprite* Sprite = bUseSlices ? CurveSliceSprites[Index].Get() : CurveSprite.Get();
		const FBoxSphereBounds Bounds = Sprite->GetRenderBounds();
		const double StartFraction = bUseCanvasProportions ? PixelOffset / TotalPixels
			: static_cast<double>(Index) / Count;
		if (bUseCanvasProportions) PixelOffset += Canvases[Index].Pixels.X;
		const double EndFraction = bUseCanvasProportions ? PixelOffset / TotalPixels
			: static_cast<double>(Index + 1) / Count;
		const FVector Start = bUseCanvasProportions ? CanvasPoints[Index]
			: CurveSpline->GetLocationAtDistanceAlongSpline(SplineLength * StartFraction, ESplineCoordinateSpace::World);
		const FVector End = bUseCanvasProportions ? CanvasPoints[Index + 1]
			: CurveSpline->GetLocationAtDistanceAlongSpline(SplineLength * EndFraction, ESplineCoordinateSpace::World);
		const FVector Delta = End - Start;
		if (Delta.IsNearlyZero())
		{
			Segment->SetVisibility(false);
			continue;
		}
		Segment->SetSprite(Sprite);
		Segment->SetMaterial(0, CurveMaterial ? CurveMaterial.Get() : Sprite->GetDefaultMaterial());
		Segment->SetSpriteColor(Color);
		Segment->SetTranslucentSortPriority(TranslucencySortPriority);
		const FQuat Rotation = FRotationMatrix::MakeFromXY(Delta, FVector(0.0, 1.0, 0.0)).ToQuat();
		// Artwork receives only the overlap required at each internal turn; placeholders retain
		// their original LineWidth overlap. Neither mode extends its two physical end anchors.
		const FVector Direction = Delta.GetSafeNormal();
		const double SeamOverlap = bUseSlices ? 0.0 : LineWidth * 0.1;
		double StartOverlap = Index > 0 ? SeamOverlap : 0.0;
		double EndOverlap = Index + 1 < Count ? SeamOverlap : 0.0;
		if (bUseCanvasProportions && !bStraightArtwork)
		{
			const double HalfHeight = Canvases[Index].Pixels.Y * 0.5 * SplineLength / TotalPixels;
			if (Index > 0) StartOverlap = GetCanvasSeamOverlap(Start - CanvasPoints[Index - 1], Delta, HalfHeight);
			if (Index + 1 < Count) EndOverlap = GetCanvasSeamOverlap(Delta, CanvasPoints[Index + 2] - End, HalfHeight);
		}
		const FVector DrawStart = Start - Direction * StartOverlap;
		const FVector DrawEnd = End + Direction * EndOverlap;
		const FVector Scale = bUseCanvasProportions
			? FVector(FVector::Distance(DrawStart, DrawEnd) / (Canvases[Index].Pixels.X * Canvases[Index].UnitsPerPixel),
				1.0, SplineLength / (TotalPixels * Canvases[Index].UnitsPerPixel))
			: FVector(FVector::Distance(DrawStart, DrawEnd) / (Bounds.BoxExtent.X * 2.0),
				1.0, LineWidth / (Bounds.BoxExtent.Z * 2.0));
		// Every muscle gets a disjoint depth slot; its slices receive stable depths inside it.
		// The spline and physical endpoints retain their exact positions on the gameplay plane.
		const double Depth = AssemblyRenderLayers::MuscleDepth(Counts, Rank, Index, !IsMuscleInitialized(), VisualDepthOffset);
		const FVector SourceCenter = bUseCanvasProportions ? Canvases[Index].CenterInPivotSpace : Bounds.Origin;
		const FVector Location = (DrawStart + DrawEnd) * 0.5 - Rotation.RotateVector(SourceCenter * Scale) + DepthNormal * Depth;
		Segment->SetWorldTransform(FTransform(Rotation, Location, Scale));
		Segment->SetVisibility(true);
	}
}

bool AAssemblyMuscle::InitializeMuscle(UAssemblyComponent* Assembly, FGuid InstanceId, FText& OutReason)
{
	OutReason = FText::GetEmpty();
	if (IsMuscleInitialized() && (BoundAssembly.Get() != Assembly || MuscleData.InstanceId != InstanceId))
	{
		OutReason = LOCTEXT("AlreadyBound", "此 Actor 已绑定另一肌肉实例；重新使用前请调用 ClearAssemblyBinding。");
		return false;
	}
	FAssemblyMuscleInstance Data;
	FVector A, B;
	if (!IsValid(Assembly) || !Assembly->IsInitialized() || Assembly->GetWorld() != GetWorld()
		|| !Assembly->GetMuscle(InstanceId, Data))
	{
		OutReason = LOCTEXT("MissingMuscle", "请提供已初始化的 AssemblyComponent 和成功安装的肌肉实例 ID。");
		return false;
	}
	if (!HasUsablePresentation() || !Assembly->GetEndpointWorldPosition(Data.EndpointA, A)
		|| !Assembly->GetEndpointWorldPosition(Data.EndpointB, B) || !HasUsableEndpoints(A, B))
	{
		HideCurve();
		OutReason = LOCTEXT("InvalidCurve", "肌肉端点、曲线 Sprite、线宽或弯曲参数无效；切片数组必须为空或包含 4–64 张有效 Sprite，保留比例时切片需使用相同画布高度与未旋转的有效几何。");
		return false;
	}
	BoundAssembly = Assembly;
	MuscleData = Data;
	Assembly->OnAssemblyChanged.AddUniqueDynamic(this, &AAssemblyMuscle::HandleAssemblyChanged);
	Assembly->OnAssemblyReset.AddUniqueDynamic(this, &AAssemblyMuscle::HandleAssemblyReset);
	Assembly->OnMuscleDriveRequested.AddUniqueDynamic(this, &AAssemblyMuscle::HandleMuscleDriveRequested);
	RefreshCurve();
	return true;
}

bool AAssemblyMuscle::SetPreviewEndpoints(FVector WorldA, FVector WorldB, FText& OutReason)
{
	OutReason = FText::GetEmpty();
	if (IsMuscleInitialized() || !HasUsableEndpoints(WorldA, WorldB))
	{
		OutReason = LOCTEXT("InvalidPreview", "预览端点必须为两个不同的有限世界坐标，且此 Actor 尚未绑定安装实例。");
		return false;
	}
	PreviewWorldA = WorldA;
	PreviewWorldB = WorldB;
	bHasWorldPreview = true;
	RefreshCurve();
	return true;
}

void AAssemblyMuscle::SetPreviewValid(bool bValid)
{
	bPreviewValid = bValid;
	RefreshCurve();
}

void AAssemblyMuscle::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RefreshCurve();
}

void AAssemblyMuscle::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (MuscleData.InstanceId.IsValid() && !BoundAssembly.IsValid())
	{
		ClearAssemblyBinding();
		Destroy();
		return;
	}
	if (IsMuscleInitialized())
	{
		RefreshCurve();
	}
}

void AAssemblyMuscle::ClearAssemblyBinding()
{
	if (UAssemblyComponent* Assembly = BoundAssembly.Get())
	{
		Assembly->OnAssemblyChanged.RemoveDynamic(this, &AAssemblyMuscle::HandleAssemblyChanged);
		Assembly->OnAssemblyReset.RemoveDynamic(this, &AAssemblyMuscle::HandleAssemblyReset);
		Assembly->OnMuscleDriveRequested.RemoveDynamic(this, &AAssemblyMuscle::HandleMuscleDriveRequested);
	}
	BoundAssembly.Reset();
	MuscleData = FAssemblyMuscleInstance();
	bHasWorldPreview = false;
	HideCurve();
}

void AAssemblyMuscle::HandleAssemblyChanged()
{
	FAssemblyMuscleInstance Current;
	if (!BoundAssembly.IsValid() || !BoundAssembly->GetMuscle(MuscleData.InstanceId, Current))
	{
		ClearAssemblyBinding();
		Destroy();
		return;
	}
	MuscleData = Current;
	RefreshCurve();
}

void AAssemblyMuscle::HandleAssemblyReset()
{
	ClearAssemblyBinding();
	Destroy();
}

void AAssemblyMuscle::HandleMuscleDriveRequested(FGuid MuscleId, bool bContracting)
{
	if (MuscleId == MuscleData.InstanceId)
	{
		MuscleData.bContracting = bContracting;
		RefreshCurve();
	}
}

void AAssemblyMuscle::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearAssemblyBinding();
	Super::EndPlay(EndPlayReason);
}

#undef LOCTEXT_NAMESPACE
