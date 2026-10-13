#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineBaseTypes.h"
#include "Engine/EngineTypes.h"
#include "Assembly/AssemblyTypes.h"
#include "Assembly/AssemblyWorkbenchTypes.h"
#include "AssemblySessionComponent.generated.h"

class APlayerController;
class UAssemblyComponent;
class UAssemblyInteractionComponent;
class UAssemblyPhysicsBridgeComponent;
class UAssemblyWorkbenchComponent;
class UAssemblyHUDWidget;
class UEnhancedInputComponent;
class UEnhancedInputLocalPlayerSubsystem;
class UInputMappingContext;
class UInputAction;
class UGameViewportClient;
class UCharacterMovementComponent;
class UCapsuleComponent;
class SWidget;

/** Local assembly lifecycle and input. Reuses the character's model, presentation and camera. */
UCLASS(BlueprintType, Blueprintable, ClassGroup = (Assembly), meta = (BlueprintSpawnableComponent))
class JGD2026_API UAssemblySessionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAssemblySessionComponent();
	// Automatic initialization runs after the owning pawn's BeginPlay and local possession.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Session")
	bool bAutoInitialize = true;
	// Assembly movement belongs to the core/rigid bodies in both phases, never WASD locomotion.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Physics")
	bool bSuspendCharacterMovement = true;
	// Takes effect only with a registered physics lifecycle provider. Never disables a real body.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Physics")
	bool bDisableCharacterCapsuleForPhysics = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|UI")
	bool bCreateHUD = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|UI")
	TSubclassOf<UAssemblyHUDWidget> HUDClass;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|UI")
	int32 HUDZOrder = 10;
	// Applies only to a HUD created by this component. Provided HUDs retain their input configuration.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|UI")
	bool bConfigureHUDInputMode = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Input")
	TObjectPtr<UInputMappingContext> InputMappingContext;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Input")
	int32 MappingPriority = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Input")
	TObjectPtr<UInputAction> WAction;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Input")
	TObjectPtr<UInputAction> AAction;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Input")
	TObjectPtr<UInputAction> SAction;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Input")
	TObjectPtr<UInputAction> DAction;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Input")
	float RotationStep = 90.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Input")
	bool bBindBackspaceToRestart = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|Input")
	bool bBindJointBreakKey = true;
	UPROPERTY(BlueprintAssignable, Category = "Assembly|Events")
	FAssemblyWorkbenchChanged OnSessionChanged;
	UPROPERTY(BlueprintAssignable, Category = "Assembly|Events")
	FAssemblyWorkbenchMessage OnMessage;

	UFUNCTION(BlueprintCallable, Category = "Assembly|Session")
	bool InitializeSession(APlayerController* PC, FText& OutReason);
	// Explicit shutdown suspends automatic initialization until InitializeSession is called again.
	UFUNCTION(BlueprintCallable, Category = "Assembly|Session")
	void ShutdownSession();
	UFUNCTION(BlueprintCallable, Category = "Assembly|Session")
	bool StartPlaying(FText& OutReason);
	UFUNCTION(BlueprintCallable, Category = "Assembly|Session")
	bool RestartSession(FText& OutReason);
	UFUNCTION(BlueprintPure, Category = "Assembly|Session")
	UAssemblyWorkbenchComponent* GetWorkbench() const;
	UFUNCTION(BlueprintPure, Category = "Assembly|Session")
	UAssemblyComponent* GetAssemblyComponent() const;
	// Native bridge gate: a cached model alone does not mean its character proxy is ready or owned.
	bool CanOwnPhysicsLifecycle(const UAssemblyComponent* Model) const;
	UFUNCTION(BlueprintPure, Category = "Assembly|Session")
	APlayerController* GetPlayerController() const;
	UFUNCTION(BlueprintPure, Category = "Assembly|UI")
	UAssemblyHUDWidget* GetHUD() const;
	UFUNCTION(BlueprintCallable, Category = "Assembly|UI")
	void ShowMessage(FText Message);
	UFUNCTION(BlueprintCallable, Category = "Assembly|Input")
	void ClearHeldInput();
	// Initializes an existing user HUD; its viewport membership and input mode stay caller-owned.
	UFUNCTION(BlueprintCallable, Category = "Assembly|UI")
	void SetHUD(UAssemblyHUDWidget* InHUD);

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;

private:
	UPROPERTY(Transient)
	TWeakObjectPtr<UAssemblyComponent> BoundAssembly;
	UPROPERTY(Transient)
	TWeakObjectPtr<UAssemblyInteractionComponent> BoundInteraction;
	UPROPERTY(Transient)
	TWeakObjectPtr<UAssemblyPhysicsBridgeComponent> BoundPhysicsBridge;
	UPROPERTY(Transient)
	TWeakObjectPtr<UAssemblyWorkbenchComponent> BoundWorkbench;
	UPROPERTY(Transient)
	TWeakObjectPtr<APlayerController> BoundController;
	UPROPERTY(Transient)
	TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> BoundInputSubsystem;
	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> AddedMappingContext;
	UPROPERTY(Transient)
	TObjectPtr<UEnhancedInputComponent> SessionInput;
	UPROPERTY(Transient)
	TObjectPtr<UAssemblyHUDWidget> HUD;
	UPROPERTY(Transient)
	TWeakObjectPtr<UGameViewportClient> SavedHUDViewport;
	UPROPERTY(Transient)
	TWeakObjectPtr<UCharacterMovementComponent> SuspendedCharacterMovement;
	UPROPERTY(Transient)
	TWeakObjectPtr<UCapsuleComponent> DisabledCharacterCapsule;
	UPROPERTY(Transient)
	TWeakObjectPtr<UAssemblyPhysicsBridgeComponent> PendingProxyShutdownBridge;
	TArray<uint32> ActionBindingHandles;
	TSet<EAssemblyMuscleKey> HeldMuscleKeys;
	FDelegateHandle DeactivationHandle;
	FDelegateHandle BackgroundHandle;
	TWeakPtr<SWidget> PreviousKeyboardFocus;
	EMouseCaptureMode PreviousMouseCaptureMode = EMouseCaptureMode::NoCapture;
	EMouseLockMode PreviousMouseLockMode = EMouseLockMode::DoNotLock;
	bool bInitialized = false;
	bool bInitializing = false;
	bool bReleasing = false;
	bool bTransitioning = false;
	bool bAutoInitializationSuppressed = false;
	bool bOwnedHUD = false;
	bool bHUDAddedToViewport = false;
	bool bHUDInputConfigured = false;
	bool bPreviousShowMouseCursor = false;
	bool bPreviousHideCursorDuringCapture = false;
	bool bPreviousViewportIgnoreInput = false;
	bool bIgnoreMoveInputAdded = false;
	bool bSpaceHeld = false;
	bool bViewportWasFocused = false;
	bool bModelRefreshPending = false;
	bool bReplayHeldMusclesPending = false;
	bool bCharacterMovementSuspended = false;
	bool bCharacterCapsuleDisabled = false;
	bool bHadPhysicsBridgeBinding = false;
	bool bProxyRestorePending = false;
	bool bEndingPlay = false;
	bool bPreviousCharacterMovementTick = false;
	uint8 PreviousCharacterMovementMode = 0;
	uint8 PreviousCharacterCustomMovementMode = 0;
	ECollisionEnabled::Type PreviousCharacterCapsuleCollision = ECollisionEnabled::NoCollision;
	FText LastInitializationFailure;

	bool CanUseSession(FText& OutReason) const;
	bool IsCurrentSessionBinding(const UAssemblyComponent* Model, const UAssemblyInteractionComponent* Interaction,
		const UAssemblyWorkbenchComponent* Workbench, const APlayerController* PC) const;
	bool BindSessionInput(FText& OutReason);
	void UnbindSessionInput();
	bool EnsureHUD(FText& OutReason);
	void DetachHUD();
	void RestoreHUDInput();
	void ReleaseSession();
	void UpdateMovementGate();
	void UpdateCharacterProxyState();
	void SuspendCharacterMovement();
	void RestoreCharacterMovement();
	void RestoreCharacterCapsule();
	bool TryRestoreCharacterProxy();
	void ApplyHeldMuscleInput(bool bAllowTransition = false);
	bool IsSessionViewportFocused() const;
	bool CanAcceptInput(bool bAllowTransition = false) const;
	void HandleApplicationDeactivated();
	void HandleMusclePressed(EAssemblyMuscleKey Key);
	void HandleMuscleReleased(EAssemblyMuscleKey Key);
	void HandleSpacePressed();
	void HandleSpaceReleased();
	void HandleRotatePressed();
	void HandleCancelPressed();
	void HandleDeletePressed();
	void HandleRestartPressed();
	void HandleJointBreakPressed();
	UFUNCTION()
	void HandleModelChanged();
	UFUNCTION()
	void HandleModelReset();
	UFUNCTION()
	void HandlePhaseChanged(EAssemblyPhase Phase);
	UFUNCTION()
	void HandleWorkbenchChanged();
	UFUNCTION()
	void HandleWorkbenchMessage(FText Message);
};
