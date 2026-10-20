#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Assembly/AssemblyTypes.h"
#include "Assembly/AssemblyWorkbenchTypes.h"
#include "AssemblyHUDWidget.generated.h"

class UAssemblySessionComponent;
class UAssemblyWorkbenchComponent;
class UAssemblyShelfCardWidget;
class UAssemblyUITheme;
class UAssemblyPlayingToolbarWidget;
class UVerticalBox;
class UTextBlock;
class UButton;
class USpinBox;
class UBorder;

UENUM()
enum class EAssemblyHUDAction : uint8
{
	Start, Restart, Cancel, Move, Remove, Rotate, Binding, SelectJoint, JointKind, ApplyLimits, BreakJoint
};

class UAssemblyHUDWidget;

/** Pointer-only controls leave WASD and Space available to the player's input component. */
UCLASS()
class JGD2026_API UAssemblyPointerButton : public UButton
{
	GENERATED_BODY()
public:
	UAssemblyPointerButton(const FObjectInitializer& ObjectInitializer);
};

/** Per-button receiver keeps IDs out of widget names and avoids global input bindings. */
UCLASS()
class JGD2026_API UAssemblyHUDActionReceiver : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY(Transient)
	TWeakObjectPtr<UAssemblyHUDWidget> HUD;
	EAssemblyHUDAction Action = EAssemblyHUDAction::Cancel;
	int32 Argument = 0;
	FGuid InstanceId;
	UFUNCTION()
	void Invoke();
};

/** Blueprint UI base and optional functional placeholder. Assembly always happens in the game world. */
UCLASS(BlueprintType, Blueprintable)
class JGD2026_API UAssemblyHUDWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UAssemblyHUDWidget(const FObjectInitializer& ObjectInitializer);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|UI")
	TObjectPtr<UAssemblyUITheme> UITheme;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|UI")
	bool bBuildPlaceholderUI = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|UI")
	bool bForwardWorldPointerEvents = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assembly|UI")
	TSubclassOf<UAssemblyShelfCardWidget> ShelfCardClass;
	UFUNCTION(BlueprintCallable, Category = "Assembly|UI")
	void InitializeHUD(UAssemblySessionComponent* InSession);
	UFUNCTION(BlueprintCallable, Category = "Assembly|UI")
	void DetachHUD();
	UFUNCTION(BlueprintCallable, Category = "Assembly|UI")
	void RefreshPresentation();
	UFUNCTION(BlueprintPure, Category = "Assembly|UI")
	UAssemblySessionComponent* GetSession() const { return Session.Get(); }
	UFUNCTION(BlueprintPure, Category = "Assembly|UI")
	UAssemblyWorkbenchComponent* GetWorkbench() const;
	UFUNCTION(BlueprintImplementableEvent, Category = "Assembly|UI")
	void OnHUDInitialized(UAssemblySessionComponent* InSession);
	UFUNCTION(BlueprintImplementableEvent, Category = "Assembly|UI")
	void OnHUDPresentationChanged();
	UFUNCTION(BlueprintImplementableEvent, Category = "Assembly|UI")
	void OnHUDMessage(const FText& Message);

	void ExecuteAction(EAssemblyHUDAction Action, int32 Argument, FGuid InstanceId);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& MouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& MouseEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& Geometry, const FPointerEvent& MouseEvent) override;
	virtual FReply NativeOnMouseWheel(const FGeometry& Geometry, const FPointerEvent& MouseEvent) override;
	virtual void NativeOnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;
	virtual bool NativeOnDragOver(const FGeometry& Geometry, const FDragDropEvent& DragDropEvent, UDragDropOperation* Operation) override;
	virtual bool NativeOnDrop(const FGeometry& Geometry, const FDragDropEvent& DragDropEvent, UDragDropOperation* Operation) override;

private:
	friend class UAssemblyHUDActionReceiver;
	UPROPERTY(Transient)
	TWeakObjectPtr<UAssemblySessionComponent> Session;
	UPROPERTY(Transient)
	TWeakObjectPtr<UAssemblySessionComponent> BoundSession;
	UPROPERTY(Transient)
	TWeakObjectPtr<UAssemblyWorkbenchComponent> BoundWorkbench;
	UPROPERTY(Transient)
	TObjectPtr<UBorder> EditPanel;
	UPROPERTY(Transient) TObjectPtr<UBorder> InspectorPanel;
	UPROPERTY(Transient) TObjectPtr<UBorder> AssemblyActionsPanel;
	UPROPERTY(Transient)
	TObjectPtr<UBorder> PlayPanel;
	UPROPERTY(Transient)
	TObjectPtr<UAssemblyPlayingToolbarWidget> PlayingToolbar;
	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> Shelf;
	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> JointList;
	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> PlayJointList;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> SelectionCaption;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> StatusCaption;
	UPROPERTY(Transient) TObjectPtr<UBorder> BindingPrompt;
	UPROPERTY(Transient) TObjectPtr<UWidget> BindingControls;
	UPROPERTY(Transient)
	TObjectPtr<USpinBox> MinAngle;
	UPROPERTY(Transient)
	TObjectPtr<USpinBox> MaxAngle;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UAssemblyShelfCardWidget>> Cards;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UButton>> KeyButtons;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UButton>> JointKindButtons;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UAssemblyHUDActionReceiver>> Actions;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UAssemblyHUDActionReceiver>> JointActions;
	FString ShelfSignature;
	FString JointSignature;
	FText DisplayedMessage;
	EAssemblyWorkbenchTool DisplayedTool = EAssemblyWorkbenchTool::None;
	FGuid DisplayedToolId;
	EAssemblyPhase DisplayedPhase = EAssemblyPhase::Assembly;
	bool bHaveDisplayedState = false;
	bool bShowingExplicitMessage = false;
	FGuid DisplayedAngleJoint;
	float DisplayedModelMinAngle = 0.0f;
	float DisplayedModelMaxAngle = 0.0f;
	bool bPlaceholderBuilt = false;
	bool bOwnsPointerCapture = false;
	TWeakObjectPtr<UAssemblyWorkbenchComponent> CapturedWorkbench;
	FGuid CapturedToolId;
	bool bRefreshing = false;
	bool OwnsCapturedTool() const;
	void EndWorldPointerCapture(bool bCancelTool);
	void BuildPlaceholder();
	void BindPresentation();
	void UnbindPresentation();
	void RefreshShelf();
	void RefreshJoints();
	void PresentReason(bool bSucceeded, const FText& Reason);
	FText GetDefaultMessage() const;
	bool IsOverPlaceholderPanel(FVector2D AbsolutePosition) const;
	FVector2D ToViewportPixels(FVector2D AbsolutePosition) const;
	UTextBlock* MakeText(const FText& Text);
	UButton* MakeButton(const FText& Text, EAssemblyHUDAction Action, int32 Argument = 0, FGuid InstanceId = FGuid(), bool bJointAction = false);
	UFUNCTION()
	void HandleChanged();
	UFUNCTION()
	void HandleMessage(FText Message);
	UFUNCTION() void HandleBindingRequired();
public:
	void CompleteLevel(int32 EarnedStars = 1, int32 BestStars = 1);
};
