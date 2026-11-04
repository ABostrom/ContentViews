#include "ContentViewQuery.h"
#include "ContentViewsTedsBackend.h"
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
		// For captures and quick looks: a view's source widget, the one the Content Browser hosts, in a window of its own.
		OpenCommand = MakeUnique<FAutoConsoleCommand>(TEXT("ContentViews.OpenSource"),
			TEXT("Opens a view's Content Source in its own window. ContentViews.OpenSource <view name> [width height]"),
			FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
			{
				const UContentViewDefinition* View = Args.Num() > 0 ? UE::ContentViews::FindView(Args[0]) : nullptr;
				if (!View || !UE::ContentViews::Teds::FBackend::Get().FindView(FSoftObjectPath(View)) || !FSlateApplication::IsInitialized())
				{
					return;
				}
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
						SNew(SContentViewsSource, FSoftObjectPath(View))
					]);
			}));
	}

	virtual void ShutdownModule() override
	{
		OpenCommand.Reset();
	}

private:
	TUniquePtr<FAutoConsoleCommand> OpenCommand;
};

IMPLEMENT_MODULE(FContentViewsTedsModule, ContentViewsTeds)

#undef LOCTEXT_NAMESPACE
