#include "Audio/JGDInputFeedbackLibrary.h"
#include "Audio/JGDAudioSubsystem.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

UJGDInputFeedbackLibrary::UJGDInputFeedbackLibrary()
{
	static ConstructorHelpers::FObjectFinder<USoundBase> Press(TEXT("/Game/Res/Audio/UI/S_UI_KeyPress.S_UI_KeyPress"));
	static ConstructorHelpers::FObjectFinder<USoundBase> Release(TEXT("/Game/Res/Audio/UI/S_UI_KeyRelease.S_UI_KeyRelease"));
	PressSound = Press.Object;
	ReleaseSound = Release.Object;
}

FJGDAudioHandle UJGDInputFeedbackLibrary::PlayInputPress(const UObject* WorldContextObject)
{
	UJGDInputFeedbackLibrary const* Sounds = GetDefault<UJGDInputFeedbackLibrary>();
	if (UJGDAudioSubsystem* Bus = UJGDAudioSubsystem::Get(WorldContextObject); Bus && Sounds->PressSound)
		return Bus->PlayAudio2D(Sounds->PressSound, 0.45f);
	return FJGDAudioHandle();
}

FJGDAudioHandle UJGDInputFeedbackLibrary::PlayInputRelease(const UObject* WorldContextObject)
{
	const auto* Sounds = GetDefault<UJGDInputFeedbackLibrary>();
	if (UJGDAudioSubsystem* Bus = UJGDAudioSubsystem::Get(WorldContextObject); Bus && Sounds->ReleaseSound)
		return Bus->PlayAudio2D(Sounds->ReleaseSound, 0.3f);
	return FJGDAudioHandle();
}
