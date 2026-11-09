#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/AssetData.h"
#include "ContentViewDefinition.h"
#include "ContentViewValue.h"
#include "ContentViewQuery.generated.h"

/** One row of a view: the asset and its value in each of the view's columns. */
USTRUCT(BlueprintType)
struct CONTENTVIEWS_API FContentViewRow
{
	GENERATED_BODY()

	/** The asset's object path, for opening or editing it. */
	UPROPERTY(BlueprintReadOnly, Category = "Content Views")
	FString AssetPath;

	/** Column name -> value. A column the asset has no value for is null. */
	UPROPERTY(BlueprintReadOnly, Category = "Content Views")
	TMap<FString, FContentViewValue> Values;
};

/** One column of a view as an agent or a header sees it. */
USTRUCT(BlueprintType)
struct CONTENTVIEWS_API FContentViewColumnInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Content Views")
	FString Name;

	/** The column's type; for a registry tag, the type most of its values have. */
	UPROPERTY(BlueprintReadOnly, Category = "Content Views")
	EContentViewValueType Type = EContentViewValueType::String;

	/** True for a column a module computes; false for a registry tag read as it is. */
	UPROPERTY(BlueprintReadOnly, Category = "Content Views")
	bool bComputed = false;

	UPROPERTY(BlueprintReadOnly, Category = "Content Views")
	FString Tooltip;
};

namespace UE::ContentViews
{
	/** The view's row class: a native class is resident; a Blueprint row class is loaded (a class, never a row). */
	CONTENTVIEWS_API UClass* ResolveRowClass(const UContentViewDefinition& View);

	/** The registry filter a view starts from: assets of the row class, and Blueprints, which IsRowOf then sorts. */
	CONTENTVIEWS_API FARFilter MakeRowFilter(const UClass& RowClass, bool bIncludeSubclasses);

	/**
	 * Whether Asset is a row of RowClass: an asset of that class (or a subclass), or a Blueprint whose parent chain
	 * reaches it. Read from registry tags only.
	 */
	CONTENTVIEWS_API bool IsRowOf(const FAssetData& Asset, const UClass& RowClass, bool bIncludeSubclasses);

	/** What is wrong with Filter, in words, or empty when it can be run. */
	CONTENTVIEWS_API FText CheckFilter(const FContentViewFilter& Filter);

	CONTENTVIEWS_API bool PassesFilter(const FAssetData& Asset, const FContentViewFilter& Filter);

	/**
	 * Whether Asset is a row of a view over RowClass with Filters: IsRowOf, not a level's external actor, and passing every
	 * filter. Both backends decide a view's rows with this, so they list the same ones.
	 */
	CONTENTVIEWS_API bool IsViewRow(const FAssetData& Asset, const UClass& RowClass, bool bIncludeSubclasses,
		TConstArrayView<FContentViewFilter> Filters);

	/** Every row of View passing its own filters and Extra, in registry order. */
	CONTENTVIEWS_API TArray<FAssetData> GatherRows(const UContentViewDefinition& View,
		TConstArrayView<FContentViewFilter> Extra = {});

	/** Sorts by one column's values; ties keep the asset name order. */
	CONTENTVIEWS_API void SortRows(TArray<FAssetData>& Rows, const FContentViewSort& Sort);

	/**
	 * The view's rows with their values: its filters and Extra applied, sorted by Sort (or the view's own when Sort has no
	 * column), at most Limit (0 for all). Reads the asset registry only.
	 */
	CONTENTVIEWS_API TArray<FContentViewRow> RunView(const UContentViewDefinition& View,
		TConstArrayView<FContentViewFilter> Extra, const FContentViewSort& Sort, int32 Limit);

	/** The view's columns with their types. A registry tag column's type is the one most of its rows hold. */
	CONTENTVIEWS_API TArray<FContentViewColumnInfo> DescribeColumns(const UContentViewDefinition& View);

	/** Whether some row of the view carries a value for Column (a registered column always does). */
	CONTENTVIEWS_API bool IsKnownColumn(const UContentViewDefinition& View, FName Column);

	/**
	 * Every view asset of every archetype (asset views, Scene Cost, ...), by name. The views this user hid
	 * (UContentViewsUserSettings) are left out unless bIncludeHidden.
	 */
	CONTENTVIEWS_API TArray<FAssetData> FindViews(bool bIncludeHidden = false);

	/** The view of any archetype named Name: its object path, asset name or display name. Loads the view asset. */
	CONTENTVIEWS_API UContentViewBase* FindViewAsset(const FString& Name);

	/** The asset view named Name, or null when there is none or Name is a view of another archetype. */
	CONTENTVIEWS_API UContentViewDefinition* FindView(const FString& Name);
}
