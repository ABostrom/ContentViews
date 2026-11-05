#include "ContentViewsTedsBackend.h"

#include "ContentViewsTeds.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "ContentBrowserModule.h"
#include "ContentViewQuery.h"
#include "ContentViewsSettings.h"
#include "ContentViewsTedsColumns.h"
#include "ContentViewsTedsFactory.h"
#include "Elements/Framework/TypedElementQueryContext.h"
#include "Elements/Interfaces/TypedElementDataStorageInterface.h"
#include "Elements/Interfaces/TypedElementDataStorageUiInterface.h"
#include "HAL/IConsoleManager.h"
#include "IContentBrowserSingleton.h"
#include "SceneCostView.h"
#include "Styling/AppStyle.h"
#include "TedsAlertColumns.h"
#include "TedsAlerts.h"
#include "TedsAssetDataColumns.h"
#include "UObject/UObjectGlobals.h"

#define LOCTEXT_NAMESPACE "ContentViewsTeds"

namespace UE::ContentViews::Teds
{
	using namespace UE::Editor::DataStorage;
	using namespace UE::Editor::DataStorage::Queries;

	namespace
	{
		const TCHAR* const CooperativeTaskName = TEXT("ContentViews.Rows");

		IUiProvider::FPurposeID ListPurpose()
		{
			return IUiProvider::FPurposeInfo("ContentBrowser", "RowLabel", NAME_None).GeneratePurposeID();
		}

		IUiProvider::FPurposeID TilePurpose()
		{
			return IUiProvider::FPurposeInfo("ContentBrowser", "TileLabel", NAME_None).GeneratePurposeID();
		}

		/** The first #RRGGBB of a colour column's value. */
		TOptional<FLinearColor> FirstColour(const FContentViewValue& Value)
		{
			const TArray<FString> Items = SplitList(Value.String);
			if (Value.bIsSet && !Items.IsEmpty() && Items[0].StartsWith(TEXT("#")))
			{
				return FLinearColor::FromSRGBColor(FColor::FromHex(Items[0]));
			}
			return {};
		}
	}

	TArray<TWeakObjectPtr<const UScriptStruct>> FViewState::ShownColumns() const
	{
		TArray<TWeakObjectPtr<const UScriptStruct>> Shown;
		for (const FColumnSlot& Slot : Columns)
		{
			Shown.Add(Slot.Type);
		}
		if (HasComputed())
		{
			Shown.Add(FTedsAlertColumn::StaticStruct());
		}
		return Shown;
	}

	FQueryDescription FViewState::MakeRowQuery() const
	{
		return Select().ReadOnly<FAssetDataColumn_Experimental, FAssetNameColumn>().Where().All(RowTag).Compile();
	}

	FName FContentViewSource::GetName()
	{
		return FName(Path.ToString());
	}

	FText FContentViewSource::GetDisplayName()
	{
		const UContentViewBase* View = FBackend::Get().FindViewAsset(Path);
		return View ? View->GetViewName() : FText::FromString(Path.GetAssetName());
	}

	FSlateIcon FContentViewSource::GetIcon()
	{
		const UContentViewBase* View = FBackend::Get().FindViewAsset(Path);
		return FSlateIcon(FAppStyle::GetAppStyleSetName(),
			View && !View->Icon.IsNone() ? View->Icon : FName(TEXT("ContentBrowser.TabIcon")));
	}

	void FContentViewSource::GetAssetViewInitParams(UE::Editor::ContentBrowser::FTableViewerInitParams& OutInitParams)
	{
		// Read by the engine's own host when this plugin's host is not the registered one: rows and columns, no filter bar.
		const FBackend& Backend = FBackend::Get();
		if (const FViewState* State = Backend.FindView(Path))
		{
			const UContentViewDefinition* View = State->View.Get();
			const bool bTiles = View && View->Mode == EContentViewMode::Tiles;
			OutInitParams.QueryDescription = State->MakeRowQuery();
			OutInitParams.Columns = State->ShownColumns();
			OutInitParams.CellWidgetPurpose = bTiles ? TilePurpose() : ListPurpose();
			OutInitParams.TableViewMode = bTiles ? ETableViewMode::Tile : ETableViewMode::List;
		}
		else if (const FSceneViewState* Scene = Backend.FindSceneView(Path))
		{
			OutInitParams.QueryDescription = Scene->MakeRowQuery();
			OutInitParams.Columns = Backend.SceneShownColumns();
			OutInitParams.CellWidgetPurpose = ListPurpose();
			OutInitParams.TableViewMode = ETableViewMode::List;
		}
	}

	FBackend& FBackend::Get()
	{
		static FBackend Backend;
		return Backend;
	}

	void FBackend::Start(ICoreProvider& InStorage, IUiProvider& InUi)
	{
		Storage = &InStorage;
		Ui = &InUi;
		AllAssetsQuery = Storage->RegisterQuery(Select().ReadOnly<FAssetDataColumn_Experimental>().Compile());
		Storage->RegisterCooperativeUpdate(CooperativeTaskName, ICoreProvider::ECooperativeTaskPriority::Medium,
			[this](FTimespan Allowance) { CooperativeUpdate(Allowance); });
		StartScene();

		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(AssetRegistryConstants::ModuleName).Get();
		Registry.OnAssetAdded().AddRaw(this, &FBackend::HandleAssetAdded);
		Registry.OnAssetRemoved().AddRaw(this, &FBackend::HandleAssetRemoved);
		Registry.OnFilesLoaded().AddRaw(this, &FBackend::ApplySetting);
		FCoreUObjectDelegates::OnObjectPropertyChanged.AddRaw(this, &FBackend::HandleObjectChanged);
		GetMutableDefault<UContentViewsSettings>()->OnSettingChanged().AddLambda([this](UObject*, FPropertyChangedEvent&)
		{
			ApplySetting();
		});
		ApplySetting();
	}

	void FBackend::Stop()
	{
		if (!Storage)
		{
			return;
		}
		if (FAssetRegistryModule* Module = FModuleManager::GetModulePtr<FAssetRegistryModule>(AssetRegistryConstants::ModuleName))
		{
			Module->Get().OnAssetAdded().RemoveAll(this);
			Module->Get().OnAssetRemoved().RemoveAll(this);
			Module->Get().OnFilesLoaded().RemoveAll(this);
		}
		FCoreUObjectDelegates::OnObjectPropertyChanged.RemoveAll(this);
		if (UObjectInitialized())
		{
			GetMutableDefault<UContentViewsSettings>()->OnSettingChanged().RemoveAll(this);
		}
		TArray<FSoftObjectPath> Paths;
		Views.GetKeys(Paths);
		for (const FSoftObjectPath& Path : Paths)
		{
			DropView(Path);
		}
		SceneViews.GetKeys(Paths);
		for (const FSoftObjectPath& Path : Paths)
		{
			DropSceneView(Path);
		}
		StopScene();
		Storage->UnregisterCooperativeUpdate(CooperativeTaskName);
		Storage->UnregisterQuery(AllAssetsQuery);
		Storage = nullptr;
		Ui = nullptr;
	}

	bool FBackend::IsActive() const
	{
		return Storage && UContentViewsSettings::IsTedsActive();
	}

	bool FBackend::IsCaughtUp() const
	{
		return Pending.IsEmpty() && PendingComputed.IsEmpty() && !bComputedDirty;
	}

	void FBackend::NoteUpdatedRows(TConstArrayView<RowHandle> Rows)
	{
		if (IsActive() && !Rows.IsEmpty())
		{
			Pending.Append(Rows);
			bComputedDirty = true;
		}
	}

	const FViewState* FBackend::FindView(const FSoftObjectPath& Path) const
	{
		return Views.Find(Path);
	}

	TOptional<FSoftObjectPath> FBackend::FindPathOfSource(const UE::Editor::ContentBrowser::IContentSource* Source) const
	{
		for (const TWeakPtr<FContentViewSource>& Weak : Sources)
		{
			if (const TSharedPtr<FContentViewSource> Pinned = Weak.Pin(); Pinned && Pinned.Get() == Source)
			{
				return Pinned->Path;
			}
		}
		return {};
	}

	const FViewState* FBackend::FindViewOfSource(const UE::Editor::ContentBrowser::IContentSource* Source) const
	{
		const TOptional<FSoftObjectPath> Path = FindPathOfSource(Source);
		return Path ? FindView(*Path) : nullptr;
	}

	const UContentViewBase* FBackend::FindViewAsset(const FSoftObjectPath& Path) const
	{
		if (const FViewState* State = FindView(Path))
		{
			return State->View.Get();
		}
		const FSceneViewState* Scene = FindSceneView(Path);
		return Scene ? Scene->View.Get() : nullptr;
	}

	TArray<RowHandle> FBackend::RowsOf(const FViewState& View) const
	{
		TArray<RowHandle> Rows;
		if (Storage && View.RowQuery != InvalidQueryHandle)
		{
			Storage->RunQuery(View.RowQuery, CreateDirectQueryCallbackBinding([&Rows](IDirectQueryContext&, RowHandle Row)
			{
				Rows.Add(Row);
			}));
		}
		return Rows;
	}

	TConstQueryFunction<bool> FBackend::MakeValueFilter(const FContentViewFilter& Filter) const
	{
		ICoreProvider* Store = Storage;
		return BuildConstQueryFunction<bool>([Store, Filter](TConstQueryContext<SingleRowInfo> Context)
		{
			const FAssetDataColumn_Experimental* Asset = Store->GetColumn<FAssetDataColumn_Experimental>(Context.GetCurrentRow());
			return Asset && PassesFilter(Asset->AssetData, Filter);
		});
	}

	FContentViewValue FBackend::ReadValue(RowHandle Row, const FColumnSlot& Slot) const
	{
		switch (Slot.Kind)
		{
		case EColumnKind::AssetName:
			if (const FAssetNameColumn* Name = Storage->GetColumn<FAssetNameColumn>(Row))
			{
				return FContentViewValue::OfString(Name->Name.ToString());
			}
			break;
		case EColumnKind::Number:
			if (const FContentViewNumberColumn* Number = Storage->GetColumn<FContentViewNumberColumn>(Row, Slot.Name))
			{
				return FContentViewValue::OfNumber(Number->Value);
			}
			break;
		case EColumnKind::Text:
			if (const FContentViewStringColumn* Text = Storage->GetColumn<FContentViewStringColumn>(Row, Slot.Name))
			{
				return FContentViewValue::OfString(Text->Value, Text->Display == Text->Value ? FString() : Text->Display);
			}
			break;
		case EColumnKind::Tag:
			if (const FItemStringAttributeColumn_Experimental* Tag = Storage->GetColumn<FItemStringAttributeColumn_Experimental>(Row, Slot.Name))
			{
				return FContentViewValue::FromTagString(Tag->Value);
			}
			break;
		case EColumnKind::Colour:
			if (const FContentViewColourColumn* Colour = Storage->GetColumn<FContentViewColourColumn>(Row, Slot.Name))
			{
				return FContentViewValue::OfString(Colour->Hex);
			}
			break;
		}
		return FContentViewValue();
	}

	void FBackend::ApplySetting()
	{
		if (!Storage)
		{
			return;
		}
		if (!IsActive())
		{
			TArray<FSoftObjectPath> Paths;
			Views.GetKeys(Paths);
			for (const FSoftObjectPath& Path : Paths)
			{
				DropView(Path);
			}
			SceneViews.GetKeys(Paths);
			for (const FSoftObjectPath& Path : Paths)
			{
				DropSceneView(Path);
			}
			Pending.Reset();
			PendingComputed.Reset();
			bComputedDirty = false;
			if (bShowedSourceBar)
			{
				if (IConsoleVariable* Bar = IConsoleManager::Get().FindConsoleVariable(TEXT("ContentBrowser.UI.ShowContentSourcesBar")))
				{
					Bar->Set(false, ECVF_SetByCode);
				}
				bShowedSourceBar = false;
			}
			return;
		}
		// The engine hides the Content Browser's source bar unless this experimental switch is on.
		if (IConsoleVariable* Bar = IConsoleManager::Get().FindConsoleVariable(TEXT("ContentBrowser.UI.ShowContentSourcesBar")))
		{
			if (!Bar->GetBool())
			{
				Bar->Set(true, ECVF_SetByCode);
				bShowedSourceBar = true;
			}
		}
		RefreshViews();
	}

	void FBackend::RefreshViews()
	{
		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(AssetRegistryConstants::ModuleName).Get();
		if (Registry.IsLoadingAssets())
		{
			return; // OnFilesLoaded calls back.
		}
		for (const FAssetData& Asset : FindViews())
		{
			if (!Views.Contains(Asset.GetSoftObjectPath()))
			{
				// View assets are small and the legacy backend loads them the same way.
				if (UContentViewDefinition* View = Cast<UContentViewDefinition>(Asset.GetAsset()))
				{
					BuildView(*View);
				}
			}
		}
		TArray<FAssetData> Scenes;
		Registry.GetAssetsByClass(USceneCostView::StaticClass()->GetClassPathName(), Scenes, /*bSearchSubClasses*/ true);
		for (const FAssetData& Asset : Scenes)
		{
			if (!SceneViews.Contains(Asset.GetSoftObjectPath()))
			{
				if (USceneCostView* View = Cast<USceneCostView>(Asset.GetAsset()))
				{
					BuildSceneView(*View);
				}
			}
		}
	}

	void FBackend::BuildView(UContentViewDefinition& View)
	{
		const FSoftObjectPath Path(&View);
		const bool bExisted = Views.Contains(Path);
		FViewState& State = Views.FindOrAdd(Path);
		if (State.RowQuery != InvalidQueryHandle)
		{
			Storage->UnregisterQuery(State.RowQuery);
		}
		State = FViewState();
		State.Path = Path;
		State.View = &View;
		State.RowClass = ResolveRowClass(View);
		State.bIncludeSubclasses = View.bIncludeSubclasses;
		State.RowTag = Storage->GenerateDynamicColumn({.TemplateType = FContentViewRowTag::StaticStruct(), .Identifier = FName(Path.ToString())});

		const TArray<FContentViewColumnInfo> Infos = DescribeColumns(View);
		for (int32 Index = 0; Index < View.Columns.Num(); ++Index)
		{
			FColumnSlot& Slot = State.Columns.AddDefaulted_GetRef();
			Slot.Name = View.Columns[Index];
			Slot.Label = View.GetColumnLabel(Slot.Name);
			Slot.Reader = GetColumn(Slot.Name);
			Slot.bComputed = IsColumnRegistered(Slot.Name);
			const EContentViewValueType Type = Infos.IsValidIndex(Index) ? Infos[Index].Type : EContentViewValueType::String;
			const UScriptStruct* Template = nullptr;
			if (Slot.Name == TEXT("Name"))
			{
				Slot.Kind = EColumnKind::AssetName;
				Slot.Type = FAssetNameColumn::StaticStruct();
			}
			else if (Slot.Reader->GetShows() == EContentViewColumnShows::Colour)
			{
				Slot.Kind = EColumnKind::Colour;
				Template = FContentViewColourColumn::StaticStruct();
			}
			else if (Type == EContentViewValueType::Number)
			{
				Slot.Kind = EColumnKind::Number;
				Template = FContentViewNumberColumn::StaticStruct();
			}
			else if (Slot.bComputed)
			{
				Slot.Kind = EColumnKind::Text;
				Template = FContentViewStringColumn::StaticStruct();
			}
			else
			{
				Slot.Kind = EColumnKind::Tag;
				Template = FItemStringAttributeColumn_Experimental::StaticStruct();
			}
			if (Template)
			{
				Slot.Type = Storage->GenerateDynamicColumn({.TemplateType = Template, .Identifier = Slot.Name});
				// The header reads the column type's display name; a generated type would show Template::Name.
				const_cast<UScriptStruct*>(Slot.Type)->SetMetaData(TEXT("DisplayName"), *Slot.Label);
				EnsureCellWidget(*Template, Slot.Name);
			}
		}
		State.RowQuery = Storage->RegisterQuery(State.MakeRowQuery());
		RegisterSource(Path);
		QueueAllAssetRows();
		if (bExisted)
		{
			ViewChanged.Broadcast(Path);
		}
	}

	void FBackend::DropView(const FSoftObjectPath& Path)
	{
		if (FViewState* State = Views.Find(Path))
		{
			for (const RowHandle Row : RowsOf(*State))
			{
				Storage->RemoveColumn(Row, State->RowTag);
			}
			Storage->UnregisterQuery(State->RowQuery);
			Views.Remove(Path);
		}
		if (Registered.Remove(Path) > 0 && FModuleManager::Get().IsModuleLoaded("ContentBrowser"))
		{
			IContentBrowserSingleton::Get().UnregisterContentSourceFactory(FName(Path.ToString()));
		}
		ViewChanged.Broadcast(Path);
	}

	void FBackend::RegisterSource(const FSoftObjectPath& Path)
	{
		Registered.Add(Path);
		// Registering again under the same name replaces the factory and tells every source bar to rebuild.
		IContentBrowserSingleton::Get().RegisterContentSourceFactory(FName(Path.ToString()),
			IContentBrowserSingleton::FContentSourceFactory::CreateLambda([this, Path]()
			{
				TSharedRef<FContentViewSource> Source = MakeShared<FContentViewSource>(Path);
				Sources.RemoveAll([](const TWeakPtr<FContentViewSource>& Weak) { return !Weak.IsValid(); });
				Sources.Add(Source);
				return TSharedRef<UE::Editor::ContentBrowser::IContentSource>(Source);
			}));
	}

	void FBackend::QueueAllAssetRows()
	{
		Storage->RunQuery(AllAssetsQuery, CreateDirectQueryCallbackBinding([this](IDirectQueryContext&, RowHandle Row)
		{
			Pending.Add(Row);
		}));
		bComputedDirty = true;
	}

	void FBackend::CooperativeUpdate(FTimespan Allowance)
	{
		if (!IsActive())
		{
			return;
		}
		const double Now = FPlatformTime::Seconds();
		const double End = Now + Allowance.GetTotalSeconds();
		// Placements first, within half the allowance: they are what a level edit changes.
		UpdateScene(Now + Allowance.GetTotalSeconds() * 0.5);
		while (!Pending.IsEmpty() && FPlatformTime::Seconds() < End)
		{
			TSet<RowHandle>::TIterator It = Pending.CreateIterator();
			const RowHandle Row = *It;
			It.RemoveCurrent();
			UpdateRow(Row, /*bComputedOnly*/ false);
		}
		if (!Pending.IsEmpty())
		{
			return;
		}
		if (bComputedDirty)
		{
			// A computed column reads other assets (the teams that list a player), so any change may move any of its values.
			bComputedDirty = false;
			for (const TPair<FSoftObjectPath, FViewState>& View : Views)
			{
				if (View.Value.HasComputed())
				{
					PendingComputed.Append(RowsOf(View.Value));
				}
			}
		}
		while (!PendingComputed.IsEmpty() && FPlatformTime::Seconds() < End)
		{
			TSet<RowHandle>::TIterator It = PendingComputed.CreateIterator();
			const RowHandle Row = *It;
			It.RemoveCurrent();
			UpdateRow(Row, /*bComputedOnly*/ true);
		}
	}

	void FBackend::UpdateRow(RowHandle Row, bool bComputedOnly)
	{
		if (!Storage->IsRowAvailable(Row))
		{
			return;
		}
		const FAssetDataColumn_Experimental* Column = Storage->GetColumn<FAssetDataColumn_Experimental>(Row);
		if (!Column)
		{
			return;
		}
		const FAssetData Asset = Column->AssetData;
		for (const TPair<FSoftObjectPath, FViewState>& Pair : Views)
		{
			const FViewState& View = Pair.Value;
			const UClass* RowClass = View.RowClass.Get();
			const bool bIsRow = RowClass && IsRowOf(Asset, *RowClass, View.bIncludeSubclasses);
			if (!bComputedOnly)
			{
				if (bIsRow)
				{
					Storage->AddColumn(Row, View.RowTag);
				}
				else if (Storage->HasColumns(Row, TConstArrayView<const UScriptStruct*>({View.RowTag})))
				{
					Storage->RemoveColumn(Row, View.RowTag);
				}
			}
			if (!bIsRow)
			{
				continue;
			}
			for (const FColumnSlot& Slot : View.Columns)
			{
				if (Slot.Kind != EColumnKind::AssetName && (!bComputedOnly || Slot.bComputed))
				{
					WriteValue(Row, Slot, Slot.Reader->GetValue(Asset));
				}
			}
		}
	}

	void FBackend::WriteValue(RowHandle Row, const FColumnSlot& Slot, const FContentViewValue& Value)
	{
		SetAlert(Row, Slot, Value.Problem);
		const bool bHas = Storage->HasColumns(Row, TConstArrayView<const UScriptStruct*>({Slot.Type}));
		const bool bWrite = Value.bIsSet && (Slot.Kind != EColumnKind::Number || Value.Type == EContentViewValueType::Number)
			&& (Slot.Kind != EColumnKind::Colour || FirstColour(Value).IsSet());
		if (!bWrite)
		{
			if (bHas)
			{
				Storage->RemoveColumn(Row, Slot.Type);
			}
			return;
		}
		switch (Slot.Kind)
		{
		case EColumnKind::Number:
			if (FContentViewNumberColumn* Number = Storage->GetColumn<FContentViewNumberColumn>(Row, Slot.Name))
			{
				Number->Value = Value.Number;
				Number->Display = Value.GetDisplay();
			}
			else
			{
				FContentViewNumberColumn New;
				New.Value = Value.Number;
				New.Display = Value.GetDisplay();
				Storage->AddColumn(Row, Slot.Name, MoveTemp(New));
			}
			break;
		case EColumnKind::Text:
			if (FContentViewStringColumn* Text = Storage->GetColumn<FContentViewStringColumn>(Row, Slot.Name))
			{
				Text->Value = Value.String;
				Text->Display = Value.GetDisplay();
			}
			else
			{
				FContentViewStringColumn New;
				New.Value = Value.String;
				New.Display = Value.GetDisplay();
				Storage->AddColumn(Row, Slot.Name, MoveTemp(New));
			}
			break;
		case EColumnKind::Tag:
		{
			// Strings keep the value (a path for a reference); numbers and bools their words, which read back the same.
			const FString Shown = Value.Type == EContentViewValueType::String ? Value.String : Value.GetDisplay();
			if (FItemStringAttributeColumn_Experimental* Tag = Storage->GetColumn<FItemStringAttributeColumn_Experimental>(Row, Slot.Name))
			{
				Tag->Value = Shown;
			}
			else
			{
				FItemStringAttributeColumn_Experimental New;
				New.Value = Shown;
				Storage->AddColumn(Row, Slot.Name, MoveTemp(New));
			}
			break;
		}
		case EColumnKind::Colour:
		{
			const FLinearColor Colour = FirstColour(Value).GetValue();
			const FString Hex = TEXT("#") + Colour.ToFColorSRGB().ToHex().Left(6);
			if (FContentViewColourColumn* Chip = Storage->GetColumn<FContentViewColourColumn>(Row, Slot.Name))
			{
				Chip->Colour = Colour;
				Chip->Hex = Hex;
			}
			else
			{
				Storage->AddColumn(Row, Slot.Name, FContentViewColourColumn{.Colour = Colour, .Hex = Hex});
			}
			break;
		}
		case EColumnKind::AssetName:
			break;
		}
	}

	void FBackend::SetAlert(RowHandle Row, const FColumnSlot& Slot, const FString& Problem)
	{
		const FName AlertName(*FString::Printf(TEXT("ContentViews.%s"), *Slot.Name.ToString()));
		const TPair<RowHandle, FName> Key(Row, AlertName);
		const bool bAlerted = Alerted.Contains(Key);
		const FText Message = FText::Format(LOCTEXT("Alert", "{0}: {1}"), FText::FromString(Slot.Label), FText::FromString(Problem));
		if (!Problem.IsEmpty() && !bAlerted)
		{
			Alerts::AddAlert(*Storage, Row, AlertName, Message, UE::Editor::DataStorage::Columns::FAlertColumnType::Warning);
			Alerted.Add(Key);
		}
		else if (!Problem.IsEmpty())
		{
			Alerts::UpdateAlertText(*Storage, Row, AlertName, Message);
		}
		else if (bAlerted)
		{
			Alerts::RemoveAlert(*Storage, Row, AlertName);
			Alerted.Remove(Key);
		}
	}

	void FBackend::EnsureCellWidget(const UScriptStruct& Template, FName Identifier)
	{
		const TPair<const UScriptStruct*, FName> Key(&Template, Identifier);
		if (!CellWidgets.Contains(Key))
		{
			CellWidgets.Add(Key);
			RegisterContentViewCell(*Ui, Template, Identifier);
		}
	}

	void FBackend::HandleAssetAdded(const FAssetData& Asset)
	{
		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(AssetRegistryConstants::ModuleName).Get();
		if (!IsActive() || Registry.IsLoadingAssets())
		{
			return;
		}
		if (Asset.IsInstanceOf(UContentViewDefinition::StaticClass()))
		{
			if (UContentViewDefinition* View = Cast<UContentViewDefinition>(Asset.GetAsset()))
			{
				BuildView(*View);
			}
		}
		else if (Asset.IsInstanceOf(USceneCostView::StaticClass()))
		{
			if (USceneCostView* Scene = Cast<USceneCostView>(Asset.GetAsset()))
			{
				BuildSceneView(*Scene);
			}
		}
	}

	void FBackend::HandleAssetRemoved(const FAssetData& Asset)
	{
		if (Views.Contains(Asset.GetSoftObjectPath()))
		{
			DropView(Asset.GetSoftObjectPath());
		}
		if (SceneViews.Contains(Asset.GetSoftObjectPath()))
		{
			DropSceneView(Asset.GetSoftObjectPath());
		}
	}

	void FBackend::HandleObjectChanged(UObject* Object, FPropertyChangedEvent& Event)
	{
		if (!IsActive())
		{
			return;
		}
		if (UContentViewDefinition* View = Cast<UContentViewDefinition>(Object); View && Views.Contains(FSoftObjectPath(View)))
		{
			BuildView(*View);
		}
		else if (USceneCostView* Scene = Cast<USceneCostView>(Object); Scene && SceneViews.Contains(FSoftObjectPath(Scene)))
		{
			BuildSceneView(*Scene);
		}
	}

	// The public face (ContentViewsTeds.h).

	bool IsActive()
	{
		return FBackend::Get().IsActive();
	}

	bool IsCaughtUp()
	{
		return FBackend::Get().IsCaughtUp();
	}

	bool HasContentSource(const UContentViewDefinition& View)
	{
		return FBackend::Get().HasSource(FSoftObjectPath(&View));
	}

	TArray<FContentViewRow> ReadRows(const UContentViewDefinition& View)
	{
		TArray<FContentViewRow> Rows;
		FBackend& Backend = FBackend::Get();
		const FViewState* State = Backend.FindView(FSoftObjectPath(&View));
		if (!State)
		{
			return Rows;
		}
		for (const RowHandle Row : Backend.RowsOf(*State))
		{
			const FAssetDataColumn_Experimental* Asset = Backend.GetStorage()->GetColumn<FAssetDataColumn_Experimental>(Row);
			if (!Asset)
			{
				continue;
			}
			FContentViewRow& Out = Rows.AddDefaulted_GetRef();
			Out.AssetPath = Asset->AssetData.GetSoftObjectPath().ToString();
			for (const FColumnSlot& Slot : State->Columns)
			{
				Out.Values.Add(Slot.Name.ToString(), Backend.ReadValue(Row, Slot));
			}
		}
		return Rows;
	}

	TMap<FString, FString> ReadAlerts(const UContentViewDefinition& View)
	{
		TMap<FString, FString> Found;
		FBackend& Backend = FBackend::Get();
		if (const FViewState* State = Backend.FindView(FSoftObjectPath(&View)))
		{
			for (const RowHandle Row : Backend.RowsOf(*State))
			{
				const FTedsAlertColumn* Alert = Backend.GetStorage()->GetColumn<FTedsAlertColumn>(Row);
				const FAssetDataColumn_Experimental* Asset = Backend.GetStorage()->GetColumn<FAssetDataColumn_Experimental>(Row);
				if (Alert && Asset)
				{
					Found.Add(Asset->AssetData.GetSoftObjectPath().ToString(), Alert->Message.ToString());
				}
			}
		}
		return Found;
	}
}

#undef LOCTEXT_NAMESPACE
