#include "ContentViewColumns.h"
#include "ContentViewDefinition.h"
#include "ContentViewQuery.h"
#include "ContentViewValueConverter.h"
#include "ContentViewsSettings.h"
#include "ContentViewsTools.h"
#include "Engine/AssetManager.h"
#include "Engine/Engine.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CoreDelegates.h"
#include "Modules/ModuleManager.h"
#include "SContentViewsTab.h"
#include "Styling/AppStyle.h"
#include "ToolsetRegistry/ToolsetRegistrySubsystem.h"
#include "ToolsetRegistry/UToolsetRegistry.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "ContentViews"

namespace
{
	const FName ContentViewsTabId(TEXT("ContentViews"));

	/** The open tab's widget, for opening a view in it. */
	TWeakPtr<SContentViewsTab> GContentViewsTab;

	void ContentViewsRegisterBuiltInColumns()
	{
		using namespace UE::ContentViews;
		RegisterColumn(TEXT("Name"), MakeColumn(EContentViewValueType::String, [](const FAssetData& Asset)
		{
			return FContentViewValue::OfString(Asset.AssetName.ToString());
		}, LOCTEXT("NameTooltip", "The asset's name.")));
		RegisterColumn(TEXT("Path"), MakeColumn(EContentViewValueType::String, [](const FAssetData& Asset)
		{
			return FContentViewValue::OfString(Asset.PackagePath.ToString());
		}, LOCTEXT("PathTooltip", "The folder the asset is in.")));
		RegisterColumn(TEXT("Plugin"), MakeColumn(EContentViewValueType::String, [](const FAssetData& Asset)
		{
			FString Root = Asset.PackageName.ToString();
			Root.RightChopInline(1);
			int32 Slash = INDEX_NONE;
			if (Root.FindChar(TEXT('/'), Slash))
			{
				Root.LeftInline(Slash);
			}
			return FContentViewValue::OfString(Root);
		}, LOCTEXT("PluginTooltip", "The content root the asset is in: a plugin's name, or Game.")));
		RegisterColumn(TEXT("Class"), MakeColumn(EContentViewValueType::String, [](const FAssetData& Asset)
		{
			return FContentViewValue::OfString(Asset.AssetClassPath.ToString(), Asset.AssetClassPath.GetAssetName().ToString());
		}, LOCTEXT("ClassTooltip", "The asset's class.")));
	}
}

namespace UE::ContentViews
{
	/** Opens the Content Views tab on View, sized Size when given (for captures). */
	void OpenView(const UContentViewBase* View, const FVector2D& Size = FVector2D::ZeroVector)
	{
		const TSharedPtr<SDockTab> Tab = FGlobalTabmanager::Get()->TryInvokeTab(ContentViewsTabId);
		if (const TSharedPtr<SContentViewsTab> Widget = GContentViewsTab.Pin(); Widget && View)
		{
			Widget->ShowView(*View);
		}
		if (Tab && !Size.IsZero())
		{
			if (const TSharedPtr<SWindow> Window = Tab->GetParentWindow())
			{
				Window->Resize(Size);
			}
		}
	}
}

class FContentViewsModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		ContentViewsRegisterBuiltInColumns();

		FGlobalTabmanager::Get()->RegisterNomadTabSpawner(ContentViewsTabId,
			FOnSpawnTab::CreateLambda([](const FSpawnTabArgs&)
			{
				TSharedRef<SContentViewsTab> Widget = SNew(SContentViewsTab);
				GContentViewsTab = Widget;
				return SNew(SDockTab).TabRole(ETabRole::NomadTab)[Widget];
			}))
			.SetDisplayName(LOCTEXT("TabName", "Content Views"))
			.SetTooltipText(LOCTEXT("TabTooltip", "Browse assets through view assets: rows, columns, sort and filters."))
			.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "ContentBrowser.TabIcon"))
			.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory())
			// Read every time the menu is built, so the backend setting applies at once.
			.SetMenuType(TAttribute<ETabSpawnerMenuType::Type>::CreateLambda([]()
			{
				return UContentViewsSettings::IsLegacyActive() ? ETabSpawnerMenuType::Enabled : ETabSpawnerMenuType::Hidden;
			}));

		OpenCommand = MakeUnique<FAutoConsoleCommand>(TEXT("ContentViews.Open"),
			TEXT("Opens the Content Views tab. ContentViews.Open [view name] [width height]"),
			FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
			{
				const UContentViewBase* View = Args.Num() > 0 ? UE::ContentViews::FindViewAsset(Args[0]) : nullptr;
				const FVector2D Size = Args.Num() > 2
					? FVector2D(FCString::Atof(*Args[1]), FCString::Atof(*Args[2])) : FVector2D::ZeroVector;
				UE::ContentViews::OpenView(View, Size);
			}));

		UAssetManager::CallOrRegister_OnAssetManagerCreated(FSimpleMulticastDelegate::FDelegate::CreateLambda([]()
		{
			// Editor only, so the cook never takes a view.
			UAssetManager::Get().ScanPathForPrimaryAssets(UContentViewBase::StaticAssetType(), TEXT("/Game"),
				UContentViewBase::StaticClass(), /*bHasBlueprintClasses*/ false, /*bIsEditorOnly*/ true,
				/*bForceSynchronousScan*/ false);
		}));

		// The toolset registry is an editor subsystem, so it exists only once the engine has started.
		if (GEngine && GEngine->IsInitialized())
		{
			RegisterToolset();
		}
		else
		{
			FCoreDelegates::GetOnPostEngineInit().AddRaw(this, &FContentViewsModule::RegisterToolset);
		}
	}

	virtual void ShutdownModule() override
	{
		FCoreDelegates::GetOnPostEngineInit().RemoveAll(this);
		OpenCommand.Reset();
		if (FSlateApplication::IsInitialized())
		{
			FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(ContentViewsTabId);
		}
		if (UObjectInitialized())
		{
			if (Converter)
			{
				if (TValueOrError<TObjectPtr<UToolsetRegistrySubsystem>, FString> Subsystem = UToolsetRegistrySubsystem::Get();
					Subsystem.HasValue())
				{
					Subsystem.GetValue()->ToolsetRegistry.UnregisterConverter(Converter);
				}
			}
			UToolsetRegistry::UnregisterToolsetClass(UContentViewsTools::StaticClass());
		}
		Converter.Reset();
	}

private:
	void RegisterToolset()
	{
		Converter = MakeShared<FContentViewValueConverter>();
		if (TValueOrError<TObjectPtr<UToolsetRegistrySubsystem>, FString> Subsystem = UToolsetRegistrySubsystem::Get(TEXT("ContentViews"));
			Subsystem.HasValue())
		{
			Subsystem.GetValue()->ToolsetRegistry.RegisterConverter(Converter);
		}
		UToolsetRegistry::RegisterToolsetClass(UContentViewsTools::StaticClass());
	}

	TUniquePtr<FAutoConsoleCommand> OpenCommand;
	TSharedPtr<FContentViewValueConverter> Converter;
};

IMPLEMENT_MODULE(FContentViewsModule, ContentViews)

#undef LOCTEXT_NAMESPACE
