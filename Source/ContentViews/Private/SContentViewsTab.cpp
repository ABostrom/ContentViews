#include "SContentViewsTab.h"

#include "Algo/AllOf.h"
#include "AssetDefinitionRegistry.h"
#include "AssetRegistry/AssetRegistryHelpers.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "AssetViewSortManager.h"
#include "ContentBrowserDataMenuContexts.h"
#include "ContentBrowserDataSubsystem.h"
#include "ContentBrowserMenuContexts.h"
#include "ContentBrowserModule.h"
#include "ContentViewColumns.h"
#include "ContentViewDefinition.h"
#include "ContentViewQuery.h"
#include "Editor.h"
#include "Framework/Commands/InputBindingManager.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/MultiBox/MultiBoxExtender.h"
#include "HAL/PlatformApplicationMisc.h"
#include "IContentBrowserDataModule.h"
#include "IContentBrowserSingleton.h"
#include "SAssetView.h"
#include "Styling/AppStyle.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "ToolMenus.h"
#include "UObject/UObjectGlobals.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "ContentViews"

namespace
{
	/** The asset view inside a picker, found by type: the picker offers no other way to set its sort. */
	TSharedPtr<SAssetView> ContentViewFindAssetView(const TSharedRef<SWidget>& Widget)
	{
		if (Widget->GetType() == TEXT("SAssetView"))
		{
			return StaticCastSharedRef<SAssetView>(Widget);
		}
		FChildren* Children = Widget->GetChildren();
		for (int32 Index = 0; Children && Index < Children->Num(); ++Index)
		{
			if (TSharedPtr<SAssetView> Found = ContentViewFindAssetView(Children->GetChildAt(Index)))
			{
				return Found;
			}
		}
		return nullptr;
	}

	/** Registers the stock per-class context menus the Content Browser would, so a class's own entries appear. */
	FName ContentViewRegisterMenuHierarchy(UClass* Class)
	{
		static const FName BaseMenuName(TEXT("ContentBrowser.AssetContextMenu"));
		UToolMenus* Menus = UToolMenus::Get();
		for (UClass* Current = Class; Current; Current = Current->GetSuperClass())
		{
			const FName CurrentName = UToolMenus::JoinMenuPaths(BaseMenuName, Current->GetFName());
			if (Menus->IsMenuRegistered(CurrentName))
			{
				break;
			}
			UClass* Parent = Current->GetSuperClass();
			const bool bTop = Parent == nullptr || Parent == UObject::StaticClass();
			Menus->RegisterMenu(CurrentName, bTop ? BaseMenuName : UToolMenus::JoinMenuPaths(BaseMenuName, Parent->GetFName()));
			if (bTop)
			{
				break;
			}
		}
		return Class ? UToolMenus::JoinMenuPaths(BaseMenuName, Class->GetFName()) : BaseMenuName;
	}

	/** The selection as Content Browser items, as the asset view's own menu passes them to its extenders. */
	TArray<FContentBrowserItem> ContentViewBrowserItems(const TArray<FAssetData>& Assets)
	{
		UContentBrowserDataSubsystem* Data = IContentBrowserDataModule::Get().GetSubsystem();
		TArray<FContentBrowserItem> Items;
		for (const FAssetData& Asset : Assets)
		{
			const FName Virtual = Data->ConvertInternalPathToVirtual(FName(Asset.GetObjectPathString()));
			const FContentBrowserItem Item = Data->GetItemAtPath(Virtual, EContentBrowserItemTypeFilter::IncludeFiles);
			if (Item.IsValid())
			{
				Items.Add(Item);
			}
		}
		return Items;
	}
}

namespace UE::ContentViews
{
	TSharedPtr<SWidget> MakeAssetContextMenu(const TArray<FAssetData>& Assets)
	{
		if (Assets.IsEmpty())
		{
			return nullptr;
		}
		UClass* Common = UAssetRegistryHelpers::FindAssetNativeClass(Assets[0]);
		for (const FAssetData& Asset : Assets)
		{
			const UClass* Class = UAssetRegistryHelpers::FindAssetNativeClass(Asset);
			while (Common && Class && !Class->IsChildOf(Common))
			{
				Common = Common->GetSuperClass();
			}
		}

		UContentBrowserAssetContextMenuContext* Context = NewObject<UContentBrowserAssetContextMenuContext>();
		Context->SelectedAssets = Assets;
		Context->CommonClass = Common;
		Context->bCanBeModified = true;
		Context->bCanView = true;
		Context->bHasCookedPackages = false;
		Context->bContainsUnsupportedAssets = false;
		if (Common)
		{
			Context->CommonAssetDefinition = UAssetDefinitionRegistry::Get()->GetAssetDefinitionForClass(Common);
			PRAGMA_DISABLE_DEPRECATION_WARNINGS
			Context->CommonAssetTypeActions = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get()
				.GetAssetTypeActionsForClass(Common).Pin();
			PRAGMA_ENABLE_DEPRECATION_WARNINGS
		}

		TSharedRef<FExtender> Extender = MakeShared<FExtender>();
		Extender->AddMenuExtension("GetAssetActions", EExtensionHook::Before, nullptr,
			FMenuExtensionDelegate::CreateLambda([Assets](FMenuBuilder& Builder)
			{
				Builder.BeginSection("ContentViewsAsset", LOCTEXT("AssetSection", "Asset"));
				Builder.AddMenuEntry(LOCTEXT("Edit", "Edit..."), FText::GetEmpty(),
					FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Edit"),
					FUIAction(FExecuteAction::CreateLambda([Assets]()
					{
						TArray<FSoftObjectPath> Paths;
						for (const FAssetData& Asset : Assets)
						{
							Paths.Add(Asset.GetSoftObjectPath());
						}
						GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorsForAssets(Paths);
					})));
				Builder.AddMenuEntry(LOCTEXT("Browse", "Browse to Asset"), FText::GetEmpty(),
					FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.BrowseContent"),
					FUIAction(FExecuteAction::CreateLambda([Assets]()
					{
						FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser").Get().SyncBrowserToAssets(Assets);
					})));
				Builder.AddMenuEntry(LOCTEXT("CopyReference", "Copy Reference"), FText::GetEmpty(),
					FSlateIcon(FAppStyle::GetAppStyleSetName(), "GenericCommands.Copy"),
					FUIAction(FExecuteAction::CreateLambda([Assets]()
					{
						TArray<FString> References;
						for (const FAssetData& Asset : Assets)
						{
							References.Add(Asset.GetExportTextName());
						}
						FPlatformApplicationMisc::ClipboardCopy(*FString::Join(References, LINE_TERMINATOR));
					})));
				Builder.EndSection();
			}));

		// The Content Browser's command list: its extenders bind the commands its menu entries name (Reference Viewer, ...).
		FContentBrowserModule& Browser = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
		const TSharedRef<FUICommandList> Commands = MakeShared<FUICommandList>();
		for (FContentBrowserCommandExtender& CommandExtender : Browser.GetAllContentBrowserCommandExtenders())
		{
			CommandExtender.ExecuteIfBound(Commands, FOnContentBrowserGetSelection::CreateLambda(
				[Assets](TArray<FAssetData>& OutAssets, TArray<FString>& OutPaths) { OutAssets = Assets; }));
		}
		// Modules that keep their own list for the Content Browser append it here (Manage Tags), as SContentBrowser does.
		FInputBindingManager::Get().RegisterCommandList(TEXT("ContentBrowser"), Commands);

		// The legacy extenders the asset view's menu combines, beside the tab's own entries.
		TArray<TSharedPtr<FExtender>> Extenders{Extender};
		for (FContentBrowserMenuExtender_SelectedAssets& MenuExtender : Browser.GetAllAssetViewContextMenuExtenders())
		{
			if (MenuExtender.IsBound())
			{
				Extenders.Add(MenuExtender.Execute(Assets));
			}
		}

		FToolMenuContext MenuContext(Commands, FExtender::Combine(Extenders), Context);

		// Extenders of ContentBrowser.AssetContextMenu read this context unchecked: the asset view always adds it.
		UContentBrowserDataMenuContext_FileMenu* DataContext = NewObject<UContentBrowserDataMenuContext_FileMenu>();
		DataContext->SelectedItems = ContentViewBrowserItems(Assets);
		DataContext->bCanBeModified = Context->bCanBeModified;
		DataContext->bCanView = Context->bCanView;
		DataContext->bHasCookedPackages = Context->bHasCookedPackages;
		DataContext->bContainsUnsupportedAssets = Context->bContainsUnsupportedAssets;
		MenuContext.AddObject(DataContext);

		return UToolMenus::Get()->GenerateWidget(ContentViewRegisterMenuHierarchy(Common), MenuContext);
	}
}

namespace
{

	/** The view as the stock asset picker: its rows, its columns from the registry and providers, list or tiles. */
	TSharedRef<SWidget> ContentViewMakePicker(const UContentViewDefinition& View)
	{
		UClass* RowClass = UE::ContentViews::ResolveRowClass(View);
		if (!RowClass)
		{
			return SNew(STextBlock).Text(LOCTEXT("NoRowClass", "This view has no row class."));
		}

		FAssetPickerConfig Config;
		Config.Filter = UE::ContentViews::MakeRowFilter(*RowClass, View.bIncludeSubclasses);
		const bool bSubclasses = View.bIncludeSubclasses;
		const TArray<FContentViewFilter> Filters = View.Filters;
		Config.OnShouldFilterAsset = FOnShouldFilterAsset::CreateLambda(
			[WeakClass = TWeakObjectPtr<UClass>(RowClass), bSubclasses, Filters](const FAssetData& Asset)
			{
				const UClass* Class = WeakClass.Get();
				if (!Class || !UE::ContentViews::IsRowOf(Asset, *Class, bSubclasses))
				{
					return true;
				}
				return !Algo::AllOf(Filters, [&Asset](const FContentViewFilter& Filter)
				{
					return UE::ContentViews::PassesFilter(Asset, Filter);
				});
			});

		for (const FContentViewColumnInfo& Info : UE::ContentViews::DescribeColumns(View))
		{
			const FName Column(*Info.Name);
			if (Column == FAssetViewSortManager::NameColumnId)
			{
				continue;
			}
			const TSharedRef<const IContentViewColumn> Reader = UE::ContentViews::GetColumn(Column);
			const bool bNumber = Info.Type == EContentViewValueType::Number;
			Config.CustomColumns.Emplace(Column, FText::FromString(FName::NameToDisplayString(Info.Name, false)),
				FText::FromString(Info.Tooltip),
				bNumber ? UObject::FAssetRegistryTag::TT_Numerical : UObject::FAssetRegistryTag::TT_Alphabetical,
				FOnGetCustomAssetColumnData::CreateLambda([Reader](FAssetData& Asset, FName)
				{
					const FContentViewValue Value = Reader->GetValue(Asset);
					return Value.Type == EContentViewValueType::Number && Value.bIsSet
						? FString::SanitizeFloat(Value.Number) : Value.GetDisplay();
				}),
				FOnGetCustomAssetColumnDisplayText::CreateLambda([Reader](FAssetData& Asset, FName)
				{
					return FText::FromString(Reader->GetValue(Asset).GetDisplay());
				}));
		}

		Config.HiddenColumnNames = {
			FAssetViewSortManager::ClassColumnId.ToString(), FAssetViewSortManager::PathColumnId.ToString(),
			FAssetViewSortManager::DiskSizeColumnId.ToString(), FAssetViewSortManager::RevisionControlColumnId.ToString(),
			FAssetViewSortManager::AssetAccessSpecifierColumnId.ToString()};
		Config.OnAssetTagWantsToBeDisplayed = FOnShouldDisplayAssetTag::CreateLambda([](FName, FName) { return false; });
		Config.bShowTypeInColumnView = false;
		Config.bShowPathInColumnView = false;
		Config.bCanShowClasses = false;
		Config.bForceShowPluginContent = true;
		Config.bAddFilterUI = true;
		Config.bFocusSearchBoxWhenOpened = false;
		Config.InitialAssetViewType = View.Mode == EContentViewMode::Tiles ? EAssetViewType::Tile : EAssetViewType::Column;
		Config.ThumbnailLabel = EThumbnailLabel::AssetName;
		Config.OnAssetDoubleClicked = FOnAssetDoubleClicked::CreateLambda([](const FAssetData& Asset)
		{
			GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorsForAssets(TArray<FSoftObjectPath>{Asset.GetSoftObjectPath()});
		});
		Config.OnGetAssetContextMenu = FOnGetAssetContextMenu::CreateStatic(&UE::ContentViews::MakeAssetContextMenu);

		const TSharedRef<SWidget> Picker =
			FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser").Get().CreateAssetPicker(Config);
		if (const TSharedPtr<SAssetView> AssetView = ContentViewFindAssetView(Picker))
		{
			const FName SortColumn = View.DefaultSort.Column.IsNone() ? FAssetViewSortManager::NameColumnId : View.DefaultSort.Column;
			AssetView->SetSortParameters(EColumnSortPriority::Primary, SortColumn,
				View.DefaultSort.bDescending ? EColumnSortMode::Descending : EColumnSortMode::Ascending);
		}
		return Picker;
	}
}

void SContentViewsTab::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SSplitter)
		+ SSplitter::Slot()
		.Value(0.16f)
		[
			SAssignNew(ViewList, SListView<FViewItem>)
			.ListItemsSource(&ViewItems)
			.SelectionMode(ESelectionMode::Single)
			.OnGenerateRow(this, &SContentViewsTab::MakeViewRow)
			.OnSelectionChanged(this, &SContentViewsTab::HandleViewSelected)
		]
		+ SSplitter::Slot()
		.Value(0.84f)
		[
			SAssignNew(PickerHost, SBox)
		]
	];

	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(AssetRegistryConstants::ModuleName).Get();
	Registry.OnAssetAdded().AddSP(this, &SContentViewsTab::HandleAssetsChanged);
	Registry.OnAssetRemoved().AddSP(this, &SContentViewsTab::HandleAssetsChanged);
	Registry.OnAssetRenamed().AddSP(this, &SContentViewsTab::HandleAssetRenamed);
	FCoreUObjectDelegates::OnObjectPropertyChanged.AddSP(this, &SContentViewsTab::HandleObjectChanged);

	RefreshViewList();
	if (ViewItems.Num() > 0)
	{
		ViewList->SetSelection(ViewItems[0]);
	}
}

SContentViewsTab::~SContentViewsTab()
{
	if (FAssetRegistryModule* Module = FModuleManager::GetModulePtr<FAssetRegistryModule>(AssetRegistryConstants::ModuleName))
	{
		Module->Get().OnAssetAdded().RemoveAll(this);
		Module->Get().OnAssetRemoved().RemoveAll(this);
		Module->Get().OnAssetRenamed().RemoveAll(this);
	}
	FCoreUObjectDelegates::OnObjectPropertyChanged.RemoveAll(this);
}

void SContentViewsTab::ShowView(const UContentViewDefinition& View)
{
	const FSoftObjectPath Path(&View);
	for (const FViewItem& Item : ViewItems)
	{
		if (Item->GetSoftObjectPath() == Path)
		{
			ViewList->SetSelection(Item);
			ViewList->RequestScrollIntoView(Item);
			return;
		}
	}
}

TArray<FSoftObjectPath> SContentViewsTab::GetListedViews() const
{
	TArray<FSoftObjectPath> Paths;
	for (const FViewItem& Item : ViewItems)
	{
		Paths.Add(Item->GetSoftObjectPath());
	}
	return Paths;
}

void SContentViewsTab::RefreshViewList()
{
	const FSoftObjectPath Selected = Shown.IsValid() ? FSoftObjectPath(Shown.Get()) : FSoftObjectPath();
	ViewItems.Reset();
	for (const FAssetData& View : UE::ContentViews::FindViews())
	{
		ViewItems.Add(MakeShared<FAssetData>(View));
	}
	ViewList->RequestListRefresh();
	for (const FViewItem& Item : ViewItems)
	{
		if (Item->GetSoftObjectPath() == Selected)
		{
			ViewList->SetSelection(Item, ESelectInfo::Direct);
		}
	}
}

void SContentViewsTab::RebuildPicker()
{
	const UContentViewDefinition* View = Shown.Get();
	PickerHost->SetContent(View ? ContentViewMakePicker(*View) : SNullWidget::NullWidget);
}

TSharedRef<ITableRow> SContentViewsTab::MakeViewRow(FViewItem Item, const TSharedRef<STableViewBase>& Owner)
{
	FText Label;
	if (!Item->GetTagValue(GET_MEMBER_NAME_CHECKED(UContentViewDefinition, DisplayName), Label) || Label.IsEmpty())
	{
		Label = FText::FromName(Item->AssetName);
	}
	FName Icon;
	if (!Item->GetTagValue(GET_MEMBER_NAME_CHECKED(UContentViewDefinition, Icon), Icon) || Icon.IsNone())
	{
		Icon = TEXT("ClassIcon.DataAsset");
	}

	return SNew(STableRow<FViewItem>, Owner)
		.Padding(FMargin(6.f, 4.f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.f, 0.f, 6.f, 0.f)
			[
				SNew(SImage)
				.Image(FAppStyle::GetBrush(Icon))
				.ColorAndOpacity(FSlateColor::UseForeground())
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(Label)
			]
		];
}

void SContentViewsTab::HandleViewSelected(FViewItem Item, ESelectInfo::Type SelectInfo)
{
	UContentViewDefinition* View = Item.IsValid() ? Cast<UContentViewDefinition>(Item->GetAsset()) : nullptr;
	if (View == Shown.Get())
	{
		return;
	}
	Shown = View;
	RebuildPicker();
}

void SContentViewsTab::HandleAssetsChanged(const FAssetData& Asset)
{
	if (Asset.IsInstanceOf(UContentViewDefinition::StaticClass()))
	{
		RefreshViewList();
	}
}

void SContentViewsTab::HandleAssetRenamed(const FAssetData& Asset, const FString& OldPath)
{
	HandleAssetsChanged(Asset);
}

void SContentViewsTab::HandleObjectChanged(UObject* Object, FPropertyChangedEvent& Event)
{
	if (Object && Object == Shown.Get())
	{
		RebuildPicker();
	}
}

#undef LOCTEXT_NAMESPACE
