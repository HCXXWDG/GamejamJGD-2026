#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "JGDAudioTypes.h"
#include "JGDInputFeedbackLibrary.generated.h"

class USoundBase;

/** Short non-spatial input sounds, routed through the existing audio bus and master volume. */
UCLASS()
class JGD2026_API UJGDInputFeedbackLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UJGDInputFeedbackLibrary();
	UFUNCTION(BlueprintCallable, Category="JGD|Audio", meta=(WorldContext="WorldContextObject"))
	static FJGDAudioHandle PlayInputPress(const UObject* WorldContextObject);
	UFUNCTION(BlueprintCallable, Category="JGD|Audio", meta=(WorldContext="WorldContextObject"))
	static FJGDAudioHandle PlayInputRelease(const UObject* WorldContextObject);
private:
	UPROPERTY() TObjectPtr<USoundBase> PressSound;
	UPROPERTY() TObjectPtr<USoundBase> ReleaseSound;
};
