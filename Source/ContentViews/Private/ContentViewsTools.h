#pragma once

#include "CoreMinimal.h"
#include "ContentViewDefinition.h"
#include "ContentViewQuery.h"
#include "ToolsetRegistry/ToolsetDefinition.h"
#include "ContentViewsTools.generated.h"

/** A view as list_views shows it. */
USTRUCT()
struct FContentViewSummary
{
	GENERATED_BODY()

	/** The name the view is shown under; any tool taking a view accepts it. */
	UPROPERTY()
	FString Name;

	/** The view asset's object path. */
	UPROPERTY()
	FString AssetPath;

	/** The class whose assets are its rows. */
	UPROPERTY()
	FString RowClass;

	UPROPERTY()
	TArray<FString> Columns;
};

/** A view's shape: its columns with their types, filters, sort and how many rows it has now. */
USTRUCT()
struct FContentViewDescription
{
	GENERATED_BODY()

	UPROPERTY()
	FContentViewSummary View;

	UPROPERTY()
	TArray<FContentViewColumnInfo> Columns;

	/** Conditions every row already meets. */
	UPROPERTY()
	TArray<FContentViewFilter> Filters;

	UPROPERTY()
	FContentViewSort DefaultSort;

	UPROPERTY()
	int32 RowCount = 0;
};

/**
 * Reads Content Views: data-driven tables of assets (abilities, characters, levels, any asset class) defined by view
 * assets. Lists the views, describes their typed columns, and runs one with extra filters, a sort and a limit, returning
 * rows with each asset's path and typed values, read from the asset registry without loading any asset.
 */
UCLASS(BlueprintType, Hidden)
class UContentViewsTools : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	/**
	 * Returns every view.
	 * @return Each view's name, asset path, row class and columns.
	 */
	UFUNCTION(meta = (AICallable), Category = "ContentViews")
	static TArray<FContentViewSummary> ListViews();

	/**
	 * Returns a view's columns with their types, its own filters, its default sort and its row count.
	 * @param View The view's name, asset name or object path.
	 */
	UFUNCTION(meta = (AICallable), Category = "ContentViews")
	static FContentViewDescription DescribeView(const FString& View);

	/**
	 * Runs a view: its rows that meet its own filters and Filters, sorted, as the asset path and a value per column.
	 * @param View The view's name, asset name or object path.
	 * @param Filters Extra conditions, all of which a row must meet. Empty for none.
	 * @param SortColumn The column to sort by. Empty for the view's default sort.
	 * @param bSortDescending Largest first; unset values stay last either way.
	 * @param Limit The most rows to return. 0 for all.
	 */
	UFUNCTION(meta = (AICallable), Category = "ContentViews")
	static TArray<FContentViewRow> RunView(const FString& View, const TArray<FContentViewFilter>& Filters,
		const FString& SortColumn = TEXT(""), bool bSortDescending = false, int32 Limit = 0);

	/**
	 * Returns a view's definition as data, the form a new view can be written in.
	 * @param View The view's name, asset name or object path.
	 */
	UFUNCTION(meta = (AICallable), Category = "ContentViews")
	static FContentViewSpec ExportView(const FString& View);

private:
	static UContentViewDefinition* FindViewOrRaise(const FString& View);
	static FContentViewSummary Summarise(const UContentViewDefinition& View);
};
