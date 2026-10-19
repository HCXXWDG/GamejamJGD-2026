#if WITH_DEV_AUTOMATION_TESTS

#include "Audio/JGDMusicSettings.h"
#include "Misc/AutomationTest.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJGDMusicMapRoutingTest,
	"JGD2026.Audio.Music.MapRouting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FJGDMusicMapRoutingTest::RunTest(const FString& Parameters)
{
	// Use isolated settings and symbolic paths; no installed audio device or real assets needed.
	UJGDMusicSettings* Settings = NewObject<UJGDMusicSettings>();
	Settings->MenuMapNames = { TEXT("Menu"), TEXT("TitleScreen") };
	Settings->MenuMusic = TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Test/MenuTrack.MenuTrack")));
	Settings->GameplayMusic = TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Test/GameTrack.GameTrack")));
	TestEqual(TEXT("Menu selects the menu song"), Settings->GetMusicForMap(TEXT("Menu")), Settings->MenuMusic);
	TestEqual(TEXT("Additional configured menu maps are honored"), Settings->GetMusicForMap(TEXT("TitleScreen")), Settings->MenuMusic);
	TestEqual(TEXT("A long package name still identifies the menu"), Settings->GetMusicForMap(TEXT("/Game/Maps/Menu")), Settings->MenuMusic);
	TestEqual(TEXT("PIE menu names do not select gameplay by mistake"), Settings->GetMusicForMap(TEXT("UEDPIE_0_Menu")), Settings->MenuMusic);
	TestEqual(TEXT("Formal levels select gameplay music"), Settings->GetMusicForMap(TEXT("Level_0")), Settings->GameplayMusic);
	TestEqual(TEXT("The local assembly test map selects gameplay music"), Settings->GetMusicForMap(TEXT("Test")), Settings->GameplayMusic);
	TestEqual(TEXT("An unlisted new game map defaults to gameplay"), Settings->GetMusicForMap(TEXT("NewLevel")), Settings->GameplayMusic);
	Settings->MenuMapNames.Reset();
	TestEqual(TEXT("Removing menu routing intentionally uses gameplay"), Settings->GetMusicForMap(TEXT("Menu")), Settings->GameplayMusic);
	Settings->GameplayMusic.Reset();
	TestTrue(TEXT("An absent configured sound remains an empty selection"), Settings->GetMusicForMap(TEXT("Level_1")).IsNull());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJGDMusicLoudnessTest,
	"JGD2026.Audio.Music.Loudness",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FJGDMusicLoudnessTest::RunTest(const FString& Parameters)
{
	UJGDMusicSettings* Settings = NewObject<UJGDMusicSettings>();
	Settings->MusicLoudness = 0.6f;
	TestEqual(TEXT("Default music gain is retained"), Settings->GetSafeLoudness(), 0.6f);
	Settings->MusicLoudness = 0.25f;
	TestEqual(TEXT("A normal gain is retained"), Settings->GetSafeLoudness(), 0.25f);
	Settings->MusicLoudness = 0.0f;
	TestEqual(TEXT("Mute remains exactly zero"), Settings->GetSafeLoudness(), 0.0f);
	Settings->MusicLoudness = -1.0f;
	TestEqual(TEXT("Negative gain is clamped to mute"), Settings->GetSafeLoudness(), 0.0f);
	Settings->MusicLoudness = 2.0f;
	TestEqual(TEXT("Above-range gain is clamped"), Settings->GetSafeLoudness(), 1.0f);
	Settings->MusicLoudness = std::numeric_limits<float>::quiet_NaN();
	TestEqual(TEXT("NaN gain uses the safe default"), Settings->GetSafeLoudness(), 0.6f);
	Settings->MusicLoudness = std::numeric_limits<float>::infinity();
	TestEqual(TEXT("Positive infinity uses the safe default"), Settings->GetSafeLoudness(), 0.6f);
	Settings->MusicLoudness = -std::numeric_limits<float>::infinity();
	TestEqual(TEXT("Negative infinity uses the safe default"), Settings->GetSafeLoudness(), 0.6f);
	return true;
}

#endif
