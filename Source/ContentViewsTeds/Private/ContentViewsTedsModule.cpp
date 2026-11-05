#include "AssetRegistry/AssetRegistryModule.h"
#include "ContentViewQuery.h"
#include "ContentViewsTedsBackend.h"
#include "SceneCostView.h"
#include "SSceneCostView.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/IConsoleManager.h"
#include "Modules/ModuleManager.h"
#include "SContentViewsSource.h"
#include "Widgets/SWindow.h"

#define LOCTEXT_NAMESPACE "ContentViewsTeds"

// Everything else starts from UContentViewsTedsFactory, which TEDS finds and runs once it has started.
class FContentViewsTedsModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		DumpCommand = MakeUnique<FAutoConsoleCommand>(TEXT("ContentViews.SceneCost.Dump"),
			TEXT("Logs Scene Cost's totals and each Scene Cost view's first rows. ContentViews.SceneCost.Dump [rows]"),
			FConsoleCommandWithArgsDelegate::CreateStatic(&FContentViewsTedsModule::DumpSceneCost));
		// For captures and quick looks: a view's source widget, the one the Content Browser hosts, in a window of its own.
		OpenCommand = MakeUnique<FAutoConsoleCommand>(TEXT("ContentViews.OpenSource"),
			TEXT("Opens a view's Content Source in its own window. ContentViews.OpenSource <view name> [width height]"),
			FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
			{
				const UE::ContentViews::Teds::FBackend& Backend = UE::ContentViews::Teds::FBackend::Get();
				const UContentViewBase* View = Args.Num() > 0 ? FindSourceView(Args[0]) : nullptr;
				if (!View || !FSlateApplication::IsInitialized())
				{
					return;
				}
				const FSoftObjectPath Path(View);
				const TSharedRef<SWidget> Content = Backend.FindView(Path)
					? StaticCastSharedRef<SWidget>(SNew(SContentViewsSource, Path)) : StaticCastSharedRef<SWidget>(SNew(SSceneCostView, Path));
				const FVector2D Size = Args.Num() > 2 ? FVector2D(FCString::Atof(*Args[1]), FCString::Atof(*Args[2])) : FVector2D(1600, 900);
				// One at a time, so a capture finds the window it asked for.
				static TWeakPtr<SWindow> Open;
				if (const TSharedPtr<SWindow> Previous = Open.Pin())
				{
					Previous->RequestDestroyWindow();
				}
				Open = FSlateApplication::Get().AddWindow(SNew(SWindow)
					.Title(FText::Format(LOCTEXT("SourceWindow", "Content Source: {0}"), View->GetViewName()))
					.ClientSize(Size)
					[
						Content
					]);
			}));
	}

	/** A view with a source, of either archetype, by display name, asset name or object path. */
	static const UContentViewBase* FindSourceView(const FString& Name)
	{
		const UE::ContentViews::Teds::FBackend& Backend = UE::ContentViews::Teds::FBackend::Get();
		if (const UContentViewDefinition* View = UE::ContentViews::FindView(Name); View && Backend.FindView(FSoftObjectPath(View)))
		{
			return View;
		}
		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(AssetRegistryConstants::ModuleName).Get();
		TArray<FAssetData> Scenes;
		Registry.GetAssetsByClass(USceneCostView::StaticClass()->GetClassPathName(), Scenes, /*bSearchSubClasses*/ true);
		for (const FAssetData& Asset : Scenes)
		{
			const USceneCostView* Scene = Cast<USceneCostView>(Asset.GetAsset());
			if (Scene && Backend.FindSceneView(FSoftObjectPath(Scene)) && (Scene->GetViewName().ToString().Equals(Name, ESearchCase::IgnoreCase)
				|| Asset.AssetName.ToString().Equals(Name, ESearchCase::IgnoreCase) || Asset.GetObjectPathString().Equals(Name, ESearchCase::IgnoreCase)))
			{
				return Scene;
			}
		}
		return nullptr;
	}

	virtual void ShutdownModule() override
	{
		OpenCommand.Reset();
		DumpCommand.Reset();
	}

	/** For agents and logs: Scene Cost's totals and each Scene Cost view's first rows, most scene triangles first. */
	static void DumpSceneCost(const TArray<FString>& Args)
	{
		using namespace UE::ContentViews::Teds;
		const int32 Count = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 10;
		const FSceneTotals Totals = ReadSceneTotals();
		UE_LOG(LogTemp, Display, TEXT("SceneCost: level %s, %d loaded actors, %d meshes, %lld instances in %d components, %lld scene triangles, %lld not Nanite"),
			*Totals.Level, Totals.LoadedActors, Totals.Meshes, Totals.Instances, Totals.Components, Totals.SceneTriangles, Totals.NonNaniteTriangles);
		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(AssetRegistryConstants::ModuleName).Get();
		TArray<FAssetData> Scenes;
		Registry.GetAssetsByClass(USceneCostView::StaticClass()->GetClassPathName(), Scenes, /*bSearchSubClasses*/ true);
		for (const FAssetData& Asset : Scenes)
		{
			const USceneCostView* View = Cast<USceneCostView>(Asset.GetAsset());
			const TArray<FSceneCostRow> Rows = View ? ReadSceneRows(*View) : TArray<FSceneCostRow>();
			for (int32 Index = 0; Index < FMath::Min(Count, Rows.Num()); ++Index)
			{
				const FSceneCostRow& Row = Rows[Index];
				UE_LOG(LogTemp, Display, TEXT("SceneCost: %s #%d %s placed=%lld tris=%lld scene=%lld nanite=%d alert=%s"), *View->GetViewName().ToString(),
					Index + 1, *Row.Mesh, Row.Placed, Row.Triangles, Row.SceneTriangles, Row.bNanite ? 1 : 0, *Row.Alert);
			}
		}
	}

private:
	TUniquePtr<FAutoConsoleCommand> OpenCommand;
	TUniquePtr<FAutoConsoleCommand> DumpCommand;
};

IMPLEMENT_MODULE(FContentViewsTedsModule, ContentViewsTeds)

#undef LOCTEXT_NAMESPACE
