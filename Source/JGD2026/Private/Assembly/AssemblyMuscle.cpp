#include "Assembly/AssemblyMuscle.h"
#include "Assembly/AssemblyComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SplineComponent.h"
#include "Materials/MaterialInterface.h"
#include "Math/RotationMatrix.h"
#include "PaperSprite.h"
#include "PaperSpriteComponent.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "AssemblyMuscle"

namespace
{
	bool HasUsableEndpoints(const FVector& A, const FVector& B)
	{
		return !A.ContainsNaN() && !B.ContainsNaN()
			&& FMath::IsFinite(FVector::DistSquared(A, B))
			&& FVector::DistSquared(A, B) > UE_KINDA_SMALL_NUMBER;
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
	if (!IsValid(CurveSprite) || !FMath::IsFinite(LineWidth) || LineWidth <= UE_SMALL_NUMBER
		|| !FMath::IsFinite(BendRatio))
	{
		return false;
	}
	const FBoxSphereBounds Bounds = CurveSprite->GetRenderBounds();
	return !Bounds.Origin.ContainsNaN() && !Bounds.BoxExtent.ContainsNaN()
		&& Bounds.BoxExtent.X > UE_SMALL_NUMBER && Bounds.BoxExtent.Z > UE_SMALL_NUMBER;
}

void AAssemblyMuscle::EnsureSegmentSprites()
{
	const int32 Count = FMath::Clamp(CurveSegments, 4, 64);
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
	for (int32 Point = 0; Point < Points.Num(); ++Point)
	{
		CurveSpline->SetSplinePointType(Point, ESplinePointType::CurveCustomTangent, false);
		const FVector Tangent = Point == 0 ? (Middle - A) * 2.0
			: Point == 2 ? (B - Middle) * 2.0 : Chord;
		CurveSpline->SetTangentsAtSplinePoint(Point, Tangent, Tangent, ESplineCoordinateSpace::World, false);
	}
	CurveSpline->UpdateSpline();
	const float SplineLength = CurveSpline->GetSplineLength();
	const FBoxSphereBounds Bounds = CurveSprite->GetRenderBounds();
	const FLinearColor Color = GetFeedbackColor();
	const int32 Count = SegmentSprites.Num();
	for (int32 Index = 0; Index < Count; ++Index)
	{
		UPaperSpriteComponent* Segment = SegmentSprites[Index];
		const FVector Start = CurveSpline->GetLocationAtDistanceAlongSpline(
			SplineLength * Index / Count, ESplineCoordinateSpace::World);
		const FVector End = CurveSpline->GetLocationAtDistanceAlongSpline(
			SplineLength * (Index + 1) / Count, ESplineCoordinateSpace::World);
		const FVector Delta = End - Start;
		if (Delta.IsNearlyZero())
		{
			Segment->SetVisibility(false);
			continue;
		}
		Segment->SetSprite(CurveSprite);
		Segment->SetMaterial(0, CurveMaterial ? CurveMaterial.Get() : CurveSprite->GetDefaultMaterial());
		Segment->SetSpriteColor(Color);
		Segment->SetTranslucentSortPriority(TranslucencySortPriority);
		const FQuat Rotation = FRotationMatrix::MakeFromXY(Delta, FVector(0.0, 1.0, 0.0)).ToQuat();
		// Overlap internal seams only; the first and last visible ends remain at their anchors.
		const FVector Direction = Delta.GetSafeNormal();
		const FVector DrawStart = Start - Direction * (Index > 0 ? LineWidth * 0.1 : 0.0);
		const FVector DrawEnd = End + Direction * (Index + 1 < Count ? LineWidth * 0.1 : 0.0);
		const FVector Scale(FVector::Distance(DrawStart, DrawEnd) / (Bounds.BoxExtent.X * 2.0),
			1.0, LineWidth / (Bounds.BoxExtent.Z * 2.0));
		const FVector Location = (DrawStart + DrawEnd) * 0.5 - Rotation.RotateVector(Bounds.Origin * Scale);
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
		OutReason = LOCTEXT("InvalidCurve", "肌肉端点或曲线 Sprite、线宽、弯曲参数无效。");
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
