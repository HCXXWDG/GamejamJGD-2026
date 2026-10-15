#if WITH_DEV_AUTOMATION_TESTS

#include "Assembly/NormalBone.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyReferenceMetricsTest,
	"JGD2026.NormalBone.ReferenceAspectPhysicalDimensionsAndMass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAssemblyReferenceMetricsTest::RunTest(const FString& Parameters)
{
	FNormalBoneParameters P;
	FNormalBonePhysicalStats Stats;
	FText Reason;
	TestTrue(TEXT("Legacy aspect zero still evaluates"), NormalBoneMetrics::Evaluate(P, Stats, Reason));
	TestTrue(TEXT("Legacy medium thickness remains 12.8"), FMath::IsNearlyEqual(Stats.Thickness, 12.8f));
	P.BodyAspectRatio = 315.0f / 957.0f;
	for (int32 Length = 0; Length < 3; ++Length)
	{
		P.Length = static_cast<EBoneLengthTier>(Length);
		for (int32 Thickness = 0; Thickness < 3; ++Thickness)
		{
			P.Thickness = static_cast<EBoneThicknessTier>(Thickness);
			TestTrue(TEXT("Every length/thickness supports explicit physical aspect"), NormalBoneMetrics::Evaluate(P, Stats, Reason));
			const float ExpectedLength = 64.0f * (1.0f + Length * 0.5f);
			const float ExpectedHeight = ExpectedLength * P.BodyAspectRatio * (Thickness + 1) * 0.5f;
			TestTrue(TEXT("Physical length retains the chosen trait"), FMath::IsNearlyEqual(Stats.Length, ExpectedLength));
			TestTrue(TEXT("Physical height uses the configured artwork baseline"), FMath::IsNearlyEqual(Stats.Thickness, ExpectedHeight, 0.0001f));
			TestTrue(TEXT("Mass follows the new actual body area"), FMath::IsNearlyEqual(Stats.MassKg,
				ExpectedLength * ExpectedHeight / (64.0f * 64.0f) * 2.0f, 0.0001f));
		}
	}
	for (float Invalid : { -1.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), MAX_flt })
	{
		P.BodyAspectRatio = Invalid;
		TestFalse(TEXT("Invalid/overflowing aspect is rejected"), NormalBoneMetrics::Evaluate(P, Stats, Reason));
		TestFalse(TEXT("Failure clears stale geometry"), Stats.bValid);
	}
	P = FNormalBoneParameters();
	P.BodyAspectRatio = 315.0f / 957.0f;
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	ANormalBone* Bone = World->SpawnActor<ANormalBone>();
	FAssemblyBoneDefinition Definition;
	Definition.BoneParameters = P;
	TestTrue(TEXT("Body accepts the reference physical definition"), Bone->ApplyRigidPartDefinition(Definition, Reason));
	TestTrue(TEXT("Collision half-width agrees with metrics"), FMath::IsNearlyEqual(
		Bone->GetBoneBody()->GetUnscaledBoxExtent().Z, 128.0 * (315.0 / 957.0) * 0.5, 0.0001));
	TestTrue(TEXT("Endpoint stays on the 128-unit physical length"), Bone->GetEndLocalPosition(EBoneEnd::End).Equals(FVector2D(64, 0), 0.0001));
	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	return true;
}

#endif
