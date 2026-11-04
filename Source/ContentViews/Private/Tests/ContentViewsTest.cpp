#include "ContentViewsTestRow.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AssetRegistry/AssetRegistryModule.h"
#include "ContentViewDefinition.h"
#include "ContentViewQuery.h"
#include "Dom/JsonObject.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "Misc/DataValidation.h"
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

#endif
