#if WITH_DEV_AUTOMATION_TESTS

#include "Assembly/AssemblyPlayPreferences.h"
#include "Misc/AutomationTest.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyPlayingRouteProgressTest,
	"JGD2026.Assembly.PlayingToolbar.RouteProgress",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssemblyPlayingRouteProgressTest::RunTest(const FString& Parameters)
{
	using AssemblyPlayingHUD::CalculateRouteProgress;
	const FVector Start(0.0, 0.0, 0.0);
	const FVector Goal(60.0, 0.0, 80.0);
	TestEqual(TEXT("Spawn starts at zero"), CalculateRouteProgress(Start, Goal, Start), 0.0f);
	TestEqual(TEXT("Reaching the destination fills the bar"), CalculateRouteProgress(Start, Goal, Goal), 1.0f);
	TestEqual(TEXT("Half the initial remaining distance gives half progress"),
		CalculateRouteProgress(Start, Goal, FVector(30.0, 0.0, 40.0)), 0.5f);
	TestEqual(TEXT("Moving away is clamped at zero"),
		CalculateRouteProgress(Start, Goal, FVector(-60.0, 0.0, -80.0)), 0.0f);
	TestEqual(TEXT("An X coordinate alone does not complete a destination at another height"),
		CalculateRouteProgress(Start, Goal, FVector(60.0, 0.0, 0.0)), 0.2f);
	TestEqual(TEXT("Depth changes do not change planar progress"),
		CalculateRouteProgress(FVector(0.0, -90.0, 0.0), FVector(60.0, 700.0, 80.0), FVector(30.0, 8000.0, 40.0)), 0.5f);
	TestEqual(TEXT("Returning toward spawn reduces progress"),
		CalculateRouteProgress(Start, Goal, FVector(15.0, 0.0, 20.0)), 0.25f);
	TestEqual(TEXT("Coincident start and goal do not produce a false completed bar"),
		CalculateRouteProgress(Start, Start, Goal), 0.0f);
	const double NaN = std::numeric_limits<double>::quiet_NaN();
	const double Infinity = std::numeric_limits<double>::infinity();
	TestEqual(TEXT("Invalid start is rejected"), CalculateRouteProgress(FVector(NaN, 0.0, 0.0), Goal, Start), 0.0f);
	TestEqual(TEXT("Invalid goal is rejected"), CalculateRouteProgress(Start, FVector(0.0, 0.0, Infinity), Start), 0.0f);
	TestEqual(TEXT("Invalid core is rejected"), CalculateRouteProgress(Start, Goal, FVector(0.0, NaN, 0.0)), 0.0f);
	TestEqual(TEXT("Finite coordinate overflow is rejected"),
		CalculateRouteProgress(FVector(-MAX_dbl, 0.0, 0.0), FVector(MAX_dbl, 0.0, 0.0), Start), 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyPlayVolumePreferencesTest,
	"JGD2026.Assembly.PlayingToolbar.VolumePreferences",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssemblyPlayVolumePreferencesTest::RunTest(const FString& Parameters)
{
	// A transient instance avoids mutating the live settings CDO or writing user files.
	UAssemblyPlayPreferences* Preferences = NewObject<UAssemblyPlayPreferences>();
	TestNotNull(TEXT("A local preference instance can be constructed"), Preferences);
	if (!Preferences) { return false; }
	Preferences->SetMasterVolume(0.35f);
	TestEqual(TEXT("Normal volume is retained"), Preferences->GetMasterVolume(), 0.35f);
	Preferences->SetMasterVolume(-1.0f);
	TestEqual(TEXT("Negative volume is clamped to mute"), Preferences->GetMasterVolume(), 0.0f);
	Preferences->SetMasterVolume(4.0f);
	TestEqual(TEXT("Volume above one is clamped"), Preferences->GetMasterVolume(), 1.0f);
	Preferences->SetMasterVolume(std::numeric_limits<float>::quiet_NaN());
	TestEqual(TEXT("NaN uses the safe default"), Preferences->GetMasterVolume(), 1.0f);
	Preferences->SetMasterVolume(std::numeric_limits<float>::infinity());
	TestEqual(TEXT("Infinite volume uses the safe default"), Preferences->GetMasterVolume(), 1.0f);
	Preferences->SetMasterVolume(-std::numeric_limits<float>::infinity());
	TestEqual(TEXT("Negative infinite volume uses the safe default"), Preferences->GetMasterVolume(), 1.0f);
	// The no-world/no-device route must remain safe in menus, commandlets and -NoSound.
	Preferences->ApplyAudio(nullptr);
	Preferences->ApplyAudio(Preferences);
	return true;
}

#endif
