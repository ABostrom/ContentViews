#include "ContentViewQuery.h"

#include "Algo/AllOf.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "ContentViewColumns.h"
#include "ContentViewsUserSettings.h"
#include "Engine/Blueprint.h"
#include "Misc/PackageName.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ContentViewQuery)

#define LOCTEXT_NAMESPACE "ContentViews"

namespace
{
	IAssetRegistry& ContentViewRegistry()
	{
		return FModuleManager::LoadModuleChecked<FAssetRegistryModule>(AssetRegistryConstants::ModuleName).Get();
	}

	/** Whether the class at ClassPath is Base or, with subclasses, derives from it; unloaded Blueprint classes by their tags. */
	bool ContentViewClassDerives(FTopLevelAssetPath ClassPath, const UClass& Base, bool bIncludeSubclasses)
	{
		IAssetRegistry& Registry = ContentViewRegistry();
		// A parent chain longer than this is a cycle in stale tags, not a hierarchy.
		for (int32 Depth = 0; Depth < 64 && ClassPath.IsValid(); ++Depth)
		{
			if (ClassPath == Base.GetClassPathName())
			{
				return true;
			}
			if (!bIncludeSubclasses)
			{
				return false;
			}
			if (const UClass* Loaded = FindObject<UClass>(ClassPath))
			{
				return Loaded->IsChildOf(&Base);
			}

			FString BlueprintPath = ClassPath.ToString();
			BlueprintPath.RemoveFromEnd(TEXT("_C"));
			const FAssetData Blueprint = Registry.GetAssetByObjectPath(FSoftObjectPath(BlueprintPath));
			FString Parent;
			if (!Blueprint.IsValid() || !Blueprint.GetTagValue(FBlueprintTags::ParentClassPath, Parent))
			{
				return false;
			}
			ClassPath = FTopLevelAssetPath(FPackageName::ExportTextPathToObjectPath(Parent));
		}
		return false;
	}

	/** Every asset that is a row of View meeting Filters (none: every row, before the view's own). */
	TArray<FAssetData> ContentViewCandidates(const UContentViewDefinition& View, TConstArrayView<FContentViewFilter> Filters = {})
	{
		TArray<FAssetData> Found;
		const UClass* RowClass = UE::ContentViews::ResolveRowClass(View);
		if (!RowClass)
		{
			return Found;
		}
		ContentViewRegistry().GetAssets(UE::ContentViews::MakeRowFilter(*RowClass, View.bIncludeSubclasses), Found);
		Found.RemoveAll([RowClass, &View, Filters](const FAssetData& Asset)
		{
			return !UE::ContentViews::IsViewRow(Asset, *RowClass, View.bIncludeSubclasses, Filters);
		});
		return Found;
	}

	bool ContentViewMatches(const FContentViewValue& Value, const FString& Wanted)
	{
		if (!Value.bIsSet)
		{
			return false;
		}
		switch (Value.Type)
		{
		case EContentViewValueType::Number:
			return Wanted.IsNumeric() && FMath::IsNearlyEqual(Value.Number, FCString::Atod(*Wanted));
		case EContentViewValueType::Bool:
			return Value.bBool == (Wanted.Equals(TEXT("True"), ESearchCase::IgnoreCase) || Wanted == TEXT("1"));
		default:
			return Value.String.Equals(Wanted, ESearchCase::IgnoreCase)
				|| Value.GetDisplay().Equals(Wanted, ESearchCase::IgnoreCase);
		}
	}

	/** Whether Text holds the gameplay tag Tag or one under it, as a whole word. */
	bool ContentViewHoldsTag(const FString& Text, const FString& Tag)
	{
		FString Word;
		const auto Check = [&Word, &Tag]()
		{
			const bool bHit = Word.Equals(Tag, ESearchCase::IgnoreCase)
				|| Word.StartsWith(Tag + TEXT("."), ESearchCase::IgnoreCase);
			Word.Reset();
			return bHit;
		};
		for (const TCHAR Char : Text)
		{
			if (FChar::IsAlnum(Char) || Char == TEXT('_') || Char == TEXT('.'))
			{
				Word.AppendChar(Char);
			}
			else if (!Word.IsEmpty() && Check())
			{
				return true;
			}
		}
		return !Word.IsEmpty() && Check();
	}
}

namespace UE::ContentViews
{
	UClass* ResolveRowClass(const UContentViewDefinition& View)
	{
		if (View.RowClass.IsNull())
		{
			return nullptr;
		}
		if (UClass* Loaded = View.RowClass.Get())
		{
			return Loaded;
		}
		// A Blueprint row class: the class is loaded, never a row.
		return View.RowClass.LoadSynchronous();
	}

	FARFilter MakeRowFilter(const UClass& RowClass, bool bIncludeSubclasses)
	{
		FARFilter Filter;
		Filter.ClassPaths.Add(RowClass.GetClassPathName());
		if (bIncludeSubclasses)
		{
			Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
		}
		Filter.bRecursiveClasses = bIncludeSubclasses;
		return Filter;
	}

	bool IsRowOf(const FAssetData& Asset, const UClass& RowClass, bool bIncludeSubclasses)
	{
		const UClass* AssetClass = Asset.GetClass(EResolveClass::No);
		if (AssetClass && AssetClass->IsChildOf<UBlueprint>())
		{
			FString Generated;
			if (!bIncludeSubclasses || !Asset.GetTagValue(FBlueprintTags::GeneratedClassPath, Generated))
			{
				return false;
			}
			const FTopLevelAssetPath GeneratedPath(FPackageName::ExportTextPathToObjectPath(Generated));
			// A Blueprint row class's own Blueprint is the class, not one of its rows.
			return GeneratedPath != RowClass.GetClassPathName()
				&& ContentViewClassDerives(GeneratedPath, RowClass, /*bIncludeSubclasses*/ true);
		}
		return ContentViewClassDerives(Asset.AssetClassPath, RowClass, bIncludeSubclasses);
	}

	FText CheckFilter(const FContentViewFilter& Filter)
	{
		if (Filter.Column.IsNone())
		{
			return LOCTEXT("FilterNoColumn", "names no column");
		}
		if (Filter.Operator != EContentViewFilterOp::HasValue && Filter.Value.TrimStartAndEnd().IsEmpty())
		{
			return LOCTEXT("FilterNoValue", "has no value to compare with");
		}
		const bool bNumeric = Filter.Operator == EContentViewFilterOp::AtLeast || Filter.Operator == EContentViewFilterOp::AtMost;
		if (bNumeric && !Filter.Value.TrimStartAndEnd().IsNumeric())
		{
			return FText::Format(LOCTEXT("FilterNotANumber", "compares with '{0}', which is not a number"),
				FText::FromString(Filter.Value));
		}
		return FText::GetEmpty();
	}

	bool PassesFilter(const FAssetData& Asset, const FContentViewFilter& Filter)
	{
		if (!CheckFilter(Filter).IsEmpty())
		{
			return false;
		}
		const FString Wanted = Filter.Value.TrimStartAndEnd();
		const FContentViewValue Value = GetColumn(Filter.Column)->GetValue(Asset);
		switch (Filter.Operator)
		{
		case EContentViewFilterOp::Equals:
			return ContentViewMatches(Value, Wanted);
		case EContentViewFilterOp::NotEquals:
			return !ContentViewMatches(Value, Wanted);
		case EContentViewFilterOp::AtLeast:
			return Value.bIsSet && Value.Type == EContentViewValueType::Number && Value.Number >= FCString::Atod(*Wanted);
		case EContentViewFilterOp::AtMost:
			return Value.bIsSet && Value.Type == EContentViewValueType::Number && Value.Number <= FCString::Atod(*Wanted);
		case EContentViewFilterOp::Contains:
			return Value.bIsSet && (Value.String.Contains(Wanted) || Value.GetDisplay().Contains(Wanted));
		case EContentViewFilterOp::HasTag:
			return Value.bIsSet && ContentViewHoldsTag(Value.String, Wanted);
		case EContentViewFilterOp::HasValue:
			return Value.bIsSet;
		}
		return false;
	}

	bool IsViewRow(const FAssetData& Asset, const UClass& RowClass, bool bIncludeSubclasses,
		TConstArrayView<FContentViewFilter> Filters)
	{
		// An external actor is a part of its level, not an asset of its own: an Actor view must not list them.
		return Asset.GetOptionalOuterPathName().IsNone() && IsRowOf(Asset, RowClass, bIncludeSubclasses)
			&& Algo::AllOf(Filters, [&Asset](const FContentViewFilter& Filter) { return PassesFilter(Asset, Filter); });
	}

	TArray<FAssetData> GatherRows(const UContentViewDefinition& View, TConstArrayView<FContentViewFilter> Extra)
	{
		TArray<FContentViewFilter> Filters(View.Filters);
		Filters.Append(Extra.GetData(), Extra.Num());
		return ContentViewCandidates(View, Filters);
	}

	void SortRows(TArray<FAssetData>& Rows, const FContentViewSort& Sort)
	{
		const FName Column = Sort.Column.IsNone() ? FName(TEXT("Name")) : Sort.Column;
		const TSharedRef<const IContentViewColumn> Reader = GetColumn(Column);
		TArray<TPair<FContentViewValue, FAssetData>> Keyed;
		Keyed.Reserve(Rows.Num());
		for (FAssetData& Row : Rows)
		{
			Keyed.Emplace(Reader->GetValue(Row), MoveTemp(Row));
		}
		Keyed.StableSort([&Sort](const TPair<FContentViewValue, FAssetData>& A, const TPair<FContentViewValue, FAssetData>& B)
		{
			int32 Order = FContentViewValue::Compare(A.Key, B.Key);
			// Unset cells stay last whichever way the column runs.
			if (Sort.bDescending && A.Key.bIsSet && B.Key.bIsSet)
			{
				Order = -Order;
			}
			return Order != 0 ? Order < 0 : A.Value.AssetName.LexicalLess(B.Value.AssetName);
		});
		Rows.Reset();
		for (TPair<FContentViewValue, FAssetData>& Pair : Keyed)
		{
			Rows.Add(MoveTemp(Pair.Value));
		}
	}

	TArray<FContentViewRow> RunView(const UContentViewDefinition& View, TConstArrayView<FContentViewFilter> Extra,
		const FContentViewSort& Sort, int32 Limit)
	{
		TArray<FAssetData> Assets = GatherRows(View, Extra);
		SortRows(Assets, Sort.Column.IsNone() ? View.DefaultSort : Sort);
		if (Limit > 0 && Assets.Num() > Limit)
		{
			Assets.SetNum(Limit);
		}

		TArray<TPair<FName, TSharedRef<const IContentViewColumn>>> Readers;
		for (const FName Column : View.Columns)
		{
			Readers.Emplace(Column, GetColumn(Column));
		}

		TArray<FContentViewRow> Rows;
		Rows.Reserve(Assets.Num());
		for (const FAssetData& Asset : Assets)
		{
			FContentViewRow& Row = Rows.AddDefaulted_GetRef();
			Row.AssetPath = Asset.GetSoftObjectPath().ToString();
			for (const TPair<FName, TSharedRef<const IContentViewColumn>>& Reader : Readers)
			{
				Row.Values.Add(Reader.Key.ToString(), Reader.Value->GetValue(Asset));
			}
		}
		return Rows;
	}

	TArray<FContentViewColumnInfo> DescribeColumns(const UContentViewDefinition& View)
	{
		const TArray<FAssetData> Rows = GatherRows(View);
		TArray<FContentViewColumnInfo> Infos;
		for (const FName Column : View.Columns)
		{
			const TSharedRef<const IContentViewColumn> Reader = GetColumn(Column);
			FContentViewColumnInfo& Info = Infos.AddDefaulted_GetRef();
			Info.Name = Column.ToString();
			Info.bComputed = IsColumnRegistered(Column);
			Info.Tooltip = Reader->GetTooltip().ToString();
			if (const TOptional<EContentViewValueType> Fixed = Reader->GetType())
			{
				Info.Type = *Fixed;
				continue;
			}
			int32 Counts[3] = {0, 0, 0};
			for (const FAssetData& Row : Rows)
			{
				const FContentViewValue Value = Reader->GetValue(Row);
				if (Value.bIsSet)
				{
					++Counts[static_cast<int32>(Value.Type)];
				}
			}
			int32 Most = 0;
			for (int32 Type = 1; Type < 3; ++Type)
			{
				Most = Counts[Type] > Counts[Most] ? Type : Most;
			}
			Info.Type = static_cast<EContentViewValueType>(Most);
		}
		return Infos;
	}

	bool IsKnownColumn(const UContentViewDefinition& View, FName Column)
	{
		if (IsColumnRegistered(Column))
		{
			return true;
		}
		const TArray<FAssetData> Candidates = ContentViewCandidates(View);
		// With no rows at all nothing can be told about a tag, so it is not refused.
		return Candidates.IsEmpty() || Candidates.ContainsByPredicate([Column](const FAssetData& Asset)
		{
			return Asset.TagsAndValues.Contains(Column);
		});
	}

	TArray<FAssetData> FindViews(bool bIncludeHidden)
	{
		TArray<FAssetData> Views;
		ContentViewRegistry().GetAssetsByClass(UContentViewBase::StaticClass()->GetClassPathName(), Views,
			/*bSearchSubClasses*/ true);
		if (!bIncludeHidden)
		{
			Views.RemoveAll([](const FAssetData& View) { return UContentViewsUserSettings::IsHidden(View.GetSoftObjectPath()); });
		}
		Views.Sort([](const FAssetData& A, const FAssetData& B) { return A.AssetName.LexicalLess(B.AssetName); });
		return Views;
	}

	UContentViewBase* FindViewAsset(const FString& Name)
	{
		// A hidden view is still run by name: hiding tidies the lists, it does not take the view away.
		for (const FAssetData& View : FindViews(/*bIncludeHidden*/ true))
		{
			FText Shown;
			const bool bShownMatches = View.GetTagValue(GET_MEMBER_NAME_CHECKED(UContentViewBase, DisplayName), Shown)
				&& Shown.ToString().Equals(Name, ESearchCase::IgnoreCase);
			if (bShownMatches || View.AssetName.ToString().Equals(Name, ESearchCase::IgnoreCase)
				|| View.GetObjectPathString().Equals(Name, ESearchCase::IgnoreCase))
			{
				return Cast<UContentViewBase>(View.GetAsset());
			}
		}
		return nullptr;
	}

	UContentViewDefinition* FindView(const FString& Name)
	{
		return Cast<UContentViewDefinition>(FindViewAsset(Name));
	}
}

#undef LOCTEXT_NAMESPACE
