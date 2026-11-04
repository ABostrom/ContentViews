#pragma once

#include "CoreMinimal.h"
#include "ContentViewValue.h"

struct FAssetData;

/** How a backend draws a column's values. */
enum class EContentViewColumnShows : uint8
{
	/** The value's words. */
	Text,

	/** A colour chip: the value is #RRGGBB, or a comma list of them (the first is drawn). */
	Colour,
};

/**
 * A column a view can list by name. Reads its value from the asset's registry data only, never by loading the asset.
 * A view's column with no registered provider reads the registry tag of that name.
 */
class CONTENTVIEWS_API IContentViewColumn
{
public:
	virtual ~IContentViewColumn() = default;

	/** The type of every value, or unset when it varies by value (a raw registry tag). */
	virtual TOptional<EContentViewValueType> GetType() const { return {}; }

	virtual FContentViewValue GetValue(const FAssetData& Asset) const = 0;

	/** The column header's tooltip. */
	virtual FText GetTooltip() const { return FText::GetEmpty(); }

	virtual EContentViewColumnShows GetShows() const { return EContentViewColumnShows::Text; }
};

namespace UE::ContentViews
{
	/** Adds a computed column under Name; a later registration under the same name replaces it. */
	CONTENTVIEWS_API void RegisterColumn(FName Name, TSharedRef<const IContentViewColumn> Column);

	CONTENTVIEWS_API void UnregisterColumn(FName Name);

	/** Whether Name is a computed column rather than a registry tag. */
	CONTENTVIEWS_API bool IsColumnRegistered(FName Name);

	/** The registered column, else one that reads the registry tag Name. */
	CONTENTVIEWS_API TSharedRef<const IContentViewColumn> GetColumn(FName Name);

	/** A column from a function, for computed columns registered in project code. */
	CONTENTVIEWS_API TSharedRef<const IContentViewColumn> MakeColumn(TOptional<EContentViewValueType> Type,
		TFunction<FContentViewValue(const FAssetData&)> Read, FText Tooltip = FText::GetEmpty(),
		EContentViewColumnShows Shows = EContentViewColumnShows::Text);

	/** The registry tag Tag of Asset as a value (FContentViewValue::FromTagString), localised text as its words. */
	CONTENTVIEWS_API FContentViewValue ReadTag(const FAssetData& Asset, FName Tag);

	/** The paths in a comma list written by a GetAssetRegistryTags (the form a list tag takes in this plugin's views). */
	CONTENTVIEWS_API TArray<FString> SplitList(const FString& Raw);

	/** Joins paths or words into the comma list SplitList reads. */
	CONTENTVIEWS_API FString JoinList(TConstArrayView<FString> Items);
}
