#if WITH_DEV_AUTOMATION_TESTS

#include "Assembly/AssemblySpriteImage.h"
#include "../AssemblySpriteGeometry.h"
#include "Misc/AutomationTest.h"
#include "UObject/GarbageCollection.h"
#include "UObject/UnrealType.h"
#include <limits>

namespace
{
	constexpr EAutomationTestFlags SpriteImageTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	TArray<FVector4> Rectangle()
	{
		return { FVector4(-64, 32, 0.2, 0.1), FVector4(64, 32, 0.6, 0.1), FVector4(64, -32, 0.6, 0.3),
			FVector4(-64, 32, 0.2, 0.1), FVector4(64, -32, 0.6, 0.3), FVector4(-64, -32, 0.2, 0.3) };
	}

	bool MeshCoversUV(const AssemblySpriteGeometry::FMesh& Mesh, FVector2D UV)
	{
		for (int32 Index = 0; Index < Mesh.Vertices.Num(); Index += 3)
		{
			const FVector2D A(Mesh.Vertices[Index].UV), B(Mesh.Vertices[Index + 1].UV), C(Mesh.Vertices[Index + 2].UV);
			const double AB = FVector2D::CrossProduct(B - A, UV - A);
			const double BC = FVector2D::CrossProduct(C - B, UV - B);
			const double CA = FVector2D::CrossProduct(A - C, UV - C);
			if ((AB >= -1e-10 && BC >= -1e-10 && CA >= -1e-10) || (AB <= 1e-10 && BC <= 1e-10 && CA <= 1e-10)) return true;
		}
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblySpriteImageAtlasCutoutsTest,
	"JGD2026.AssemblyUI.SpriteImage.AtlasCutouts", SpriteImageTestFlags)
bool FAssemblySpriteImageAtlasCutoutsTest::RunTest(const FString& Parameters)
{
	struct FCutoutCase { const TCHAR* Path; FVector2D Cutout; FVector2D Solid; };
	const FCutoutCase Cases[] = {
		{ TEXT("/Game/Res/Art/Sprites/SP_Joint_Tee.SP_Joint_Tee"), FVector2D(0.12, 0.35), FVector2D(0.5, 0.75) },
		{ TEXT("/Game/Res/Art/Sprites/SP_Joint_Cross.SP_Joint_Cross"), FVector2D(0.85, 0.10), FVector2D(0.5, 0.5) }
	};
	for (const FCutoutCase& Case : Cases)
	{
		UPaperSprite* Sprite = LoadObject<UPaperSprite>(nullptr, Case.Path);
		if (!TestNotNull(TEXT("Load the actual affected artwork"), Sprite)) return false;
		AssemblySpriteGeometry::FRenderData Data;
		if (!TestTrue(TEXT("Actual cooked sprite data is usable"), Data.SetSprite(Sprite)))
		{
			AddError(Data.LastError);
			return false;
		}
		FVector2D MaxUV(-TNumericLimits<double>::Max(), -TNumericLimits<double>::Max());
		for (const FVector4& Vertex : Sprite->BakedRenderData)
		{
			MaxUV.X = FMath::Max(MaxUV.X, Vertex.Z);
			MaxUV.Y = FMath::Max(MaxUV.Y, Vertex.W);
		}
		AddInfo(FString::Printf(TEXT("Sprite=%s, Texture=%s, Vertices=%d, original MaxUV=(%.17g,%.17g)."),
			*Sprite->GetPathName(), *GetPathNameSafe(Data.Texture.Get()), Sprite->BakedRenderData.Num(), MaxUV.X, MaxUV.Y));
		const FSlateAtlasData Atlas = Sprite->GetSlateAtlasData();
		const FVector2D CutoutUV = Atlas.StartUV + Atlas.SizeUV * Case.Cutout;
		const FVector2D SolidUV = Atlas.StartUV + Atlas.SizeUV * Case.Solid;
		TestFalse(FString::Printf(TEXT("%s excludes neighboring atlas art inside its rectangular UV region"), *Sprite->GetName()), MeshCoversUV(Data.Mesh, CutoutUV));
		TestTrue(TEXT("The joint's own central artwork remains visible"), MeshCoversUV(Data.Mesh, SolidUV));
		TestEqual(TEXT("UI preserves the sprite's exact triangle count"), Data.Mesh.Vertices.Num(), Sprite->BakedRenderData.Num());
		for (int32 Index = 0; Index < Data.Mesh.Vertices.Num(); ++Index)
		{
			TestTrue(TEXT("UI keeps the original normalized texture UVs"), Data.Mesh.Vertices[Index].UV.Equals(
				FVector2f(Sprite->BakedRenderData[Index].Z, Sprite->BakedRenderData[Index].W), 1e-7f));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblySpriteImageFitTest,
	"JGD2026.AssemblyUI.SpriteImage.UniformFit", SpriteImageTestFlags)
bool FAssemblySpriteImageFitTest::RunTest(const FString& Parameters)
{
	AssemblySpriteGeometry::FMesh Mesh;
	const TArray<FVector4> Baked = Rectangle();
	if (!TestTrue(TEXT("Read baked geometry"), AssemblySpriteGeometry::Build(Baked, Mesh))) return false;
	TestTrue(TEXT("Paper2D's upward Z axis becomes a downward UI axis"), Mesh.Vertices[0].Position.Equals(FVector2f(0, 0)));
	TestTrue(TEXT("Original shape dimensions are retained"), Mesh.Size.Equals(FVector2f(128, 64)));
	FVector2f Offset;
	float Scale;
	if (!TestTrue(TEXT("Fit inside a 64-unit shelf icon"), AssemblySpriteGeometry::Fit(Mesh, FVector2f(64, 64), Offset, Scale))) return false;
	TestEqual(TEXT("Uniform X/Y scale keeps the bone's aspect ratio"), Scale, 0.5f);
	TestTrue(TEXT("Shorter dimension is centered without stretching"), Offset.Equals(FVector2f(0, 16)));
	TestFalse(TEXT("A zero-size slot draws nothing"), AssemblySpriteGeometry::Fit(Mesh, FVector2f(0, 64), Offset, Scale));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblySpriteImageInvalidDataTest,
	"JGD2026.AssemblyUI.SpriteImage.InvalidDataAndEmptySprite", SpriteImageTestFlags)
bool FAssemblySpriteImageInvalidDataTest::RunTest(const FString& Parameters)
{
	AssemblySpriteGeometry::FMesh Mesh;
	TArray<FVector4> Baked = Rectangle();
	Baked[0].Z = 1.01;
	TestFalse(TEXT("Out-of-atlas UVs cannot sample unrelated texture regions"), AssemblySpriteGeometry::Build(Baked, Mesh));
	TestTrue(TEXT("Rejected geometry leaves no partial visible mesh"), Mesh.Vertices.IsEmpty());
	Baked = Rectangle();
	Baked[0].Z = 1.0 + 0.00000002;
	Baked[1].W = -0.00000002;
	TestTrue(TEXT("Paper2D float reciprocal rounding at a texture boundary is valid"), AssemblySpriteGeometry::Build(Baked, Mesh));
	TestTrue(TEXT("Accepted boundary roundoff clamps safely to the atlas edge"),
		Mesh.Vertices[0].UV.X == 1.0f && Mesh.Vertices[1].UV.Y == 0.0f);
	Baked = Rectangle();
	Baked[0].X = std::numeric_limits<double>::quiet_NaN();
	TestFalse(TEXT("Non-finite positions are rejected"), AssemblySpriteGeometry::Build(Baked, Mesh));
	Baked = Rectangle();
	Baked.Pop();
	TestFalse(TEXT("An incomplete triangle never reaches Slate"), AssemblySpriteGeometry::Build(Baked, Mesh));
	const TArray<FVector4> Flat { FVector4(0, 0, 0, 0), FVector4(1, 0, 1, 0), FVector4(2, 0, 1, 0) };
	TestFalse(TEXT("A line cannot become an accidental rectangular image"), AssemblySpriteGeometry::Build(Flat, Mesh));
	TStrongObjectPtr<UAssemblySpriteImage> Widget(NewObject<UAssemblySpriteImage>());
	UPaperSprite* ValidSprite = LoadObject<UPaperSprite>(nullptr, TEXT("/Game/Res/Art/Sprites/SP_Joint_Cross.SP_Joint_Cross"));
	Widget->SetSprite(ValidSprite);
	TestTrue(TEXT("Assigning a valid sprite enables its triangle view"), Widget->HasRenderableSprite());
	Widget->SetSprite(nullptr);
	TestFalse(TEXT("Clearing the sprite hides the old image rather than drawing a default brush"), Widget->HasRenderableSprite());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblySpriteImageTextureLifetimeTest,
	"JGD2026.AssemblyUI.SpriteImage.TextureLifetime", SpriteImageTestFlags)
bool FAssemblySpriteImageTextureLifetimeTest::RunTest(const FString& Parameters)
{
	FObjectPropertyBase* TextureProperty = FindFProperty<FObjectPropertyBase>(UPaperSprite::StaticClass(), TEXT("BakedSourceTexture"));
	if (!TestNotNull(TEXT("Cooked sprite texture property is available"), TextureProperty)) return false;
	UPaperSprite* TransientSprite = NewObject<UPaperSprite>();
	UTexture2D* TransientTexture = NewObject<UTexture2D>();
	TextureProperty->SetObjectPropertyValue_InContainer(TransientSprite, TransientTexture);
	TransientSprite->BakedRenderData = Rectangle();
	TWeakObjectPtr<UPaperSprite> WeakSprite(TransientSprite);
	TWeakObjectPtr<UTexture2D> WeakTexture(TransientTexture);
	AssemblySpriteGeometry::FRenderData Data;
	if (!TestTrue(TEXT("Cache the same render data used by Slate"), Data.SetSprite(TransientSprite))) return false;
	CollectGarbage(RF_NoFlags);
	TestFalse(TEXT("Rendering does not keep an unnecessary sprite object alive"), WeakSprite.IsValid());
	TestTrue(TEXT("The texture remains alive for a retained Slate mesh"), WeakTexture.IsValid());
	TestFalse(TEXT("Clearing a presentation rejects an empty sprite"), Data.SetSprite(nullptr));
	CollectGarbage(RF_NoFlags);
	TestFalse(TEXT("Clearing the last Slate presentation releases its texture"), WeakTexture.IsValid());
	return true;
}

#endif
