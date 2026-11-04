#include "ContentViewsTestRow.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AssetRegistry/AssetRegistryModule.h"
#include "ContentViewColumns.h"
#include "ContentViewDefinition.h"
#include "ContentViewQuery.h"
#include "ContentViewsSettings.h"
#include "ContentViewsTeds.h"
#include "DataStorage/Features.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

namespace
{
	const FName TedsTestCrewCheck(TEXT("CrewCheck"));
	const FName TedsTestSpeedTwice(TEXT("SpeedTwice"));

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
			NewView->Columns = {TEXT("Name"), TEXT("Speed"), TEXT("Crew"), TEXT("bKeeper"), TedsTestSpeedTwice, TedsTestCrewCheck};
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

#endif
