#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "AssemblyPlayingToolbarWidget.generated.h"

class AGameModeBase;
class UAssemblySessionComponent;
class UAssemblyUITheme;
class UAssemblySpriteImage;
class UBorder;
class UButton;
class UCanvasPanelSlot;
class UCheckBox;
class UComboBoxString;
class UPrimitiveComponent;
class USceneComponent;
class USizeBox;
class USlider;
class UTextBlock;
class UJGDStarRatingWidget;

/** Playing-only presentation and local pause/settings ownership. Physics stays in its provider. */
UCLASS(BlueprintType)
class JGD2026_API UAssemblyPlayingToolbarWidget : public UUserWidget
{

	GENERATED_BODY()

public:
	void InitializeToolbar(UAssemblySessionComponent* InSession, UAssemblyUITheme* InTheme);
	void SetPlaying(bool bInPlaying);
	void ResetToolbarState();
	void CompleteLevel(int32 EarnedStars = 1, int32 BestStars = 1);
	UFUNCTION(BlueprintPure, Category="Assembly|UI") bool IsLevelComplete() const { return bLevelComplete; }
	bool IsOverControls(FVector2D AbsolutePosition) const;
	UFUNCTION(BlueprintPure, Category = "Assembly|UI")
	bool IsSettingsOpen() const { return bSettingsOpen; }
	UFUNCTION(BlueprintPure, Category = "Assembly|UI")
	bool IsManuallyPaused() const { return bManualPaused; }
	UFUNCTION(BlueprintPure, Category = "Assembly|UI")
	float GetRouteProgress() const { return RouteProgress; }
	UFUNCTION(BlueprintCallable, Category="JGD|Settings")
	void ShowStandaloneSettings();

	/** Soft map reference is also discovered by the cooker. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Assembly|Navigation")
	TSoftObjectPtr<UWorld> MainMenuMap = TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/Maps/Menu.Menu")));

protected:
	virtual void NativeTick(const FGeometry& Geometry, float DeltaTime) override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& MouseEvent) override;
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent) override;

private:
	UPROPERTY(Transient) TWeakObjectPtr<UAssemblySessionComponent> Session;
	UPROPERTY(Transient) TObjectPtr<UAssemblyUITheme> Theme;
	UPROPERTY(Transient) TObjectPtr<UBorder> ToolbarPanel;
	UPROPERTY(Transient) TObjectPtr<UBorder> SettingsOverlay;
	UPROPERTY(Transient) TObjectPtr<UBorder> CompletionOverlay;
	UPROPERTY(Transient) TObjectPtr<UJGDStarRatingWidget> CompletionStars;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CompletionRating;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CollectionCaption;
	UPROPERTY(Transient) TObjectPtr<UAssemblySpriteImage> PauseImage;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PauseCaption;
	UPROPERTY(Transient) TObjectPtr<UButton> PauseButton;
	UPROPERTY(Transient) TObjectPtr<UButton> RestartButton;
	UPROPERTY(Transient) TObjectPtr<UButton> MainMenuButton;
	UPROPERTY(Transient) TObjectPtr<UWidget> RouteContainer;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> RouteCaption;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> SettingsMessage;
	UPROPERTY(Transient) TObjectPtr<USlider> VolumeSlider;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> VolumeCaption;
	UPROPERTY(Transient) TObjectPtr<UComboBoxString> QualityChoice;
	UPROPERTY(Transient) TObjectPtr<UCheckBox> FullscreenChoice;
	UPROPERTY(Transient) TObjectPtr<USizeBox> RouteFillSize;
	UPROPERTY(Transient) TObjectPtr<UCanvasPanelSlot> RouteMarkerSlot;
	UPROPERTY(Transient) TWeakObjectPtr<AGameModeBase> PauseGameMode;
	UPROPERTY(Transient) TWeakObjectPtr<USceneComponent> RouteGoal;
	UPROPERTY(Transient) TWeakObjectPtr<UPrimitiveComponent> RouteCore;
	UPROPERTY() TSubclassOf<AActor> DestinationClass;
	FVector RouteStart = FVector::ZeroVector;
	float RouteProgress = 0.0f;
	float OriginalVolume = 1.0f;
	float RouteRefreshTime = 0.0f;
	bool bPlaying = false;
	bool bManualPaused = false;
	bool bSettingsOpen = false;
	bool bOwnPauseLease = false;
	bool bBuilt = false;
	bool bRouteAvailable = false;
	bool bPopulatingSettings = false;
	bool bSettingsApplied = false;
	bool bStandaloneSettings = false;
	bool bReturningToMenu = false;
	bool bLevelComplete = false;

	void BuildUI();
	UTextBlock* MakeText(const FText& Caption, int32 Size = 16);
	UButton* MakeArtButton(const FText& Caption, class UPaperSprite* Sprite,
		UAssemblySpriteImage** OutImage = nullptr, UTextBlock** OutCaption = nullptr);
	UButton* MakeTextButton(const FText& Caption);
	bool UpdatePauseState();
	bool CanReleasePause();
	void ReleaseOwnedPause();
	void RefreshPauseButton();
	void CaptureRoute();
	void RefreshRoute();
	void CloseSettings(bool bKeepChanges);
	void Report(const FText& Message);
	UFUNCTION() void HandlePause();
	UFUNCTION() void HandleSettings();
	UFUNCTION() void HandleRestart();
	UFUNCTION() void HandleApplySettings();
	UFUNCTION() void HandleCloseSettings();
	UFUNCTION() void HandleReturnToMenu();
	UFUNCTION() void HandleNextLevel();
	UFUNCTION() void HandleCompletionMenu();
	UFUNCTION() void HandleReplayLevel();
	void LeaveCompletedLevel(FName Map);
	UFUNCTION() void HandleVolumeChanged(float Value);
	UFUNCTION() UWidget* GenerateQualityOption(FString Option);
};
