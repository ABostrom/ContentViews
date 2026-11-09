#include "ContentViewsTestRow.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AssetRegistry/AssetRegistryModule.h"
#include "ContentViewColumns.h"
#include "ContentViewDefinition.h"
#include "ContentViewQuery.h"
#include "ContentViewWidgets.h"
#include "ContentViewsFilterExtension.h"
#include "ContentViewsTools.h"
#include "ContentViewsSettings.h"
#include "ContentViewsUserSettings.h"
#include "Misc/ScopeExit.h"
#include "FrontendFilterBase.h"
#include "Widgets/Layout/SSpacer.h"
#include "Dom/JsonObject.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "Misc/DataValidation.h"
#include "SContentViewsTab.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "ToolsetRegistry/ToolsetRegistrySubsystem.h"
#include "UObject/Package.h"

namespace
{
	/**
	 * Three rows of the test class in a fresh /Temp folder (two assets and a Blueprint child), one asset of another
	 * class, and a view over them, all in memory and so in the asset registry until the fixture goes.
	 */
	struct FContentViewsTestRows
	{
		FString Folder;
		TArray<UObject*> Made;
		UContentViewDefinition* View = nullptr;

		FContentViewsTestRows()
		{
			Folder = FString::Printf(TEXT("/Temp/ContentViewsTest_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
			MakeRow(TEXT("Row_Fast"), 90.f, TEXT("Red"), false, TEXT("Role.Striker"));
			MakeRow(TEXT("Row_Slow"), 40.f, TEXT("Blue"), true, TEXT("Role.Keeper,Role.Tank"));

			UPackage* BlueprintPackage = CreatePackage(*(Folder / TEXT("BP_Row_Child")));
			UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(UContentViewsTestRow::StaticClass(),
				BlueprintPackage, TEXT("BP_Row_Child"), BPTYPE_Normal, UBlueprint::StaticClass(),
				UBlueprintGeneratedClass::StaticClass());
			CastChecked<UContentViewsTestRow>(Blueprint->GeneratedClass->GetDefaultObject())->Speed = 65.f;
			Register(Blueprint);

			UPackage* OtherPackage = CreatePackage(*(Folder / TEXT("Other_Fast")));
			UContentViewsTestOther* Other = NewObject<UContentViewsTestOther>(OtherPackage, TEXT("Other_Fast"),
				RF_Public | RF_Standalone);
			Other->Speed = 99.f;
			Register(Other);

			UPackage* ViewPackage = CreatePackage(*(Folder / TEXT("CV_ContentViewsTest")));
			View = NewObject<UContentViewDefinition>(ViewPackage, TEXT("CV_ContentViewsTest"), RF_Public | RF_Standalone);
			View->DisplayName = FText::FromString(TEXT("Content Views Test"));
			View->RowClass = UContentViewsTestRow::StaticClass();
			View->Columns = {TEXT("Name"), TEXT("Speed"), TEXT("Crew"), TEXT("bKeeper"), TEXT("Roles")};
			Register(View);
		}

		~FContentViewsTestRows()
		{
			for (UObject* Object : Made)
			{
				FAssetRegistryModule::AssetDeleted(Object);
				Object->ClearFlags(RF_Public | RF_Standalone);
				Object->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
				Object->MarkAsGarbage();
			}
		}

		void MakeRow(const TCHAR* Name, float Speed, const TCHAR* Crew, bool bKeeper, const TCHAR* Roles)
		{
			UPackage* Package = CreatePackage(*(Folder / Name));
			UContentViewsTestRow* Row = NewObject<UContentViewsTestRow>(Package, Name, RF_Public | RF_Standalone);
			Row->Speed = Speed;
			Row->Crew = Crew;
			Row->bKeeper = bKeeper;
			Row->Roles = Roles;
			Register(Row);
		}

		void Register(UObject* Object)
		{
			FAssetRegistryModule::AssetCreated(Object);
			Made.Add(Object);
		}

		/** The rows this fixture made, by name, comma separated, in the order the view gives them. */
		FString Names(TConstArrayView<FContentViewFilter> Extra = {}, FContentViewSort Sort = FContentViewSort()) const
		{
			TArray<FString> Out;
			for (const FContentViewRow& Row : UE::ContentViews::RunView(*View, Extra, Sort, 0))
			{
				if (Row.AssetPath.StartsWith(Folder))
				{
					Out.Add(Row.Values.FindChecked(TEXT("Name")).String);
				}
			}
			return FString::Join(Out, TEXT(","));
		}

		static FContentViewFilter Filter(const TCHAR* Column, EContentViewFilterOp Op, const TCHAR* Value)
		{
			FContentViewFilter Out;
			Out.Column = Column;
			Out.Operator = Op;
			Out.Value = Value;
			return Out;
		}
	};

	/** Waits for a tool call's answer, then hands it on. */
	class FContentViewsWaitForTool final : public IAutomationLatentCommand
	{
	public:
		FContentViewsWaitForTool(TFuture<TValueOrError<FString, FString>>&& InFuture,
			TFunction<void(const TValueOrError<FString, FString>&)> InOnDone)
			: Future(MoveTemp(InFuture)), OnDone(MoveTemp(InOnDone))
		{
		}

		virtual bool Update() override
		{
			if (!Future.IsReady())
			{
				return false;
			}
			OnDone(Future.Get());
			return true;
		}

	private:
		TFuture<TValueOrError<FString, FString>> Future;
		TFunction<void(const TValueOrError<FString, FString>&)> OnDone;
	};
}

/**
 * Situation: a view over a class whose assets are two plain assets and a Blueprint child, beside an asset of another class.
 * Why it matters: a view is only useful if it lists exactly that class's content, Blueprints included, with numbers as
 * numbers so they sort and compare as numbers.
 * Pass: the three rows and not the other class, by speed, with typed values; without subclasses the Blueprint goes.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FContentViewsListsTheClassTest,
	"ContentViews.AViewListsItsClassAndItsBlueprintChildrenWithTypedValues",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FContentViewsListsTheClassTest::RunTest(const FString& Parameters)
{
	FContentViewsTestRows Rows;
	FContentViewSort BySpeed;
	BySpeed.Column = TEXT("Speed");
	BySpeed.bDescending = true;
	TestEqual(TEXT("the class's rows by speed, the Blueprint child among them"),
		Rows.Names({}, BySpeed), FString(TEXT("Row_Fast,BP_Row_Child,Row_Slow")));

	const TArray<FContentViewRow> All = UE::ContentViews::RunView(*Rows.View, {}, BySpeed, 0);
	const FContentViewRow* Fast = All.FindByPredicate([](const FContentViewRow& Row) { return Row.AssetPath.EndsWith(TEXT("Row_Fast.Row_Fast")); });
	if (!TestNotNull(TEXT("the fast row"), Fast))
	{
		return false;
	}
	const FContentViewValue& Speed = Fast->Values.FindChecked(TEXT("Speed"));
	TestTrue(TEXT("Speed is a number"), Speed.bIsSet && Speed.Type == EContentViewValueType::Number && Speed.Number == 90.0);
	const FContentViewValue& Crew = Fast->Values.FindChecked(TEXT("Crew"));
	TestTrue(TEXT("Crew is the string Red"), Crew.Type == EContentViewValueType::String && Crew.String == TEXT("Red"));
	const FContentViewValue& Keeper = Fast->Values.FindChecked(TEXT("bKeeper"));
	TestTrue(TEXT("bKeeper is the bool false"), Keeper.bIsSet && Keeper.Type == EContentViewValueType::Bool && !Keeper.bBool);

	Rows.View->bIncludeSubclasses = false;
	TestEqual(TEXT("without subclasses only the class's own assets"), Rows.Names(),
		FString(TEXT("Row_Fast,Row_Slow")));
	return true;
}

/**
 * Situation: the same rows, filtered by number, inequality, gameplay tag and substring.
 * Why it matters: filters are how a view says "Pace >= 80" or "is a keeper"; one that lets the wrong row through lies.
 * Pass: each filter keeps exactly the rows that meet it, and a non-number for >= is refused.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FContentViewsFiltersTest,
	"ContentViews.FiltersKeepOnlyTheRowsThatMeetThem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FContentViewsFiltersTest::RunTest(const FString& Parameters)
{
	using F = FContentViewsTestRows;
	FContentViewsTestRows Rows;
	TestEqual(TEXT("Speed >= 60"), Rows.Names({F::Filter(TEXT("Speed"), EContentViewFilterOp::AtLeast, TEXT("60"))}),
		FString(TEXT("BP_Row_Child,Row_Fast")));
	TestEqual(TEXT("Speed <= 40"), Rows.Names({F::Filter(TEXT("Speed"), EContentViewFilterOp::AtMost, TEXT("40"))}),
		FString(TEXT("Row_Slow")));
	TestEqual(TEXT("Crew != Red (the child has no crew)"), Rows.Names({F::Filter(TEXT("Crew"), EContentViewFilterOp::NotEquals, TEXT("red"))}),
		FString(TEXT("BP_Row_Child,Row_Slow")));
	TestEqual(TEXT("bKeeper = True"), Rows.Names({F::Filter(TEXT("bKeeper"), EContentViewFilterOp::Equals, TEXT("True"))}),
		FString(TEXT("Row_Slow")));
	TestEqual(TEXT("Roles has tag Role"), Rows.Names({F::Filter(TEXT("Roles"), EContentViewFilterOp::HasTag, TEXT("Role"))}),
		FString(TEXT("Row_Fast,Row_Slow")));
	TestEqual(TEXT("Roles has tag Role.Tank"), Rows.Names({F::Filter(TEXT("Roles"), EContentViewFilterOp::HasTag, TEXT("Role.Tank"))}),
		FString(TEXT("Row_Slow")));
	TestEqual(TEXT("Roles has tag Role.Tan is no tag"), Rows.Names({F::Filter(TEXT("Roles"), EContentViewFilterOp::HasTag, TEXT("Role.Tan"))}),
		FString(TEXT("")));
	TestEqual(TEXT("Crew contains ue"), Rows.Names({F::Filter(TEXT("Crew"), EContentViewFilterOp::Contains, TEXT("UE"))}),
		FString(TEXT("Row_Slow")));
	TestFalse(TEXT("Speed >= fast is refused"),
		UE::ContentViews::CheckFilter(F::Filter(TEXT("Speed"), EContentViewFilterOp::AtLeast, TEXT("fast"))).IsEmpty());
	return true;
}

/**
 * Situation: a view naming a column nothing provides, a filter with a word for a number, and a sort on no column.
 * Why it matters: a mistyped column is an empty column nobody notices; validation is where the designer hears of it.
 * Pass: the view is invalid, with one error naming each problem.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FContentViewsValidationTest,
	"ContentViews.ValidationNamesAnUnknownColumnABadFilterAndALostSort",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FContentViewsValidationTest::RunTest(const FString& Parameters)
{
	FContentViewsTestRows Rows;
	FDataValidationContext Good;
	TestEqual(TEXT("the fixture's view is valid"), Rows.View->IsDataValid(Good), EDataValidationResult::Valid);

	Rows.View->Columns.Add(TEXT("Spede"));
	Rows.View->Filters.Add(FContentViewsTestRows::Filter(TEXT("Speed"), EContentViewFilterOp::AtLeast, TEXT("fast")));
	Rows.View->DefaultSort.Column = TEXT("Pace");
	FDataValidationContext Bad;
	TestEqual(TEXT("the broken view is invalid"), Rows.View->IsDataValid(Bad), EDataValidationResult::Invalid);
	TestEqual(TEXT("three errors"), Bad.GetNumErrors(), 3u);
	const auto Says = [&Bad](const TCHAR* Word)
	{
		return Bad.GetIssues().ContainsByPredicate([Word](const FDataValidationContext::FIssue& Issue)
		{
			return Issue.Message.ToString().Contains(Word);
		});
	};
	TestTrue(TEXT("names the unknown column"), Says(TEXT("'Spede'")));
	TestTrue(TEXT("names the bad filter"), Says(TEXT("Speed >= fast")));
	TestTrue(TEXT("names the lost sort"), Says(TEXT("'Pace'")));
	return true;
}

/**
 * Situation: an agent calls the RunView tool through the toolset registry, as MCP does, sorted and limited.
 * Why it matters: an agent acts on the rows, so each needs its asset path, and numbers must arrive as numbers.
 * Pass: two rows, fastest first, each with its path and Speed a JSON number, Crew a string, bKeeper a bool, unset null.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FContentViewsRunViewToolTest,
	"ContentViews.RunViewAnswersWithPathsAndTypedJson",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FContentViewsRunViewToolTest::RunTest(const FString& Parameters)
{
	TValueOrError<TObjectPtr<UToolsetRegistrySubsystem>, FString> Subsystem = UToolsetRegistrySubsystem::Get();
	if (!TestTrue(TEXT("the toolset registry is running"), Subsystem.HasValue()))
	{
		return false;
	}

	TSharedRef<FContentViewsTestRows> Rows = MakeShared<FContentViewsTestRows>();
	const FString Input = TEXT(R"({"view":"CV_ContentViewsTest","filters":[],"sortColumn":"Speed","bSortDescending":true,"limit":2})");
	AddCommand(new FContentViewsWaitForTool(
		Subsystem.GetValue()->ToolsetRegistry.ExecuteTool(TEXT("ContentViews.ContentViewsTools.RunView"), Input),
		[this, Rows](const TValueOrError<FString, FString>& Result)
		{
			if (!TestTrue(TEXT("the tool answered"), Result.HasValue()))
			{
				AddError(Result.GetError());
				return;
			}
			TSharedPtr<FJsonObject> Root;
			FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Result.GetValue()), Root);
			const TArray<TSharedPtr<FJsonValue>>* Found = nullptr;
			if (!TestTrue(TEXT("a returnValue array"), Root.IsValid() && Root->TryGetArrayField(TEXT("returnValue"), Found)))
			{
				AddInfo(Result.GetValue());
				return;
			}
			if (!TestEqual(TEXT("two rows, the limit"), Found->Num(), 2))
			{
				return;
			}
			const TSharedPtr<FJsonObject> First = (*Found)[0]->AsObject();
			FString Path;
			TestTrue(TEXT("the first row has its asset path"), First->TryGetStringField(TEXT("assetPath"), Path)
				&& Path == Rows->Folder / TEXT("Row_Fast.Row_Fast"));
			const TSharedPtr<FJsonObject> Values = First->GetObjectField(TEXT("values"));
			TestEqual(TEXT("Speed is a JSON number"), Values->GetField<EJson::None>(TEXT("Speed"))->Type, EJson::Number);
			TestEqual(TEXT("Speed is 90"), Values->GetNumberField(TEXT("Speed")), 90.0);
			TestEqual(TEXT("Crew is a JSON string"), Values->GetStringField(TEXT("Crew")), FString(TEXT("Red")));
			TestEqual(TEXT("bKeeper is a JSON bool"), Values->GetField<EJson::None>(TEXT("bKeeper"))->Type, EJson::Boolean);
			const TSharedPtr<FJsonObject> Second = (*Found)[1]->AsObject()->GetObjectField(TEXT("values"));
			TestEqual(TEXT("the child's unset Crew is null"), Second->GetField<EJson::None>(TEXT("Crew"))->Type, EJson::Null);
		}));
	return true;
}

/**
 * Situation: the tab's right-click menu built for one row of every view asset (the fixture's and the project's), and for
 * the fixture's rows together, with every extender the editor has registered on the Content Browser's asset menu.
 * Why it matters: extenders read the Content Browser's menu contexts without checking them; a context the tab leaves out
 * crashed the editor on a right-click (PluginBrowser read the file-menu context, null).
 * Pass: a menu for each, and the editor still running.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FContentViewsContextMenuTest,
	"ContentViews.TheRightClickMenuOpensOnARowOfEveryView",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FContentViewsContextMenuTest::RunTest(const FString& Parameters)
{
	FContentViewsTestRows Rows;
	const TArray<FAssetData> FixtureRows = UE::ContentViews::GatherRows(*Rows.View);
	TestEqual(TEXT("the fixture's three rows"), FixtureRows.Num(), 3);
	TestTrue(TEXT("a menu for the fixture's rows together"), UE::ContentViews::MakeAssetContextMenu(FixtureRows).IsValid());

	FModuleManager::LoadModuleChecked<FAssetRegistryModule>(AssetRegistryConstants::ModuleName).Get().WaitForCompletion();
	for (const FAssetData& ViewAsset : UE::ContentViews::FindViews())
	{
		const UContentViewDefinition* View = Cast<UContentViewDefinition>(ViewAsset.GetAsset());
		const TArray<FAssetData> ViewRows = View ? UE::ContentViews::GatherRows(*View) : TArray<FAssetData>();
		if (!ViewRows.IsEmpty())
		{
			TestTrue(*FString::Printf(TEXT("a menu for %s in %s"), *ViewRows[0].AssetName.ToString(), *View->GetName()),
				UE::ContentViews::MakeAssetContextMenu({ViewRows[0]}).IsValid());
		}
	}
	return true;
}

/**
 * Situation: an open Content Views tab while a new view asset and a new row asset (in memory, never saved) are made.
 * Why it matters: a view is a new asset and no code; a designer who makes one, or a new asset of its class, expects to see
 * it at once.
 * Pass: the tab lists the new view as soon as it exists, and the view's rows hold the new row with its tag value.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FContentViewsLiveTest,
	"ContentViews.ANewViewAndANewRowAppearInTheOpenTab",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FContentViewsLiveTest::RunTest(const FString& Parameters)
{
	FContentViewsTestRows Rows;
	const TSharedRef<SContentViewsTab> Tab = SNew(SContentViewsTab);
	TestTrue(TEXT("the open tab lists the fixture's view"), Tab->GetListedViews().Contains(FSoftObjectPath(Rows.View)));

	UPackage* Package = CreatePackage(*(Rows.Folder / TEXT("CV_ContentViewsTestSecond")));
	UContentViewDefinition* Second = NewObject<UContentViewDefinition>(Package, TEXT("CV_ContentViewsTestSecond"), RF_Public | RF_Standalone);
	Second->RowClass = UContentViewsTestRow::StaticClass();
	Second->Columns = {TEXT("Name"), TEXT("Speed")};
	Rows.Register(Second);
	TestTrue(TEXT("the open tab lists the new view at once"), Tab->GetListedViews().Contains(FSoftObjectPath(Second)));

	Rows.MakeRow(TEXT("Row_New"), 77.f, TEXT("Gold"), false, TEXT(""));
	const TArray<FContentViewRow> Found = UE::ContentViews::RunView(*Second, {}, FContentViewSort(), 0);
	const FContentViewRow* New = Found.FindByPredicate([](const FContentViewRow& Row) { return Row.AssetPath.EndsWith(TEXT("Row_New.Row_New")); });
	TestTrue(TEXT("the unsaved row is a row of the new view with its Speed"),
		New && New->Values.FindChecked(TEXT("Speed")).Number == 77.0);
	return true;
}

/**
 * Situation: beside the fixture's asset view, a view of another archetype (as Scene Cost is), in an open tab, the
 * Content Browser's filter list and the ListViews tool; then a widget registered for its class.
 * Why it matters: Scene Cost was missing from the tab, the menu and MCP because each list found asset views only.
 * Pass: every list finds it with its archetype; it adds no asset filter; the tab says nothing draws it until a widget is
 * registered for its class, then shows that widget.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FContentViewsOtherArchetypeTest,
	"ContentViews.EveryListFindsAViewOfAnotherArchetype",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FContentViewsOtherArchetypeTest::RunTest(const FString& Parameters)
{
	FContentViewsTestRows Rows;
	UPackage* Package = CreatePackage(*(Rows.Folder / TEXT("CV_ContentViewsTestOther")));
	UContentViewsTestOtherView* Other = NewObject<UContentViewsTestOtherView>(Package, TEXT("CV_ContentViewsTestOther"), RF_Public | RF_Standalone);
	Other->DisplayName = FText::FromString(TEXT("Content Views Test Other"));
	Rows.Register(Other);
	const FSoftObjectPath OtherPath(Other);

	TestTrue(TEXT("FindViews finds it"), UE::ContentViews::FindViews().ContainsByPredicate(
		[&OtherPath](const FAssetData& Asset) { return Asset.GetSoftObjectPath() == OtherPath; }));
	TestTrue(TEXT("FindViewAsset finds it by display name"), UE::ContentViews::FindViewAsset(TEXT("Content Views Test Other")) == Other);
	TestNull(TEXT("FindView, asset views only, does not"), UE::ContentViews::FindView(TEXT("Content Views Test Other")));

	const TSharedRef<SContentViewsTab> Tab = SNew(SContentViewsTab);
	TestTrue(TEXT("the tab lists it"), Tab->GetListedViews().Contains(OtherPath));

	TArray<TSharedRef<FFrontendFilter>> Filters;
	GetDefault<UContentViewsFilterExtension>()->AddFrontEndFilterExtensions(nullptr, Filters);
	const auto HasFilter = [&Filters](const FString& Name)
	{
		return Filters.ContainsByPredicate([&Name](const TSharedRef<FFrontendFilter>& Filter) { return Filter->GetName() == Name; });
	};
	TestTrue(TEXT("the asset view is a filter"), HasFilter(TEXT("ContentView_CV_ContentViewsTest")));
	TestFalse(TEXT("the other archetype is not"), HasFilter(TEXT("ContentView_CV_ContentViewsTestOther")));

	const TArray<FContentViewSummary> Listed = UContentViewsTools::ListViews();
	const FContentViewSummary* Summary = Listed.FindByPredicate([&OtherPath](const FContentViewSummary& Each)
	{
		return Each.AssetPath == OtherPath.ToString();
	});
	if (TestNotNull(TEXT("ListViews lists it"), Summary))
	{
		TestEqual(TEXT("with its archetype"), Summary->Archetype, FString(TEXT("ContentViewsTestOtherView")));
		TestTrue(TEXT("and no row class"), Summary->RowClass.IsEmpty());
	}

	TestEqual(TEXT("with nothing registered the tab says nothing draws it"),
		UE::ContentViews::MakeViewWidget(*Other)->GetType(), FName(TEXT("STextBlock")));
	UE::ContentViews::RegisterViewWidget(*UContentViewsTestOtherView::StaticClass(), [](UContentViewBase&) -> TSharedRef<SWidget>
	{
		return SNew(SSpacer);
	});
	TestEqual(TEXT("with a widget registered for its class the tab shows it"),
		UE::ContentViews::MakeViewWidget(*Other)->GetType(), FName(TEXT("SSpacer")));
	UE::ContentViews::UnregisterViewWidget(*UContentViewsTestOtherView::StaticClass());
	return true;
}


/**
 * Situation: the Used By column on an engine mesh the plugin's example level places, and on a view asset nothing references.
 * Why it matters: Used By is how a view says where an asset is needed before someone deletes or changes it; a missing
 * user, or a user invented for an asset nothing names, sends them the wrong way.
 * Pass: the cube lists the example level by path and name; the Scene Cost view lists nobody.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FContentViewsUsedByTest,
	"ContentViews.UsedByListsTheAssetsThatReferenceARow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FContentViewsUsedByTest::RunTest(const FString& Parameters)
{
	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(AssetRegistryConstants::ModuleName).Get();
	Registry.WaitForCompletion();
	const TSharedRef<const IContentViewColumn> UsedBy = UE::ContentViews::GetColumn(TEXT("UsedBy"));
	TestTrue(TEXT("Used By is a built-in column"), UE::ContentViews::IsColumnRegistered(TEXT("UsedBy")));

	const FAssetData Cube = Registry.GetAssetByObjectPath(FSoftObjectPath(TEXT("/Engine/BasicShapes/Cube.Cube")));
	if (!TestTrue(TEXT("the engine cube"), Cube.IsValid()))
	{
		return false;
	}
	const FContentViewValue CubeUsers = UsedBy->GetValue(Cube);
	TestTrue(TEXT("the cube is used by the example level"),
		UE::ContentViews::SplitList(CubeUsers.String).Contains(TEXT("/ContentViews/Examples/L_SceneCostExample.L_SceneCostExample")));
	TestTrue(TEXT("and shows its name"), CubeUsers.GetDisplay().Contains(TEXT("L_SceneCostExample")));

	const FAssetData View = Registry.GetAssetByObjectPath(FSoftObjectPath(TEXT("/ContentViews/Examples/CV_SceneCost.CV_SceneCost")));
	if (!TestTrue(TEXT("the Scene Cost view"), View.IsValid()))
	{
		return false;
	}
	TestFalse(TEXT("nothing uses a view"), UsedBy->GetValue(View).bIsSet);
	return true;
}

/**
 * Situation: the fixture's view, hidden by this user, then unhidden.
 * Why it matters: a long view list gets noisy (ContentViews#3); a user hides the views he does not use, for himself only,
 * and a hidden view must leave every list or it is still noise.
 * Pass: the choice is a per-user setting; hidden, the view leaves FindViews, the tab, the Content Browser's filters and
 * ListViews, while Show Hidden, ListViews with bIncludeHidden and FindViewAsset by name still reach it; unhidden, it
 * returns to every list, as for a user who never hid anything.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FContentViewsHideTest,
	"ContentViews.AHiddenViewLeavesEveryListUntilUnhidden",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FContentViewsHideTest::RunTest(const FString& Parameters)
{
	FContentViewsTestRows Rows;
	const FSoftObjectPath Path(Rows.View);
	UContentViewsUserSettings* Settings = GetMutableDefault<UContentViewsUserSettings>();
	const TArray<FSoftObjectPath> Before = Settings->HiddenViews;
	ON_SCOPE_EXIT
	{
		Settings->HiddenViews = Before;
		Settings->SaveConfig();
	};
	TestEqual(TEXT("the choice is this user's, never shared"), UContentViewsUserSettings::StaticClass()->ClassConfigName,
		FName(TEXT("EditorPerProjectUserSettings")));

	const TSharedRef<SContentViewsTab> Tab = SNew(SContentViewsTab);
	const auto Found = [&Path]() { return UE::ContentViews::FindViews().ContainsByPredicate([&Path](const FAssetData& Asset) { return Asset.GetSoftObjectPath() == Path; }); };
	const auto Filtered = [&Rows]()
	{
		TArray<TSharedRef<FFrontendFilter>> Filters;
		GetDefault<UContentViewsFilterExtension>()->AddFrontEndFilterExtensions(nullptr, Filters);
		const FString Name = TEXT("ContentView_") + Rows.View->GetName();
		return Filters.ContainsByPredicate([&Name](const TSharedRef<FFrontendFilter>& Filter) { return Filter->GetName() == Name; });
	};
	const auto Listed = [&Path](bool bIncludeHidden)
	{
		return UContentViewsTools::ListViews(bIncludeHidden).ContainsByPredicate([&Path](const FContentViewSummary& Each) { return Each.AssetPath == Path.ToString(); });
	};
	TestTrue(TEXT("shown at first"), Found() && Tab->GetListedViews().Contains(Path) && Listed(false));

	UContentViewsUserSettings::SetHidden(Path, true);
	TestFalse(TEXT("hidden, FindViews leaves it out"), Found());
	TestFalse(TEXT("hidden, the open tab leaves it out"), Tab->GetListedViews().Contains(Path));
	TestFalse(TEXT("hidden, ListViews leaves it out"), Listed(false));
	TestTrue(TEXT("hidden, ListViews with bIncludeHidden lists it"), Listed(true));
	TestTrue(TEXT("hidden, it still runs by name"), UE::ContentViews::FindViewAsset(Rows.View->GetName()) == Rows.View);
	if (UContentViewsSettings::IsLegacyActive())
	{
		TestFalse(TEXT("hidden, the Content Browser has no filter for it"), Filtered());
	}
	Tab->SetShowHidden(true);
	TestTrue(TEXT("Show Hidden lists it again"), Tab->GetListedViews().Contains(Path));
	Tab->SetShowHidden(false);

	UContentViewsUserSettings::SetHidden(Path, false);
	TestTrue(TEXT("unhidden, FindViews finds it"), Found());
	TestTrue(TEXT("unhidden, the open tab lists it"), Tab->GetListedViews().Contains(Path));
	TestTrue(TEXT("unhidden, ListViews lists it"), Listed(false));
	if (UContentViewsSettings::IsLegacyActive())
	{
		TestTrue(TEXT("unhidden, the Content Browser has its filter"), Filtered());
	}
	return true;
}

#endif
