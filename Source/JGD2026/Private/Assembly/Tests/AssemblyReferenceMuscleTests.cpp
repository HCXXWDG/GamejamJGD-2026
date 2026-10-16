#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Assembly/AssemblyComponent.h"
#include "Assembly/AssemblyMuscle.h"
#include "Components/SplineComponent.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "PaperSprite.h"
#include "PaperSpriteComponent.h"
#include <limits>

namespace AssemblyReferenceMuscleTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	struct FTestWorld
	{
		UWorld* World;
		UAssemblyComponent* Assembly;
		FTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			AActor* Owner = World->SpawnActor<AActor>();
			Assembly = NewObject<UAssemblyComponent>(Owner);
			Assembly->bAutoEnterAssembly = false;
			Owner->AddInstanceComponent(Assembly);
			Assembly->RegisterComponent();
		}
		~FTestWorld()
		{
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}
	};

	// Four source rectangles share an 80-pixel-high canvas. Their render geometry is cropped
	// to deliberately different heights and offsets; two slices have non-centred pivots.
	// No source asset is changed, and runtime presentation reads only the baked atlas/vertices.
	TArray<TObjectPtr<UPaperSprite>> MakeSlices(bool bFullCanvasGeometry = false)
	{
		UTexture2D* Texture = NewObject<UTexture2D>(GetTransientPackage(), NAME_None, RF_Transient);
		TArray<uint8> Pixels;
		Pixels.SetNumZeroed(320 * 80 * 4);
		Texture->Source.Init(320, 80, 1, 1, TSF_BGRA8, Pixels.GetData());
		const int32 Widths[] = {32, 64, 96, 128};
		const int32 Tops[] = {36, 8, 20, 34};
		const int32 Bottoms[] = {44, 64, 80, 46};
		TArray<TObjectPtr<UPaperSprite>> Result;
		int32 Offset = 0;
		for (int32 Index = 0; Index < 4; ++Index)
		{
			UPaperSprite* Sprite = NewObject<UPaperSprite>(GetTransientPackage(), NAME_None, RF_Transient);
			FSpriteAssetInitParameters Init;
			Init.Texture = Texture;
			Init.Offset = FIntPoint(Offset, 0);
			Init.Dimension = FIntPoint(Widths[Index], 80);
			const double PixelsPerUnit = Index == 2 ? 2.0 : 1.0;
			Init.SetPixelsPerUnrealUnit(PixelsPerUnit);
			Sprite->InitializeSprite(Init, false);
			const FVector2D Pivot(Offset + (Index % 2 ? 0 : Widths[Index] * 0.5), Index % 2 ? 0 : 40);
			Sprite->SetPivotMode(ESpritePivotMode::Custom, Pivot, false);
			const int32 Top = bFullCanvasGeometry ? 0 : Tops[Index];
			const int32 Bottom = bFullCanvasGeometry ? 80 : Bottoms[Index];
			const FVector2D TL(0, Top), TR(Widths[Index], Top);
			const FVector2D BL(0, Bottom), BR(Widths[Index], Bottom);
			for (const FVector2D& Point : {TL, TR, BL, BL, TR, BR})
			{
				Sprite->BakedRenderData.Add(FVector4((Offset + Point.X - Pivot.X) / PixelsPerUnit,
					(Pivot.Y - Point.Y) / PixelsPerUnit, (Offset + Point.X) / 320.0, Point.Y / 80.0));
			}
			Result.Add(Sprite);
			Offset += Widths[Index];
		}
		return Result;
	}

	AAssemblyMuscle* MakeView(FTestWorld& T, const TArray<TObjectPtr<UPaperSprite>>& Slices)
	{
		AAssemblyMuscle* Muscle = T.World->SpawnActor<AAssemblyMuscle>();
		Muscle->CurveSliceSprites = Slices;
		Muscle->bUseArtworkProportions = true;
		Muscle->BendRatio = 0.0f;
		return Muscle;
	}

	UPaperSpriteComponent* FindSegment(AAssemblyMuscle* Muscle, UPaperSprite* Sprite)
	{
		TArray<UPaperSpriteComponent*> Segments;
		Muscle->GetComponents(Segments);
		for (UPaperSpriteComponent* Segment : Segments)
		{
			if (Segment->GetSprite() == Sprite) return Segment;
		}
		return nullptr;
	}

	FBox VisibleGeometry(UPaperSpriteComponent* Segment)
	{
		FBox Box(ForceInit);
		for (const FVector4& Vertex : Segment->GetSprite()->BakedRenderData)
		{
			Box += Segment->GetComponentTransform().TransformPosition(FVector(Vertex.X, 0, Vertex.Y));
		}
		return Box;
	}

	bool CoversPointOnGamePlane(UPaperSpriteComponent* Segment, const FVector& WorldPoint)
	{
		const FVector Point = Segment->GetComponentTransform().InverseTransformPosition(WorldPoint);
		const TArray<FVector4>& Vertices = Segment->GetSprite()->BakedRenderData;
		for (int32 Index = 0; Index < Vertices.Num(); Index += 3)
		{
			bool bPositive = false, bNegative = false;
			for (int32 Edge = 0; Edge < 3; ++Edge)
			{
				const FVector4& A = Vertices[Index + Edge];
				const FVector4& B = Vertices[Index + (Edge + 1) % 3];
				const double Cross = (B.X - A.X) * (Point.Z - A.Y) - (B.Y - A.Y) * (Point.X - A.X);
				bPositive |= Cross > 0.001;
				bNegative |= Cross < -0.001;
			}
			if (!(bPositive && bNegative)) return true;
		}
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyReferenceMuscleCanvasTest,
	"JGD2026.AssemblyViews.ReferenceMuscle.PreserveCanvasAndTrimmedSilhouette", AssemblyReferenceMuscleTests::Flags)
bool FAssemblyReferenceMuscleCanvasTest::RunTest(const FString& Parameters)
{
	using namespace AssemblyReferenceMuscleTests;
	FTestWorld T;
	const TArray<TObjectPtr<UPaperSprite>> Slices = MakeSlices();
	AAssemblyMuscle* Muscle = MakeView(T, Slices);
	FText Reason;
	const double Widths[] = {32, 64, 96, 128};
	const double Tops[] = {36, 8, 20, 34};
	const double Bottoms[] = {44, 64, 80, 46};
	for (double Length : {320.0, 640.0})
	{
		const FVector A(10, 0, 20), B(10 + Length, 0, 20);
		TestTrue(TEXT("Set physical endpoints"), Muscle->SetPreviewEndpoints(A, B, Reason));
		const double PixelScale = Length / 320.0;
		double PixelOffset = 0.0;
		for (int32 Index = 0; Index < Slices.Num(); ++Index)
		{
			UPaperSpriteComponent* Segment = FindSegment(Muscle, Slices[Index]);
			if (!TestNotNull(TEXT("Every source slice produces a segment"), Segment)) return false;
			TestTrue(TEXT("Valid source geometry is visible"), Segment->IsVisible());
			const FBox Box = VisibleGeometry(Segment);
			TestTrue(TEXT("Slice start is allocated by original source width"),
				FMath::IsNearlyEqual(Box.Min.X, A.X + PixelOffset * PixelScale, 0.01));
			TestTrue(TEXT("Unequal slice widths retain their correct share of the curve"),
				FMath::IsNearlyEqual(Box.Max.X, A.X + (PixelOffset + Widths[Index]) * PixelScale, 0.01));
			TestTrue(TEXT("Cropped top retains its original canvas baseline, independent of pivot and PPU"),
				FMath::IsNearlyEqual(Box.Max.Z, A.Z + (40.0 - Tops[Index]) * PixelScale, 0.01));
			TestTrue(TEXT("Different slice heights are never normalized to the same line width"),
				FMath::IsNearlyEqual(Box.Min.Z, A.Z + (40.0 - Bottoms[Index]) * PixelScale, 0.01));
			PixelOffset += Widths[Index];
		}
		USplineComponent* Spline = Muscle->GetCurveSpline();
		TestTrue(TEXT("Artwork scaling preserves the exact first physical anchor"),
			Spline->GetLocationAtSplinePoint(0, ESplineCoordinateSpace::World).Equals(A, 0.001));
		TestTrue(TEXT("Artwork scaling preserves the exact second physical anchor"),
			Spline->GetLocationAtSplinePoint(2, ESplineCoordinateSpace::World).Equals(B, 0.001));
	}
	Muscle->BendRatio = 0.3f;
	Muscle->RefreshCurve();
	const double ExpectedPixelScale = Muscle->GetCurveSpline()->GetSplineLength() / 320.0;
	for (UPaperSprite* Sprite : Slices)
	{
		UPaperSpriteComponent* Segment = FindSegment(Muscle, Sprite);
		TestTrue(TEXT("A bent curve uses total spline length for the common artwork scale"),
			FMath::IsNearlyEqual(Segment->GetComponentScale().Z * Sprite->GetUnrealUnitsPerPixel(), ExpectedPixelScale, 0.001));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyReferenceMuscleSeamTest,
	"JGD2026.AssemblyViews.ReferenceMuscle.CurvedSliceSeamsCoverTheOuterWedge", AssemblyReferenceMuscleTests::Flags)
bool FAssemblyReferenceMuscleSeamTest::RunTest(const FString& Parameters)
{
	using namespace AssemblyReferenceMuscleTests;
	FTestWorld T;
	const TArray<TObjectPtr<UPaperSprite>> Slices = MakeSlices(true);
	AAssemblyMuscle* Muscle = MakeView(T, Slices);
	const FVector A(15, 0, 20), B(300, 0, -80);
	FText Reason;
	for (float Bend : {0.15f, -0.3f, 0.6f})
	{
		Muscle->BendRatio = Bend;
		TestTrue(TEXT("Place a curved full-canvas muscle"), Muscle->SetPreviewEndpoints(A, B, Reason));
		USplineComponent* Spline = Muscle->GetCurveSpline();
		const double Length = Spline->GetSplineLength();
		const double HalfHeight = Length * 80.0 / 320.0 * 0.5;
		const double PixelBoundaries[] = {0, 32, 96, 192, 320};
		for (int32 Seam = 1; Seam < Slices.Num(); ++Seam)
		{
			const FVector Before = Spline->GetLocationAtDistanceAlongSpline(Length * PixelBoundaries[Seam - 1] / 320.0, ESplineCoordinateSpace::World);
			const FVector Center = Spline->GetLocationAtDistanceAlongSpline(Length * PixelBoundaries[Seam] / 320.0, ESplineCoordinateSpace::World);
			const FVector After = Spline->GetLocationAtDistanceAlongSpline(Length * PixelBoundaries[Seam + 1] / 320.0, ESplineCoordinateSpace::World);
			const FVector D0 = (Center - Before).GetSafeNormal(), D1 = (After - Center).GetSafeNormal();
			const FVector N0(-D0.Z, 0, D0.X), N1(-D1.Z, 0, D1.X);
			const FVector Bisector = (N0 + N1).GetSafeNormal();
			const double OuterDistance = HalfHeight / FVector::DotProduct(Bisector, N0);
			UPaperSpriteComponent* Left = FindSegment(Muscle, Slices[Seam - 1]);
			UPaperSpriteComponent* Right = FindSegment(Muscle, Slices[Seam]);
			if (!TestNotNull(TEXT("Left seam sprite exists"), Left)
				|| !TestNotNull(TEXT("Right seam sprite exists"), Right)) return false;
			// Sample both sides of the centreline up to 90% of the common outside edge. A
			// butt-joined pair fails on its convex side; only actual triangle coverage passes.
			for (int32 Side : {-1, 1})
			{
				for (int32 Step = 1; Step <= 9; ++Step)
				{
					const FVector Sample = Center + Bisector * OuterDistance * Side * (Step / 10.0);
					TestTrue(FString::Printf(TEXT("Bend %.2f seam %d side %d sample %d has no exposed wedge"), Bend, Seam, Side, Step),
						CoversPointOnGamePlane(Left, Sample) || CoversPointOnGamePlane(Right, Sample));
				}
			}
		}
		UPaperSpriteComponent* First = FindSegment(Muscle, Slices[0]);
		UPaperSpriteComponent* Last = FindSegment(Muscle, Slices.Last());
		const FVector4 FirstTop = Slices[0]->BakedRenderData[0], FirstBottom = Slices[0]->BakedRenderData[2];
		const FVector4 LastTop = Slices.Last()->BakedRenderData[1], LastBottom = Slices.Last()->BakedRenderData[5];
		const FVector RenderedStart = First->GetComponentTransform().TransformPosition(
			FVector((FirstTop.X + FirstBottom.X) * 0.5, 0, (FirstTop.Y + FirstBottom.Y) * 0.5));
		const FVector RenderedEnd = Last->GetComponentTransform().TransformPosition(
			FVector((LastTop.X + LastBottom.X) * 0.5, 0, (LastTop.Y + LastBottom.Y) * 0.5));
		TestTrue(TEXT("First artwork edge stays at its physical anchor"), FVector2D(RenderedStart.X, RenderedStart.Z).Equals(FVector2D(A.X, A.Z), 0.001));
		TestTrue(TEXT("Last artwork edge stays at its physical anchor"), FVector2D(RenderedEnd.X, RenderedEnd.Z).Equals(FVector2D(B.X, B.Z), 0.001));
		TestTrue(TEXT("The spline itself still starts at its exact physical endpoint"),
			Spline->GetLocationAtSplinePoint(0, ESplineCoordinateSpace::World).Equals(A, 0.001));
		TestTrue(TEXT("The spline itself still ends at its exact physical endpoint"),
			Spline->GetLocationAtSplinePoint(2, ESplineCoordinateSpace::World).Equals(B, 0.001));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyReferenceMuscleSafetyTest,
	"JGD2026.AssemblyViews.ReferenceMuscle.InvalidGeometryAndPlaceholderFallback", AssemblyReferenceMuscleTests::Flags)
bool FAssemblyReferenceMuscleSafetyTest::RunTest(const FString& Parameters)
{
	using namespace AssemblyReferenceMuscleTests;
	FTestWorld T;
	const TArray<TObjectPtr<UPaperSprite>> Slices = MakeSlices();
	AAssemblyMuscle* Muscle = MakeView(T, Slices);
	FText Reason;
	Muscle->SetPreviewEndpoints(FVector::ZeroVector, FVector(320, 0, 0), Reason);
	TArray<UPaperSpriteComponent*> Segments;
	Muscle->GetComponents(Segments);
	TestEqual(TEXT("One view exists for each slice"), Segments.Num(), 4);
	const FVector4 Saved = Slices[1]->BakedRenderData[0];
	Slices[1]->BakedRenderData[0].X = std::numeric_limits<double>::quiet_NaN();
	Muscle->RefreshCurve();
	TestEqual(TEXT("Non-finite baked geometry clears the stale spline"), Muscle->GetCurveSpline()->GetNumberOfSplinePoints(), 0);
	for (const UPaperSpriteComponent* Segment : Segments)
	{
		TestFalse(TEXT("Invalid geometry leaves no stale visible segments"), Segment->IsVisible());
	}
	Slices[1]->BakedRenderData[0] = Saved;
	Slices[1]->BakedRenderData[0].X += 3.0;
	Muscle->RefreshCurve();
	TestEqual(TEXT("A malformed XY/UV mapping is rejected"), Muscle->GetCurveSpline()->GetNumberOfSplinePoints(), 0);
	Slices[1]->BakedRenderData[0] = Saved;
	Muscle->RefreshCurve();
	TestEqual(TEXT("Valid data can recover without duplicating views"), Muscle->GetCurveSpline()->GetNumberOfSplinePoints(), 3);
	Segments.Reset();
	Muscle->GetComponents(Segments);
	TestEqual(TEXT("Recovery keeps exactly the original four views"), Segments.Num(), 4);
	Muscle->CurveSliceSprites.Reset();
	Muscle->CurveSprite = Slices[0];
	Muscle->CurveSegments = 4;
	Muscle->LineWidth = 7.0f;
	Muscle->RefreshCurve();
	Segments.Reset();
	Muscle->GetComponents(Segments);
	for (UPaperSpriteComponent* Segment : Segments)
	{
		TestTrue(TEXT("The empty-slice placeholder fallback retains its configured LineWidth"),
			FMath::IsNearlyEqual(Segment->GetComponentScale().Z
				* Segment->GetSprite()->GetRenderBounds().BoxExtent.Z * 2.0, 7.0, 0.001));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyReferenceMuscleLayerTest,
	"JGD2026.AssemblyViews.ReferenceMuscle.MultipleInstancesKeepLayersAndAnchors", AssemblyReferenceMuscleTests::Flags)
bool FAssemblyReferenceMuscleLayerTest::RunTest(const FString& Parameters)
{
	using namespace AssemblyReferenceMuscleTests;
	FTestWorld T;
	FText Reason;
	FAssemblyBoneDefinition Bone;
	Bone.TypeId = TEXT("ReferenceBone");
	Bone.BoneParameters.UnitLength = 256.0f;
	Bone.Footprint.Reset();
	FGuid BoneId;
	if (!TestTrue(TEXT("Configure a free assembly"), T.Assembly->ConfigureFreeAssembly(
		FAssemblyFreeSettings(), {Bone}, {FAssemblyMuscleDefinition()}, Reason))
		|| !TestTrue(TEXT("Install a core-connected bone"), T.Assembly->TryInstallFreeBone(Bone.TypeId, FVector2D(288, 0), 0.0f, BoneId, Reason))
		|| !TestTrue(TEXT("Choose a shared muscle key"), T.Assembly->SelectMuscleKey(EAssemblyMuscleKey::W, Reason)))
	{
		AddError(Reason.ToString());
		return false;
	}
	TArray<AAssemblyMuscle*> Muscles;
	const TArray<TObjectPtr<UPaperSprite>> Slices = MakeSlices();
	for (int32 Index = 0; Index < 2; ++Index)
	{
		FAssemblyMuscleEndpoint A, B;
		A.BoneId = BoneId;
		B.BoneId = BoneId;
		A.BoneLocalPoint = FVector2D(-100 + Index * 10, 0);
		B.BoneLocalPoint = FVector2D(100 - Index * 10, 0);
		FGuid MuscleId;
		if (!TestTrue(TEXT("Install a distinct muscle sharing this key"),
			T.Assembly->TryInstallMuscle(TEXT("Muscle"), A, B, MuscleId, Reason))) return false;
		AAssemblyMuscle* Muscle = MakeView(T, Slices);
		if (!TestTrue(TEXT("Bind the installed artwork view"), Muscle->InitializeMuscle(T.Assembly, MuscleId, Reason)))
		{
			AddError(Reason.ToString());
			return false;
		}
		Muscles.Add(Muscle);
	}
	TArray<FVector2D> DepthRanges;
	for (AAssemblyMuscle* Muscle : Muscles)
	{
		Muscle->RefreshCurve();
		double MinDepth = std::numeric_limits<double>::max(), MaxDepth = -MinDepth;
		TArray<UPaperSpriteComponent*> Segments;
		Muscle->GetComponents(Segments);
		for (UPaperSpriteComponent* Segment : Segments)
		{
			MinDepth = FMath::Min(MinDepth, Segment->GetComponentLocation().Y);
			MaxDepth = FMath::Max(MaxDepth, Segment->GetComponentLocation().Y);
		}
		DepthRanges.Add(FVector2D(MinDepth, MaxDepth));
		FVector A, B;
		TestTrue(TEXT("Installed anchors are available"), Muscle->GetCurveEndpoints(A, B));
		TestTrue(TEXT("Depth is applied only to artwork, never to the first spline anchor"),
			Muscle->GetCurveSpline()->GetLocationAtSplinePoint(0, ESplineCoordinateSpace::World).Equals(A, 0.001));
		TestTrue(TEXT("Depth is applied only to artwork, never to the second spline anchor"),
			Muscle->GetCurveSpline()->GetLocationAtSplinePoint(2, ESplineCoordinateSpace::World).Equals(B, 0.001));
	}
	TestTrue(TEXT("The complete depth ranges of two muscles never interleave or coincide"),
		DepthRanges[0].Y < DepthRanges[1].X || DepthRanges[1].Y < DepthRanges[0].X);
	TestEqual(TEXT("Rendering does not duplicate or consume inventory"), T.Assembly->GetRemainingQuantity(TEXT("Muscle")), 1);
	return true;
}

#endif
