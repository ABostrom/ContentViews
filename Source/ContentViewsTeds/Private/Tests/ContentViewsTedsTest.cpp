#include "ContentViewsTestRow.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AssetRegistry/AssetRegistryModule.h"
#include "ContentViewColumns.h"
#include "ContentViewDefinition.h"
#include "ContentViewQuery.h"
#include "ContentViewsSettings.h"
#include "ContentViewsTeds.h"
#include "ContentViewsTedsBackend.h"
#include "Columns/SlateDelegateColumns.h"
#include "DataStorage/Features.h"
#include "Editor.h"
#include "Elements/Interfaces/TypedElementDataStorageInterface.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "SContentViewsSource.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "SceneCostView.h"
#include "TedsAssetDataColumns.h"
#include "TedsQueryStackExecutor.h"
#include "UObject/Package.h"
#include "ITedsTableViewer.h"

namespace
{
	const FName TedsTestCrewCheck(TEXT("CrewCheck"));
	const FName TedsTestSpeedTwice(TEXT("SpeedTwice"));
	const FName TedsTestCrewColour(TEXT("CrewColour"));

	/**
	 * Four rows of the test class in a fresh /Temp folder (three assets, one a Blueprint child), and a view over them with
	 * a registry tag number, a registry tag string, a bool and two computed columns, with the TEDS backend on.
	 */
	struct FTedsTestRows
	{
		FString Folder;
		TArray<UObject*> Made;
		UContentViewDefinition* View = nullptr;
		EContentViewsBackend WasBackend;

		FTedsTestRows()
		{
			UContentViewsSettings* Settings = GetMutableDefault<UContentViewsSettings>();
			WasBackend = Settings->Backend;
			SetBackend(EContentViewsBackend::Both);

			using namespace UE::ContentViews;
			RegisterColumn(TedsTestSpeedTwice, MakeColumn(EContentViewValueType::Number, [](const FAssetData& Asset)
			{
				const FContentViewValue Speed = ReadTag(Asset, TEXT("Speed"));
				return Speed.bIsSet ? FContentViewValue::OfNumber(Speed.Number * 2.0) : FContentViewValue();
			}));
			RegisterColumn(TedsTestCrewCheck, MakeColumn(EContentViewValueType::String, [](const FAssetData& Asset)
			{
				const FContentViewValue Crew = ReadTag(Asset, TEXT("Crew"));
				return Crew.bIsSet ? Crew : FContentViewValue::OfProblem(TEXT("no crew"));
			}));
			RegisterColumn(TedsTestCrewColour, MakeColumn(EContentViewValueType::String, [](const FAssetData& Asset)
			{
				const FContentViewValue Crew = ReadTag(Asset, TEXT("Crew"));
				return Crew.bIsSet ? FContentViewValue::OfString(Crew.String == TEXT("Red") ? TEXT("#FF0000") : TEXT("#0000FF")) : FContentViewValue();
			}, FText::GetEmpty(), EContentViewColumnShows::Colour));

			Folder = FString::Printf(TEXT("/Temp/ContentViewsTedsTest_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
			MakeRow(TEXT("Row_Fast"), 90.f, TEXT("Red"));
			MakeRow(TEXT("Row_Slow"), 40.f, TEXT("Blue"));
			MakeRow(TEXT("Row_Hundred"), 100.f, TEXT("Green"));

			UPackage* BlueprintPackage = CreatePackage(*(Folder / TEXT("BP_Row_Child")));
			UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(UContentViewsTestRow::StaticClass(),
				BlueprintPackage, TEXT("BP_Row_Child"), BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
			CastChecked<UContentViewsTestRow>(Blueprint->GeneratedClass->GetDefaultObject())->Speed = 65.f;
			Register(Blueprint);

			View = MakeView(TEXT("CV_ContentViewsTedsTest"));
		}

		~FTedsTestRows()
		{
			for (UObject* Object : Made)
			{
				FAssetRegistryModule::AssetDeleted(Object);
				Object->ClearFlags(RF_Public | RF_Standalone);
				Object->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
				Object->MarkAsGarbage();
			}
			UE::ContentViews::UnregisterColumn(TedsTestSpeedTwice);
			UE::ContentViews::UnregisterColumn(TedsTestCrewCheck);
			UE::ContentViews::UnregisterColumn(TedsTestCrewColour);
			SetBackend(WasBackend);
		}

		static void SetBackend(EContentViewsBackend Backend)
		{
			UContentViewsSettings* Settings = GetMutableDefault<UContentViewsSettings>();
			Settings->Backend = Backend;
			FPropertyChangedEvent Event(nullptr);
			Settings->OnSettingChanged().Broadcast(Settings, Event);
		}

		UContentViewDefinition* MakeView(const TCHAR* Name)
		{
			UPackage* Package = CreatePackage(*(Folder / Name));
			UContentViewDefinition* NewView = NewObject<UContentViewDefinition>(Package, Name, RF_Public | RF_Standalone);
			NewView->DisplayName = FText::FromString(Name);
			NewView->RowClass = UContentViewsTestRow::StaticClass();
			NewView->Columns = {TEXT("Name"), TEXT("Speed"), TEXT("Crew"), TEXT("bKeeper"), TedsTestSpeedTwice, TedsTestCrewCheck, TedsTestCrewColour};
			Register(NewView);
			return NewView;
		}

		void MakeRow(const TCHAR* Name, float Speed, const TCHAR* Crew)
		{
			UPackage* Package = CreatePackage(*(Folder / Name));
			UContentViewsTestRow* Row = NewObject<UContentViewsTestRow>(Package, Name, RF_Public | RF_Standalone);
			Row->Speed = Speed;
			Row->Crew = Crew;
			Register(Row);
		}

		void Register(UObject* Object)
		{
			FAssetRegistryModule::AssetCreated(Object);
			Made.Add(Object);
		}

		/** The fixture's own entries of Paths, by asset name. */
		TArray<FString> Mine(const TArray<FString>& Paths) const
		{
			TArray<FString> Names;
			for (const FString& Path : Paths)
			{
				if (Path.StartsWith(Folder))
				{
					Names.Add(FSoftObjectPath(Path).GetAssetName());
				}
			}
			return Names;
		}

		int32 TedsRowCount(const UContentViewDefinition& Of) const
		{
			int32 Count = 0;
			for (const FContentViewRow& Row : UE::ContentViews::Teds::ReadRows(Of))
			{
				Count += Row.AssetPath.StartsWith(Folder) ? 1 : 0;
			}
			return Count;
		}
	};

	/** Waits until TEDS has caught up and holds Count of the fixture's rows in View (or gives up after 60 s), then runs Then. */
	void TedsTestWhenRows(FAutomationTestBase& Test, TSharedRef<FTedsTestRows> Rows, TFunction<UContentViewDefinition*()> View,
		int32 Count, TFunction<void()> Then)
	{
		const double Start = FPlatformTime::Seconds();
		Test.AddCommand(new FFunctionLatentCommand([&Test, Rows, View, Count, Then, Start]()
		{
			const bool bReady = UE::ContentViews::Teds::IsCaughtUp() && Rows->TedsRowCount(*View()) == Count;
			if (!bReady && FPlatformTime::Seconds() - Start < 60.0)
			{
				return false;
			}
			Test.TestTrue(TEXT("TEDS caught up with the fixture's rows"), bReady);
			if (bReady)
			{
				Then();
			}
			return true;
		}));
	}
}

/**
 * Situation: the fixture's view with the TEDS backend on, beside the legacy backend's RunView over the same view.
 * Why it matters: both backends read one view asset; a row or a value that differs between them is a lie in one.
 * Pass: the same four rows, and every column's value the same type and value in both.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FContentViewsTedsSameRowsTest,
	"ContentViews.Teds.TheSourceHoldsTheLegacyBackendsRowsAndValues",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FContentViewsTedsSameRowsTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("TEDS runs in this editor"), UE::Editor::DataStorage::AreEditorDataStorageFeaturesEnabled()))
	{
		return false;
	}
	TSharedRef<FTedsTestRows> Rows = MakeShared<FTedsTestRows>();
	TedsTestWhenRows(*this, Rows, [Rows]() { return Rows->View; }, 4, [this, Rows]()
	{
		TMap<FString, FContentViewRow> Teds;
		for (FContentViewRow& Row : UE::ContentViews::Teds::ReadRows(*Rows->View))
		{
			Teds.Add(Row.AssetPath, MoveTemp(Row));
		}
		int32 Compared = 0;
		for (const FContentViewRow& Legacy : UE::ContentViews::RunView(*Rows->View, {}, FContentViewSort(), 0))
		{
			if (!Legacy.AssetPath.StartsWith(Rows->Folder))
			{
				continue;
			}
			const FContentViewRow* Row = Teds.Find(Legacy.AssetPath);
			if (!TestNotNull(*FString::Printf(TEXT("%s has a TEDS row"), *Legacy.AssetPath), Row))
			{
				continue;
			}
			++Compared;
			for (const TPair<FString, FContentViewValue>& Cell : Legacy.Values)
			{
				const FContentViewValue& Want = Cell.Value;
				const FContentViewValue* Got = Row->Values.Find(Cell.Key);
				const FString What = FString::Printf(TEXT("%s %s"), *FSoftObjectPath(Legacy.AssetPath).GetAssetName(), *Cell.Key);
				if (!TestNotNull(*What, Got))
				{
					continue;
				}
				TestEqual(*(What + TEXT(" is set")), Got->bIsSet, Want.bIsSet);
				if (Want.bIsSet && Got->bIsSet)
				{
					TestEqual(*(What + TEXT(" type")), Got->Type, Want.Type);
					TestEqual(*(What + TEXT(" string")), Got->String, Want.String);
					TestEqual(*(What + TEXT(" number")), Got->Number, Want.Number, 0.0001);
					TestEqual(*(What + TEXT(" bool")), Got->bBool, Want.bBool);
				}
			}
		}
		TestEqual(TEXT("four rows compared"), Compared, 4);
		const TMap<FString, FString> Alerts = UE::ContentViews::Teds::ReadAlerts(*Rows->View);
		const FString* ChildAlert = Alerts.Find(Rows->Folder / TEXT("BP_Row_Child.BP_Row_Child"));
		TestTrue(TEXT("the child, which has no crew, carries the provider's alert"), ChildAlert && ChildAlert->Contains(TEXT("no crew")));
		TestFalse(TEXT("a row with a crew carries none"), Alerts.Contains(Rows->Folder / TEXT("Row_Fast.Row_Fast")));
	});
	return true;
}

/**
 * Situation: the view's Speed column sorted by TEDS's own sorter, with speeds 100, 90, 65 and 40.
 * Why it matters: a registry tag is text, and text sorts 100 before 40; the TEDS column must be a number.
 * Pass: 100, 90, 65, 40 highest first.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FContentViewsTedsSortTest,
	"ContentViews.Teds.ANumberColumnSortsAsANumber",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FContentViewsTedsSortTest::RunTest(const FString& Parameters)
{
	TSharedRef<FTedsTestRows> Rows = MakeShared<FTedsTestRows>();
	TedsTestWhenRows(*this, Rows, [Rows]() { return Rows->View; }, 4, [this, Rows]()
	{
		TestEqual(TEXT("by speed, highest first"),
			FString::Join(Rows->Mine(UE::ContentViews::Teds::SortRows(*Rows->View, TEXT("Speed"), true)), TEXT(",")),
			FString(TEXT("Row_Hundred,Row_Fast,BP_Row_Child,Row_Slow")));
	});
	return true;
}

/**
 * Situation: the filter Speed >= 60 run as the source's filter bar runs it.
 * Why it matters: a value filter is how a view says "Pace >= 80"; the filter bar must keep what the legacy backend keeps.
 * Pass: the three rows at 60 or more, as the legacy backend gives them.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FContentViewsTedsFilterTest,
	"ContentViews.Teds.AValueFilterKeepsTheRowsTheLegacyBackendKeeps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FContentViewsTedsFilterTest::RunTest(const FString& Parameters)
{
	TSharedRef<FTedsTestRows> Rows = MakeShared<FTedsTestRows>();
	TedsTestWhenRows(*this, Rows, [Rows]() { return Rows->View; }, 4, [this, Rows]()
	{
		FContentViewFilter AtLeast;
		AtLeast.Column = TEXT("Speed");
		AtLeast.Operator = EContentViewFilterOp::AtLeast;
		AtLeast.Value = TEXT("60");
		TArray<FString> Teds = Rows->Mine(UE::ContentViews::Teds::FilterRows(*Rows->View, AtLeast));
		Teds.Sort();
		TArray<FString> Legacy;
		for (const FContentViewRow& Row : UE::ContentViews::RunView(*Rows->View, {AtLeast}, FContentViewSort(), 0))
		{
			Legacy.Add(Row.AssetPath);
		}
		Legacy = Rows->Mine(Legacy);
		Legacy.Sort();
		TestEqual(TEXT("TEDS keeps the three fast rows"), FString::Join(Teds, TEXT(",")), FString(TEXT("BP_Row_Child,Row_Fast,Row_Hundred")));
		TestEqual(TEXT("as the legacy backend does"), FString::Join(Teds, TEXT(",")), FString::Join(Legacy, TEXT(",")));
	});
	return true;
}

/**
 * Situation: the backend setting moved from Both to Legacy and back.
 * Why it matters: the setting is how a project turns the experimental backend off; a source left behind is a dead button.
 * Pass: the view has a source under Both, none under Legacy, and one again under TEDS.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FContentViewsTedsSwitchTest,
	"ContentViews.Teds.TheBackendSettingAddsAndRemovesTheSources",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FContentViewsTedsSwitchTest::RunTest(const FString& Parameters)
{
	FTedsTestRows Rows;
	TestTrue(TEXT("a source under Both"), UE::ContentViews::Teds::HasContentSource(*Rows.View));
	FTedsTestRows::SetBackend(EContentViewsBackend::Legacy);
	TestFalse(TEXT("TEDS is off under Legacy"), UE::ContentViews::Teds::IsActive());
	TestFalse(TEXT("no source under Legacy"), UE::ContentViews::Teds::HasContentSource(*Rows.View));
	FTedsTestRows::SetBackend(EContentViewsBackend::Teds);
	TestTrue(TEXT("a source again under TEDS"), UE::ContentViews::Teds::HasContentSource(*Rows.View));
	return true;
}

/**
 * Situation: in a running editor, a new view asset is made, then a new row asset (in memory, never saved).
 * Why it matters: a view is a new asset and no code, and a designer making an asset expects to see it in the view at once.
 * Pass: the new view has a source at once; the new row reaches it with its tag column filled and its computed column
 * recomputed, and the computed column of the other rows is still right.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FContentViewsTedsLiveTest,
	"ContentViews.Teds.ANewViewAndANewRowAppearWhileTheEditorRuns",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FContentViewsTedsLiveTest::RunTest(const FString& Parameters)
{
	TSharedRef<FTedsTestRows> Rows = MakeShared<FTedsTestRows>();
	UContentViewDefinition* Second = Rows->MakeView(TEXT("CV_ContentViewsTedsTestSecond"));
	TestTrue(TEXT("the new view has a source"), UE::ContentViews::Teds::HasContentSource(*Second));
	TedsTestWhenRows(*this, Rows, [Rows]() { return Rows->View; }, 4, [Rows]()
	{
		Rows->MakeRow(TEXT("Row_New"), 77.f, TEXT("Gold"));
	});
	TedsTestWhenRows(*this, Rows, [Second]() { return Second; }, 5, [this, Rows, Second]()
	{
		for (const FContentViewRow& Row : UE::ContentViews::Teds::ReadRows(*Second))
		{
			if (Row.AssetPath.EndsWith(TEXT("Row_New.Row_New")))
			{
				TestEqual(TEXT("its Speed tag"), Row.Values.FindChecked(TEXT("Speed")).Number, 77.0);
				TestEqual(TEXT("its computed SpeedTwice"), Row.Values.FindChecked(TedsTestSpeedTwice.ToString()).Number, 154.0);
				TestEqual(TEXT("its Crew"), Row.Values.FindChecked(TEXT("Crew")).String, FString(TEXT("Gold")));
				return;
			}
		}
		AddError(TEXT("the new row is not in the view"));
	});
	return true;
}

namespace
{
	/** The fixture's rows in the order a source's stack shows them, once it has run (by asset name). */
	TArray<FString> TedsTestShownOrder(const FTedsTestRows& Rows, const SContentViewsSource& Source)
	{
		using namespace UE::Editor::DataStorage;
		const TSharedPtr<QueryStack::IRowNode> Shown = Source.GetShownRows();
		QueryStack::FExplicitUpdateExecutor Executor(TEXT("ContentViewsTest"), Shown);
		TArray<FString> Paths;
		for (int32 Pass = 0; Pass < 200; ++Pass)
		{
			Executor.Update();
		}
		ICoreProvider* Storage = GetMutableDataStorageFeature<ICoreProvider>(StorageFeatureName);
		for (const RowHandle Row : Shown->GetRows())
		{
			if (const FAssetDataColumn_Experimental* Asset = Storage->GetColumn<FAssetDataColumn_Experimental>(Row))
			{
				Paths.Add(Asset->AssetData.GetSoftObjectPath().ToString());
			}
		}
		return Rows.Mine(Paths);
	}

	/** The row of Path in the backend's view of View. */
	UE::Editor::DataStorage::RowHandle TedsTestRowOf(const UContentViewDefinition& View, const FString& Path)
	{
		using namespace UE::Editor::DataStorage;
		ICoreProvider* Storage = GetMutableDataStorageFeature<ICoreProvider>(StorageFeatureName);
		const UE::ContentViews::Teds::FViewState* State = UE::ContentViews::Teds::FBackend::Get().FindView(FSoftObjectPath(&View));
		for (const RowHandle Row : State ? UE::ContentViews::Teds::FBackend::Get().RowsOf(*State) : TArray<RowHandle>())
		{
			const FAssetDataColumn_Experimental* Asset = Storage->GetColumn<FAssetDataColumn_Experimental>(Row);
			if (Asset && Asset->AssetData.GetSoftObjectPath().ToString() == Path)
			{
				return Row;
			}
		}
		return InvalidRowHandle;
	}
}

/**
 * Situation: a row of a view's Content Source double-clicked, then right-clicked, through the columns the TEDS table binds
 * its list's double-click and context menu to.
 * Why it matters: a table you cannot open an asset from, or act on with the Content Browser's menu, is a dead end; the
 * legacy tab's right-click once crashed for a missing menu context, so the TEDS one builds the same menu.
 * Pass: the double-click opens the row's asset editor; the right-click builds a menu for the selected row with no error.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FContentViewsTedsRowActionsTest,
	"ContentViews.Teds.ARowOpensOnDoubleClickAndGivesTheAssetMenuOnRightClick",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FContentViewsTedsRowActionsTest::RunTest(const FString& Parameters)
{
	// An opened editor asks revision control about its package, and a git provider cannot find an unsaved /Temp one.
	AddExpectedErrorPlain(TEXT("Invalid path '/Temp'"), EAutomationExpectedErrorFlags::Contains, -1);
	TSharedRef<FTedsTestRows> Rows = MakeShared<FTedsTestRows>();
	TedsTestWhenRows(*this, Rows, [Rows]() { return Rows->View; }, 4, [this, Rows]()
	{
		using namespace UE::Editor::DataStorage;
		ICoreProvider* Storage = GetMutableDataStorageFeature<ICoreProvider>(StorageFeatureName);
		const TSharedRef<SContentViewsSource> Source = SNew(SContentViewsSource, FSoftObjectPath(Rows->View));
		const FString FastPath = Rows->Folder / TEXT("Row_Fast.Row_Fast");
		const RowHandle Fast = TedsTestRowOf(*Rows->View, FastPath);
		if (!TestTrue(TEXT("the fast row is in the view"), Fast != InvalidRowHandle) || !TestTrue(TEXT("a viewer"), Source->GetViewer().IsValid()))
		{
			return;
		}
		const RowHandle WidgetRow = Source->GetViewer()->GetWidgetRowHandle();

		UObject* Asset = FSoftObjectPath(FastPath).ResolveObject();
		UAssetEditorSubsystem* Editors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
		const FWidgetDoubleClickedColumn* DoubleClick = Storage->GetColumn<FWidgetDoubleClickedColumn>(WidgetRow);
		if (TestTrue(TEXT("the table's double-click is bound"), DoubleClick && DoubleClick->OnMouseButtonDoubleClick && DoubleClick->OnMouseButtonDoubleClick->IsBound()))
		{
			DoubleClick->OnMouseButtonDoubleClick->Execute(FTedsRowHandle{.RowHandle = Fast});
			TestNotNull(TEXT("the double-click opened the row's asset"), Editors->FindEditorForAsset(Asset, /*bFocusIfOpen*/ false));
			Editors->CloseAllEditorsForAsset(Asset);
		}

		Source->GetViewer()->SetSelection(Fast, true, ESelectInfo::Direct);
		TestEqual(TEXT("the selection is the fast row's asset"), Source->GetSelectedAssets().Num(), 1);
		const FWidgetContextMenuColumn* Menu = Storage->GetColumn<FWidgetContextMenuColumn>(WidgetRow);
		if (TestTrue(TEXT("the table's context menu is bound"), Menu && Menu->OnContextMenuOpening && Menu->OnContextMenuOpening->IsBound()))
		{
			TestTrue(TEXT("the right-click builds the asset menu"), Menu->OnContextMenuOpening->Execute().IsValid());
		}
	});
	// The revision control answer arrives a few frames later; it belongs to this test.
	AddCommand(new FWaitLatentCommand(1.f));
	return true;
}

/**
 * Situation: the fixture's view in Tiles mode, sorted by Speed, highest first.
 * Why it matters: a tile view has no header to click, so the view's own sort is the only order its tiles can have.
 * Pass: the tiles run 100, 90, 65, 40.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FContentViewsTedsTileSortTest,
	"ContentViews.Teds.TilesOpenInTheViewsDefaultSort",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FContentViewsTedsTileSortTest::RunTest(const FString& Parameters)
{
	TSharedRef<FTedsTestRows> Rows = MakeShared<FTedsTestRows>();
	UContentViewDefinition* Tiles = Rows->MakeView(TEXT("CV_ContentViewsTedsTestTiles"));
	Tiles->Mode = EContentViewMode::Tiles;
	Tiles->DefaultSort.Column = TEXT("Speed");
	Tiles->DefaultSort.bDescending = true;
	FPropertyChangedEvent Changed(nullptr);
	FCoreUObjectDelegates::OnObjectPropertyChanged.Broadcast(Tiles, Changed);
	TedsTestWhenRows(*this, Rows, [Tiles]() { return Tiles; }, 4, [this, Rows, Tiles]()
	{
		const TSharedRef<SContentViewsSource> Source = SNew(SContentViewsSource, FSoftObjectPath(Tiles));
		TestEqual(TEXT("tiles by speed, highest first"), FString::Join(TedsTestShownOrder(*Rows, *Source), TEXT(",")),
			FString(TEXT("Row_Hundred,Row_Fast,BP_Row_Child,Row_Slow")));
	});
	return true;
}

/**
 * Situation: the fixture's colour column, CrewColour, in the TEDS backend.
 * Why it matters: the stock colour column's header reads "Color" in every view and a row holds one; a view's column is
 * named for what it shows.
 * Pass: the column's type is named Crew Colour, and its values are the legacy backend's (red for the red crew).
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FContentViewsTedsColourTest,
	"ContentViews.Teds.AColourColumnIsNamedForItself",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FContentViewsTedsColourTest::RunTest(const FString& Parameters)
{
	TSharedRef<FTedsTestRows> Rows = MakeShared<FTedsTestRows>();
	TedsTestWhenRows(*this, Rows, [Rows]() { return Rows->View; }, 4, [this, Rows]()
	{
		const UE::ContentViews::Teds::FViewState* State = UE::ContentViews::Teds::FBackend::Get().FindView(FSoftObjectPath(Rows->View));
		const UE::ContentViews::Teds::FColumnSlot* Slot = State ? State->FindColumn(TedsTestCrewColour) : nullptr;
		if (!TestNotNull(TEXT("the colour column"), Slot))
		{
			return;
		}
		TestEqual(TEXT("its header"), Slot->Type->GetMetaData(TEXT("DisplayName")), FString(TEXT("Crew Colour")));
		TestEqual(TEXT("its kind"), Slot->Kind, UE::ContentViews::Teds::EColumnKind::Colour);
		for (const FContentViewRow& Row : UE::ContentViews::Teds::ReadRows(*Rows->View))
		{
			if (Row.AssetPath.EndsWith(TEXT("Row_Fast.Row_Fast")))
			{
				TestEqual(TEXT("the red crew's chip"), Row.Values.FindChecked(TedsTestCrewColour.ToString()).String, FString(TEXT("#FF0000")));
			}
		}
	});
	return true;
}

namespace
{
	double TedsTestTriangles(const FAssetData& Mesh)
	{
		return UE::ContentViews::ReadTag(Mesh, TEXT("Triangles")).Number;
	}

	/** Every row of View as the legacy backend gives it is at or above Threshold, not Nanite, and in descending order. */
	void TedsTestCheckHeavy(FAutomationTestBase& Test, const UContentViewDefinition& View, double Threshold, const TCHAR* When)
	{
		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(AssetRegistryConstants::ModuleName).Get();
		const TArray<FContentViewRow> Rows = UE::ContentViews::RunView(View, {}, FContentViewSort(), 0);
		Test.TestTrue(*FString::Printf(TEXT("%s: some mesh is that heavy"), When), Rows.Num() > 0);
		double Last = TNumericLimits<double>::Max();
		for (const FContentViewRow& Row : Rows)
		{
			const double Triangles = Row.Values.FindChecked(TEXT("Triangles")).Number;
			Test.TestTrue(*FString::Printf(TEXT("%s: %s has %.0f triangles, at least %.0f"), When, *Row.AssetPath, Triangles, Threshold), Triangles >= Threshold);
			Test.TestTrue(*FString::Printf(TEXT("%s: %s in descending order"), When, *Row.AssetPath), Triangles <= Last);
			Test.TestFalse(*FString::Printf(TEXT("%s: %s is not Nanite"), When, *Row.AssetPath), Row.Values.FindChecked(TEXT("NaniteEnabled")).bBool);
			Last = Triangles;
		}
		const TArray<FString> Teds = UE::ContentViews::Teds::SortRows(View, TEXT("Triangles"), /*bDescending*/ true);
		Last = TNumericLimits<double>::Max();
		bool bOrdered = true;
		for (const FString& Path : Teds)
		{
			const double Triangles = TedsTestTriangles(Registry.GetAssetByObjectPath(FSoftObjectPath(Path)));
			bOrdered &= Triangles <= Last;
			Last = Triangles;
		}
		Test.TestTrue(*FString::Printf(TEXT("%s: TEDS sorts the meshes by triangles as numbers"), When), bOrdered && Teds.Num() >= Rows.Num());
	}
}

/**
 * Situation: the plugin's Heavy Meshes example (non-Nanite static meshes at or above a triangle count, heaviest first),
 * copied with a threshold of 1000, then edited to 5000 while its Content Source is open.
 * Why it matters: the threshold is a field on the view asset, so a designer tunes the view by editing it; an open view
 * that keeps the old rows tells him the wrong thing.
 * Pass: every row is at or above the threshold, not Nanite and in descending order in both backends; after the edit the
 * open source rebuilds, and its filter bar and the legacy backend keep only meshes at or above 5000.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FContentViewsTedsHeavyMeshesTest,
	"ContentViews.Teds.HeavyMeshesKeepsMeshesAtItsThresholdAndFollowsAnEdit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FContentViewsTedsHeavyMeshesTest::RunTest(const FString& Parameters)
{
	const UContentViewDefinition* Shipped = LoadObject<UContentViewDefinition>(nullptr, TEXT("/ContentViews/Examples/CV_HeavyMeshes.CV_HeavyMeshes"));
	if (!TestNotNull(TEXT("the shipped Heavy Meshes view"), Shipped) || !TestEqual(TEXT("its filters"), Shipped->Filters.Num(), 2))
	{
		return false;
	}
	TSharedRef<FTedsTestRows> Rows = MakeShared<FTedsTestRows>();
	UPackage* Package = CreatePackage(*(Rows->Folder / TEXT("CV_HeavyMeshesCopy")));
	UContentViewDefinition* Heavy = DuplicateObject<UContentViewDefinition>(Shipped, Package, TEXT("CV_HeavyMeshesCopy"));
	Heavy->SetFlags(RF_Public | RF_Standalone);
	Heavy->DisplayName = FText::FromString(TEXT("CV_HeavyMeshesCopy"));
	Heavy->Filters[0].Value = TEXT("1000");
	Rows->Register(Heavy);

	const double Start = FPlatformTime::Seconds();
	AddCommand(new FFunctionLatentCommand([this, Heavy, Start]()
	{
		const bool bReady = UE::ContentViews::Teds::IsCaughtUp() && UE::ContentViews::Teds::FBackend::Get().FindView(FSoftObjectPath(Heavy));
		if (!bReady && FPlatformTime::Seconds() - Start < 120.0)
		{
			return false;
		}
		TedsTestCheckHeavy(*this, *Heavy, 1000.0, TEXT("at 1000"));
		return true;
	}));
	AddCommand(new FFunctionLatentCommand([this, Rows, Heavy]()
	{
		const TSharedRef<SContentViewsSource> Open = SNew(SContentViewsSource, FSoftObjectPath(Heavy));
		const TSharedPtr<UE::Editor::DataStorage::QueryStack::IRowNode> Before = Open->GetShownRows();
		Heavy->Filters[0].Value = TEXT("5000");
		FPropertyChangedEvent Changed(nullptr);
		FCoreUObjectDelegates::OnObjectPropertyChanged.Broadcast(Heavy, Changed);
		TestTrue(TEXT("the open source rebuilt"), Open->GetShownRows() != Before);
		TedsTestCheckHeavy(*this, *Heavy, 5000.0, TEXT("at 5000"));

		using namespace UE::Editor::DataStorage;
		ICoreProvider* Storage = GetMutableDataStorageFeature<ICoreProvider>(StorageFeatureName);
		QueryStack::FExplicitUpdateExecutor Executor(TEXT("ContentViewsTest"), Open->GetShownRows());
		for (int32 Pass = 0; Pass < 200; ++Pass)
		{
			Executor.Update();
		}
		int32 Shown = 0;
		bool bAllHeavy = true;
		for (const RowHandle Row : Open->GetShownRows()->GetRows())
		{
			if (const FAssetDataColumn_Experimental* Asset = Storage->GetColumn<FAssetDataColumn_Experimental>(Row))
			{
				++Shown;
				bAllHeavy &= TedsTestTriangles(Asset->AssetData) >= 5000.0;
			}
		}
		TestEqual(TEXT("the source's filter bar keeps what the legacy backend keeps"), Shown,
			UE::ContentViews::RunView(*Heavy, {}, FContentViewSort(), 0).Num());
		TestTrue(TEXT("and only meshes at or above 5000"), bAllHeavy);
		return true;
	}));
	return true;
}

namespace
{
	/** Scene Cost's row for Mesh, or none. */
	TOptional<UE::ContentViews::Teds::FSceneCostRow> TedsTestSceneRow(const USceneCostView& View, const TCHAR* Mesh)
	{
		for (const UE::ContentViews::Teds::FSceneCostRow& Row : UE::ContentViews::Teds::ReadSceneRows(View))
		{
			if (Row.Mesh == Mesh)
			{
				return Row;
			}
		}
		return {};
	}

	/** Waits (up to 30 s) until the scene is counted and Ready holds, then runs Then; a timeout fails with What. */
	void TedsTestWhenScene(FAutomationTestBase& Test, const FString& What, TFunction<bool()> Ready, TFunction<void()> Then)
	{
		TSharedRef<double> Start = MakeShared<double>(0.0);
		Test.AddCommand(new FFunctionLatentCommand([&Test, What, Ready, Then, Start]()
		{
			*Start = *Start == 0.0 ? FPlatformTime::Seconds() : *Start;
			const bool bReady = UE::ContentViews::Teds::IsSceneCaughtUp() && Ready();
			if (!bReady && FPlatformTime::Seconds() - *Start < 30.0)
			{
				return false;
			}
			if (Test.TestTrue(*What, bReady))
			{
				Then();
			}
			return true;
		}));
	}

	const TCHAR* const TedsTestCube = TEXT("/Engine/BasicShapes/Cube.Cube");
	const TCHAR* const TedsTestSphere = TEXT("/Engine/BasicShapes/Sphere.Sphere");
}

/**
 * Situation: a new level with five cubes placed, then one deleted; then an actor whose instanced component holds seven
 * spheres, three more added the way PCG adds them (AddInstance, no Modify); then a budget at the cube's scene triangles.
 * Why it matters: Scene Cost is how a designer sees what placed content costs; a count that misses an instance, keeps a
 * deleted actor, or ignores instances PCG adds would point at the wrong mesh.
 * Pass: the cube's row shows Placed 5 and Scene Triangles 5 x its triangles, then 4; the sphere's row counts 7, then 10
 * in one instanced component; with the budget at the cube's scene triangles the spheres carry the alert and the cube does not.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FContentViewsTedsSceneCostTest,
	"ContentViews.Teds.SceneCostCountsPlacementsInstancesAndTheBudget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FContentViewsTedsSceneCostTest::RunTest(const FString& Parameters)
{
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TedsTestCube);
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TedsTestSphere);
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	if (!TestNotNull(TEXT("the engine's cube"), Cube) || !TestNotNull(TEXT("and sphere"), Sphere) || !TestNotNull(TEXT("a new level"), World))
	{
		return false;
	}
	TSharedRef<FTedsTestRows> Rows = MakeShared<FTedsTestRows>();
	UPackage* Package = CreatePackage(*(Rows->Folder / TEXT("CV_SceneCostTest")));
	USceneCostView* View = NewObject<USceneCostView>(Package, TEXT("CV_SceneCostTest"), RF_Public | RF_Standalone);
	View->DisplayName = FText::FromString(TEXT("CV_SceneCostTest"));
	View->TriangleBudget = 1000000000;
	Rows->Register(View);

	TSharedRef<TArray<TWeakObjectPtr<AActor>>> Cubes = MakeShared<TArray<TWeakObjectPtr<AActor>>>();
	for (int32 Index = 0; Index < 5; ++Index)
	{
		AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(FVector(Index * 200.0, 0.0, 0.0), FRotator::ZeroRotator);
		Actor->GetStaticMeshComponent()->SetStaticMesh(Cube);
		Cubes->Add(Actor);
	}

	TedsTestWhenScene(*this, TEXT("five cubes are counted"), [View]()
	{
		const TOptional<UE::ContentViews::Teds::FSceneCostRow> Row = TedsTestSceneRow(*View, TedsTestCube);
		return Row && Row->Placed == 5;
	}, [this, View, World, Cubes]()
	{
		const UE::ContentViews::Teds::FSceneCostRow Row = TedsTestSceneRow(*View, TedsTestCube).GetValue();
		TestTrue(TEXT("a cube has triangles"), Row.Triangles > 0);
		TestEqual(TEXT("scene triangles are placed x triangles"), Row.SceneTriangles, 5 * Row.Triangles);
		TestEqual(TEXT("five static placements"), UE::ContentViews::Teds::ReadPlacements(FSoftObjectPath(TedsTestCube)).Num(), 5);
		TestEqual(TEXT("the totals name the level counted"), UE::ContentViews::Teds::ReadSceneTotals().Level, World->GetMapName());
		World->EditorDestroyActor((*Cubes)[0].Get(), /*bShouldModifyLevel*/ true);
	});

	TedsTestWhenScene(*this, TEXT("a deleted cube is no longer counted"), [View]()
	{
		const TOptional<UE::ContentViews::Teds::FSceneCostRow> Row = TedsTestSceneRow(*View, TedsTestCube);
		return Row && Row->Placed == 4;
	}, [this, View, World, Sphere]()
	{
		const UE::ContentViews::Teds::FSceneCostRow Row = TedsTestSceneRow(*View, TedsTestCube).GetValue();
		TestEqual(TEXT("four cubes' scene triangles"), Row.SceneTriangles, 4 * Row.Triangles);
		AActor* Holder = World->SpawnActor<AActor>();
		UInstancedStaticMeshComponent* Instances = NewObject<UInstancedStaticMeshComponent>(Holder, TEXT("Spheres"));
		Instances->SetStaticMesh(Sphere);
		Holder->SetRootComponent(Instances);
		Holder->AddInstanceComponent(Instances);
		Instances->RegisterComponent();
		for (int32 Index = 0; Index < 7; ++Index)
		{
			Instances->AddInstance(FTransform(FVector(Index * 200.0, 500.0, 0.0)));
		}
	});

	TedsTestWhenScene(*this, TEXT("seven spheres in one instanced component are counted"), [View]()
	{
		const TOptional<UE::ContentViews::Teds::FSceneCostRow> Row = TedsTestSceneRow(*View, TedsTestSphere);
		return Row && Row->Placed == 7;
	}, [this]()
	{
		const TArray<UE::ContentViews::Teds::FScenePlacement> Placements = UE::ContentViews::Teds::ReadPlacements(FSoftObjectPath(TedsTestSphere));
		if (TestEqual(TEXT("one component places them"), Placements.Num(), 1))
		{
			TestEqual(TEXT("an instanced one"), Placements[0].Kind, FString(TEXT("Instanced")));
			TestEqual(TEXT("with seven instances"), Placements[0].Instances, 7);
			// PCG adds instances like this: no Modify, so no sync tag; the engine's instance delegate brings the row back.
			for (int32 Index = 0; Index < 3; ++Index)
			{
				CastChecked<UInstancedStaticMeshComponent>(Placements[0].Component.Get())->AddInstance(FTransform(FVector(Index * 200.0, 900.0, 0.0)));
			}
		}
	});

	TedsTestWhenScene(*this, TEXT("three instances added later are counted"), [View]()
	{
		const TOptional<UE::ContentViews::Teds::FSceneCostRow> Row = TedsTestSceneRow(*View, TedsTestSphere);
		return Row && Row->Placed == 10;
	}, [this, View]()
	{
		TestEqual(TEXT("the totals hold both meshes' instances"), UE::ContentViews::Teds::ReadSceneTotals().Instances, int64(14));
		// The budget at the cube's own scene triangles: the cube meets it, the ten spheres pass it.
		View->TriangleBudget = TedsTestSceneRow(*View, TedsTestCube)->SceneTriangles;
		FPropertyChangedEvent Changed(nullptr);
		FCoreUObjectDelegates::OnObjectPropertyChanged.Broadcast(View, Changed);
	});

	TedsTestWhenScene(*this, TEXT("the budget's alert is written"), [View]()
	{
		const TOptional<UE::ContentViews::Teds::FSceneCostRow> Row = TedsTestSceneRow(*View, TedsTestSphere);
		return Row && !Row->Alert.IsEmpty();
	}, [this, View, Rows]()
	{
		const UE::ContentViews::Teds::FSceneCostRow Cube = TedsTestSceneRow(*View, TedsTestCube).GetValue();
		const UE::ContentViews::Teds::FSceneCostRow Sphere = TedsTestSceneRow(*View, TedsTestSphere).GetValue();
		TestFalse(TEXT("the sphere is not Nanite"), Sphere.bNanite);
		TestTrue(*FString::Printf(TEXT("the sphere's alert names the budget: %s"), *Sphere.Alert), Sphere.Alert.Contains(TEXT("over the budget of")));
		TestTrue(*FString::Printf(TEXT("the cube, at the budget (%lld), has none: %s"), Cube.SceneTriangles, *Cube.Alert), Cube.Alert.IsEmpty());
		const TArray<UE::ContentViews::Teds::FSceneCostRow> All = UE::ContentViews::Teds::ReadSceneRows(*View);
		TestTrue(TEXT("most scene triangles first"), All.Num() == 2 && All[0].Mesh == TedsTestSphere);
	});
	return true;
}

#endif
