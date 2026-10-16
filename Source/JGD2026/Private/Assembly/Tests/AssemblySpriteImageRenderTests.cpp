#if WITH_DEV_AUTOMATION_TESTS

#include "Assembly/AssemblySpriteImage.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Framework/Application/SlateApplication.h"
#include "ImageUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "PaperSprite.h"
#include "Slate/WidgetRenderer.h"
#include "Brushes/SlateColorBrush.h"
#include "TextureResource.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"

// Run separately with a real rendering device, not as part of the NullRHI rule suite:
// -RenderOffscreen -AssemblySpriteCapture=<absolute external PNG path>
// -ExecCmds="Automation RunTests JGD2026.Render.AssemblySpriteImage;Quit"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblySpriteImageOffscreenRenderTest,
	"JGD2026.Render.AssemblySpriteImage", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAssemblySpriteImageOffscreenRenderTest::RunTest(const FString& Parameters)
{
	FString CapturePath;
	if (!FParse::Value(FCommandLine::Get(), TEXT("AssemblySpriteCapture="), CapturePath)
		|| FPaths::IsRelative(CapturePath))
	{
		AddError(TEXT("Pass -AssemblySpriteCapture=<absolute external PNG path> to this rendering-only test."));
		return false;
	}
	if (!TestTrue(TEXT("Slate is available for an actual offscreen draw"), FSlateApplication::IsInitialized())) return false;
	const TCHAR* SpriteNames[] = { TEXT("Round"), TEXT("Bar"), TEXT("Corner"), TEXT("Tee"), TEXT("Cross"), TEXT("Fork"), TEXT("FixedRound") };
	TArray<TStrongObjectPtr<UAssemblySpriteImage>> Widgets;
	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
	for (const TCHAR* Name : SpriteNames)
	{
		const FString Path = FString::Printf(TEXT("/Game/Res/Art/Sprites/SP_Joint_%s.SP_Joint_%s"), Name, Name);
		UPaperSprite* Sprite = LoadObject<UPaperSprite>(nullptr, *Path);
		if (!TestNotNull(TEXT("Load each affected joint icon"), Sprite)) return false;
		TStrongObjectPtr<UAssemblySpriteImage>& Image = Widgets.Emplace_GetRef(NewObject<UAssemblySpriteImage>());
		Image->SetDesiredSize(FVector2D(128, 128));
		Image->SetSprite(Sprite);
		if (!TestTrue(TEXT("Each icon has usable baked triangles and a retained texture"), Image->HasRenderableSprite()))
		{
			AddError(Image->GetSpriteFailureReason());
			return false;
		}
		Row->AddSlot().AutoWidth()[SNew(SBox).WidthOverride(128).HeightOverride(128)[Image->TakeWidget()]];
	}
	const FSlateColorBrush Background(FLinearColor(0.08f, 0.08f, 0.08f, 1.0f));
	const TSharedRef<SWidget> Content = SNew(SBorder).BorderImage(&Background).Padding(0.0f)[Row];
	const FIntPoint Size(128 * static_cast<int32>(UE_ARRAY_COUNT(SpriteNames)), 128);
	FWidgetRenderer Renderer(true);
	TStrongObjectPtr<UTextureRenderTarget2D> Target(Renderer.DrawWidget(Content, FVector2D(Size.X, Size.Y)));
	if (!TestTrue(TEXT("Slate produced an offscreen render target"), Target.IsValid())) return false;
	TArray<FColor> Pixels;
	if (!TestTrue(TEXT("Read the real Slate render result"), Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels))) return false;
	if (!TestEqual(TEXT("The complete icon row was rendered"), Pixels.Num(), Size.X * Size.Y)) return false;
	const FColor EmptyColor = Pixels[0];
	int32 ArtworkPixels = 0;
	for (const FColor& Pixel : Pixels)
	{
		if (FMath::Abs(static_cast<int32>(Pixel.R) - EmptyColor.R) > 8
			|| FMath::Abs(static_cast<int32>(Pixel.G) - EmptyColor.G) > 8
			|| FMath::Abs(static_cast<int32>(Pixel.B) - EmptyColor.B) > 8) ++ArtworkPixels;
	}
	TestTrue(TEXT("Actual textured artwork is present in the render target"), ArtworkPixels > 5000);
	// These source-space corners contain the blue pin and a neighboring yellow fragment.
	// A rectangular atlas image drew both; the actual Slate triangle draw must leave the backdrop.
	const FIntPoint CutoutSamples[] = { FIntPoint(3 * 128 + 21, 56), FIntPoint(4 * 128 + 109, 13) };
	for (FIntPoint Point : CutoutSamples)
	{
		const FColor Sample = Pixels[Point.Y * Size.X + Point.X];
		TestTrue(TEXT("Excluded atlas corner remains the background in the real Slate draw"),
			FMath::Abs(static_cast<int32>(Sample.R) - EmptyColor.R) <= 2
			&& FMath::Abs(static_cast<int32>(Sample.G) - EmptyColor.G) <= 2
			&& FMath::Abs(static_cast<int32>(Sample.B) - EmptyColor.B) <= 2);
	}
	TArray64<uint8> PNG;
	FImageUtils::PNGCompressImageArray(Size.X, Size.Y, TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()), PNG);
	TestTrue(TEXT("Save the externally requested evidence image"), !PNG.IsEmpty() && FFileHelper::SaveArrayToFile(PNG, *CapturePath));
	AddInfo(FString::Printf(TEXT("Joint sprite row order: Round, Bar, Corner, Tee, Cross, Fork, FixedRound. Capture: %s"), *CapturePath));
	return true;
}

#endif
