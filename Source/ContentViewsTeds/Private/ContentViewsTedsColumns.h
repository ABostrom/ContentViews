#pragma once

#include "DataStorage/CommonTypes.h"
#include "UObject/SoftObjectPath.h"
#include "ContentViewsTedsColumns.generated.h"

class AActor;
class UWorld;

/**
 * On a static mesh component's TEDS row: the mesh it places and how many times (an instanced component's instances).
 * Written from the component when its row syncs from the world; Scene Cost counts these.
 */
USTRUCT(meta = (DisplayName = "Mesh placement"))
struct FContentViewPlacementColumn : public FEditorDataStorageColumn
{
	GENERATED_BODY()

	UPROPERTY()
	FSoftObjectPath Mesh;

	UPROPERTY()
	int32 Instances = 0;

	UPROPERTY()
	bool bInstanced = false;

	UPROPERTY()
	bool bHierarchical = false;

	TWeakObjectPtr<UWorld> World;
	TWeakObjectPtr<AActor> Owner;
};

/** On every row of the Scene Cost table: one placed mesh of one Scene Cost view. */
USTRUCT()
struct FContentViewSceneMeshTag : public FEditorDataStorageTag
{
	GENERATED_BODY()
};

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

/**
 * A colour column of a view, one type per column name: the header reads the column's own name, and a view can show more
 * than one (the stock FSlateColorColumn allows one per row and reads "Color").
 */
USTRUCT(meta = (EditorDataStorage_DynamicColumnTemplate))
struct FContentViewColourColumn : public FEditorDataStorageColumn
{
	GENERATED_BODY()

	UPROPERTY()
	FLinearColor Colour = FLinearColor::White;

	/** #RRGGBB, so the column sorts and searches by its value. */
	UPROPERTY(meta = (Sortable, Searchable))
	FString Hex;
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
