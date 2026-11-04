#pragma once

#include "DataStorage/CommonTypes.h"
#include "ContentViewsTedsColumns.generated.h"

/** On every asset row that is a row of one view; one tag type per view, its identifier the view's object path. */
USTRUCT(meta = (EditorDataStorage_DynamicColumnTemplate))
struct FContentViewRowTag : public FEditorDataStorageTag
{
	GENERATED_BODY()
};

/** A number column of a view, one type per column name, so it sorts as a number. */
USTRUCT(meta = (EditorDataStorage_DynamicColumnTemplate))
struct FContentViewNumberColumn : public FEditorDataStorageColumn
{
	GENERATED_BODY()

	UPROPERTY(meta = (Sortable))
	double Value = 0.0;

	UPROPERTY(meta = (Searchable))
	FString Display;
};

/** A computed text column of a view: the value (paths for references) and the words it shows. */
USTRUCT(meta = (EditorDataStorage_DynamicColumnTemplate))
struct FContentViewStringColumn : public FEditorDataStorageColumn
{
	GENERATED_BODY()

	UPROPERTY(meta = (Sortable))
	FString Value;

	UPROPERTY(meta = (Searchable))
	FString Display;
};
