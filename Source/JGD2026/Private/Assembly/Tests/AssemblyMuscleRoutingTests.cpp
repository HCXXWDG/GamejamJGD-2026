#if WITH_DEV_AUTOMATION_TESTS

#include "Assembly/AssemblyMuscleRouting.h"
#include "Misc/AutomationTest.h"

namespace
{
	using namespace AssemblyMuscleRouting;
	constexpr auto MuscleRoutingTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
	struct FAntagonists
	{
		FAssemblyMuscleInstance Inner, Outer;
		FGuide InnerGuide, OuterGuide;
		TArray<FAssemblyJointInstance> Edges;
		FAntagonists()
		{
			FAssemblyBoneInstance A, B;
			A.InstanceId = FGuid::NewGuid(); B.InstanceId = FGuid::NewGuid();
			A.TypeId = B.TypeId = TEXT("Bone");
			A.LocalVisualTransform = FTransform(FVector(-128, 0, 0));
			B.LocalVisualTransform = FTransform(FQuat(FVector::YAxisVector, -UE_DOUBLE_PI / 2), FVector(0, 0, 128));
			FAssemblyJointInstance EdgeA, EdgeB;
			EdgeA.InstanceId = FGuid::NewGuid(); EdgeB.InstanceId = FGuid::NewGuid();
			EdgeA.BoneA = A.InstanceId; EdgeB.BoneA = B.InstanceId;
			Edges = {EdgeA, EdgeB}; // Their other endpoints are the core.
			Inner.EndpointA.BoneId = Outer.EndpointA.BoneId = A.InstanceId;
			Inner.EndpointB.BoneId = Outer.EndpointB.BoneId = B.InstanceId;
			Inner.EndpointA.BoneLocalPoint = FVector2D(24, 10);
			Inner.EndpointB.BoneLocalPoint = FVector2D(-24, 10);
			Outer.EndpointA.BoneLocalPoint = FVector2D(24, -10);
			Outer.EndpointB.BoneLocalPoint = FVector2D(-24, -10);
			FAssemblyFreeSettings Settings; Settings.CoreRadius = 40;
			BuildGuide(Inner, {A,B}, {FAssemblyBoneDefinition()}, Edges, Settings, InnerGuide);
			BuildGuide(Outer, {A,B}, {FAssemblyBoneDefinition()}, Edges, Settings, OuterGuide);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyAntagonistRouteTest, "JGD2026.Assembly.MuscleRouting.SideAndStraightLeverage", MuscleRoutingTestFlags)
bool FAssemblyAntagonistRouteTest::RunTest(const FString& Parameters)
{
	FAntagonists Pair;
	TestTrue(TEXT("A shared core produces two guides"), Pair.InnerGuide.bValid && Pair.OuterGuide.bValid);
	TestTrue(TEXT("Signed side attachments choose opposite routes"), Pair.InnerGuide.Sign == -Pair.OuterGuide.Sign);
	FState InnerState, OuterState;
	FPath Inner, Outer;
	TestTrue(TEXT("Inner bent path resolves"), Evaluate(Pair.InnerGuide, FVector(-104,0,10), FVector(-10,0,104), FTransform::Identity, InnerState, Inner));
	TestFalse(TEXT("Clear inner span stays direct"), Inner.bWrapped);
	TestTrue(TEXT("Outer bent path resolves"), Evaluate(Pair.OuterGuide, FVector(-104,0,-10), FVector(10,0,104), FTransform::Identity, OuterState, Outer));
	TestTrue(TEXT("Outer tendon goes around the far side"), Outer.bWrapped && Outer.Length > Inner.Length);
	for (double Degrees : {90., 135., 179.9, 180., 180.1, 225., 270.})
	{
		const double Angle = FMath::DegreesToRadians(180. - Degrees);
		const FVector Radial(FMath::Cos(Angle), 0, FMath::Sin(Angle));
		const FVector Side(-Radial.Z, 0, Radial.X);
		const FVector IA(-104,0,10), OA(-104,0,-10);
		const FVector IB = Radial * 104 + Side * 10;
		const FVector OB = Radial * 104 - Side * 10;
		TestTrue(TEXT("Inner route stays evaluable through straight"), Evaluate(Pair.InnerGuide, IA, IB, FTransform::Identity, InnerState, Inner));
		TestTrue(TEXT("Outer route stays evaluable through straight"), Evaluate(Pair.OuterGuide, OA, OB, FTransform::Identity, OuterState, Outer));
		const double MomentInner = FVector::CrossProduct(IA, Inner.PullA).Y;
		const double MomentOuter = FVector::CrossProduct(OA, Outer.PullA).Y;
		if (Degrees > 179 && Degrees < 181)
		{
			TestTrue(TEXT("Both antagonists keep nonzero moment arms at straight"), FMath::Abs(MomentInner) > 39.9 && FMath::Abs(MomentOuter) > 39.9);
			TestTrue(TEXT("The two keys can turn the same bone in opposite directions"), MomentInner * MomentOuter < 0);
		}
		TestTrue(TEXT("Inner muscle never swaps to the opposite torque side"), MomentInner > 0);
		TestTrue(TEXT("Outer muscle never swaps to the opposite torque side"), MomentOuter < 0);
		for (int32 Index=1; Index<Inner.Points.Num(); ++Index)
			TestTrue(TEXT("Visual path stays outside the circular joint"),
				FMath::PointDistToSegmentSquared(FVector::ZeroVector, Inner.Points[Index-1], Inner.Points[Index]) >= 1600 - .01);
	}
	TestTrue(TEXT("Removing a guide edge releases the route"), IsGuideConnected(Pair.InnerGuide, Pair.Edges));
	Pair.Edges.Pop();
	TestFalse(TEXT("Disconnected joint cannot remain a pulley"), IsGuideConnected(Pair.InnerGuide, Pair.Edges));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyRouteGradientTest, "JGD2026.Assembly.MuscleRouting.ForceGradientAndReaction", MuscleRoutingTestFlags)
bool FAssemblyRouteGradientTest::RunTest(const FString& Parameters)
{
	FAntagonists Pair;
	const FVector A(-104,0,-10), B(10,0,104);
	FState State; FPath Path;
	if (!TestTrue(TEXT("Outer path has an actual arc"), Evaluate(Pair.OuterGuide, A, B, FTransform::Identity, State, Path) && Path.bWrapped)) return false;
	for (FVector Offset : {FVector(.001,0,0), FVector(0,0,.001)})
	{
		FPath Moved; FState SampleState = State;
		Evaluate(Pair.OuterGuide, A+Offset, B, FTransform::Identity, SampleState, Moved);
		TestTrue(TEXT("Endpoint force is the negative path-length gradient"),
			FMath::IsNearlyEqual(Moved.Length-Path.Length, -FVector::DotProduct(Offset, Path.PullA), .000001));
		SampleState = State;
		Evaluate(Pair.OuterGuide, A, B, FTransform(FVector(Offset)), SampleState, Moved);
		TestTrue(TEXT("Guide reaction is the path-length gradient at the guide"),
			FMath::IsNearlyEqual(Moved.Length-Path.Length, FVector::DotProduct(Offset, Path.PullA+Path.PullB), .000001));
	}
	TestTrue(TEXT("A's pull is tangent to the joint"), FMath::Abs(FVector::DotProduct(Path.TouchA, Path.PullA)) < .0001);
	TestTrue(TEXT("B's pull is tangent to the joint"), FMath::Abs(FVector::DotProduct(Path.TouchB, Path.PullB)) < .0001);
	const FVector TotalMoment = FVector::CrossProduct(A, Path.PullA) + FVector::CrossProduct(B, Path.PullB)
		- FVector::CrossProduct(Path.TouchA, Path.PullA) - FVector::CrossProduct(Path.TouchB, Path.PullB);
	TestTrue(TEXT("Guide reactions preserve total angular momentum"), TotalMoment.IsNearlyZero(.0001));
	return true;
}

#endif
