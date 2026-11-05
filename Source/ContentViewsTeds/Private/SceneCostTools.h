#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"
#include "SceneCostTools.generated.h"

class USceneCostView;

/** One placed mesh as RunSceneCost returns it. */
USTRUCT()
struct FSceneCostToolRow
{
	GENERATED_BODY()

	/** The mesh's object path. */
	UPROPERTY()
	FString Mesh;

	/** How many times it is placed: a static component counts once, an instanced one by its instances. */
	UPROPERTY()
	int64 Placed = 0;

	/** Triangles in one copy (Nanite triangles for a Nanite mesh). */
	UPROPERTY()
	int64 Triangles = 0;

	/** Placed x Triangles. */
	UPROPERTY()
	int64 SceneTriangles = 0;

	UPROPERTY()
	bool bNanite = false;

	/** Estimated compressed size of one copy, in bytes; 0 when unknown. */
	UPROPERTY()
	int64 EstMemory = 0;

	/** Why the row is flagged (a non-Nanite mesh over the view's budget), or empty. */
	UPROPERTY()
	FString Alert;
};

/** What a Scene Cost view counted in the level open in the editor. */
USTRUCT()
struct FSceneCostToolResult
{
	GENERATED_BODY()

	/** The view's name. */
	UPROPERTY()
	FString View;

	/** The level counted. */
	UPROPERTY()
	FString Level;

	UPROPERTY()
	int32 LoadedActors = 0;

	UPROPERTY()
	int32 Meshes = 0;

	UPROPERTY()
	int32 Components = 0;

	UPROPERTY()
	int64 Instances = 0;

	UPROPERTY()
	int64 SceneTriangles = 0;

	/** The scene triangles of meshes that are not Nanite. */
	UPROPERTY()
	int64 NonNaniteTriangles = 0;

	/** The view's non-Nanite budget, in scene triangles per mesh. */
	UPROPERTY()
	int64 TriangleBudget = 0;

	/** False while a change is still waiting to be counted: call again for the settled count. */
	UPROPERTY()
	bool bCaughtUp = false;

	/** Most scene triangles first. */
	UPROPERTY()
	TArray<FSceneCostToolRow> Rows;
};

/** One component placing a mesh, as Placements returns it. */
USTRUCT()
struct FSceneCostToolPlacement
{
	GENERATED_BODY()

	/** The actor's object path, for selecting or editing it. */
	UPROPERTY()
	FString Actor;

	/** The actor's label in the Outliner. */
	UPROPERTY()
	FString ActorLabel;

	/** The component's object path. */
	UPROPERTY()
	FString Component;

	/** Static, Instanced or Hierarchical instanced. */
	UPROPERTY()
	FString Kind;

	UPROPERTY()
	int32 Instances = 0;

	/** The World Partition grid the actor streams in, Always loaded, or - in a level without World Partition. */
	UPROPERTY()
	FString Cell;
};

/**
 * Reads Scene Cost views: the static meshes placed in the level open in the editor, how many times each is placed, the
 * triangles each puts in the scene, Nanite or not, its estimated size and a budget alert, and which actors place a mesh.
 * Needs the TEDS backend on (Project Settings > Plugins > Content Views > Backend: TEDS or Both) and an editor, not a
 * commandlet. ContentViews.ContentViewsTools.ListViews lists the views; a Scene Cost view's archetype is SceneCostView.
 */
UCLASS(BlueprintType, Hidden)
class USceneCostTools : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	/**
	 * Runs a Scene Cost view on the level open in the editor: the totals and a row per placed mesh, most scene triangles first.
	 * @param View The view's name, asset name or object path.
	 * @param Limit The most rows to return. 0 for all.
	 */
	UFUNCTION(meta = (AICallable), Category = "ContentViews")
	static FSceneCostToolResult RunSceneCost(const FString& View, int32 Limit = 0);

	/**
	 * The components placing a mesh in the level open in the editor, most instances first.
	 * @param View The Scene Cost view's name, asset name or object path.
	 * @param Mesh The mesh's object path, or its asset name when one row of the view has that name.
	 */
	UFUNCTION(meta = (AICallable), Category = "ContentViews")
	static TArray<FSceneCostToolPlacement> Placements(const FString& View, const FString& Mesh);

private:
	static USceneCostView* FindSceneViewOrRaise(const FString& View);
};
