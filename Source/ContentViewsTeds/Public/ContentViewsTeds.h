#pragma once

#include "CoreMinimal.h"
#include "ContentViewQuery.h"

class AActor;
class UContentViewDefinition;
class USceneCostView;
class UStaticMeshComponent;

namespace UE::ContentViews::Teds
{
	/** What Scene Cost counted in the level open in the editor. */
	struct FSceneTotals
	{
		FString Level;
		int32 LoadedActors = 0;
		int32 Meshes = 0;
		int32 Components = 0;
		int64 Instances = 0;
		int64 SceneTriangles = 0;
		/** The scene triangles of meshes that are not Nanite. */
		int64 NonNaniteTriangles = 0;
	};

	/** One component placing a mesh. */
	struct FScenePlacement
	{
		TWeakObjectPtr<AActor> Actor;
		TWeakObjectPtr<UStaticMeshComponent> Component;
		FString ActorLabel;
		FString ComponentName;
		/** Static, Instanced or Hierarchical instanced. */
		FString Kind;
		/** The World Partition grid the actor streams in, Always loaded, or - in a level without World Partition. */
		FString Cell;
		int32 Instances = 0;
	};

	/** A Scene Cost row read back from TEDS. */
	struct FSceneCostRow
	{
		FString Mesh;
		int64 Placed = 0;
		int64 Triangles = 0;
		int64 SceneTriangles = 0;
		bool bNanite = false;
		/** Estimated compressed size of one copy of the mesh, in bytes; 0 when the registry has no estimate. */
		int64 Memory = 0;
		FString Alert;
	};
}

/**
 * What the TEDS backend has built, read back for tests and tools. Every TEDS call of the plugin is in this module; these
 * functions answer in the legacy backend's own types.
 */
namespace UE::ContentViews::Teds
{
	/** Whether TEDS runs (an editor, not a commandlet) and the backend setting includes it. */
	CONTENTVIEWSTEDS_API bool IsActive();

	/** Whether every asset and view change seen so far has reached the rows. */
	CONTENTVIEWSTEDS_API bool IsCaughtUp();

	/** Whether View has a Content Source in the Content Browser's source bar. */
	CONTENTVIEWSTEDS_API bool HasContentSource(const UContentViewDefinition& View);

	/** View's rows as TEDS holds them, before any filter, with each column's value read back from its TEDS column. */
	CONTENTVIEWSTEDS_API TArray<FContentViewRow> ReadRows(const UContentViewDefinition& View);

	/** View's rows' asset paths ordered by Column through TEDS's own generated sorter, as the table's header sorts. */
	CONTENTVIEWSTEDS_API TArray<FString> SortRows(const UContentViewDefinition& View, FName Column, bool bDescending);

	/** View's rows' asset paths that pass Filter as the source's filter bar runs it. */
	CONTENTVIEWSTEDS_API TArray<FString> FilterRows(const UContentViewDefinition& View, const FContentViewFilter& Filter);

	/** The message of each alert a row of View carries, by asset path. */
	CONTENTVIEWSTEDS_API TMap<FString, FString> ReadAlerts(const UContentViewDefinition& View);

	/** Whether every placement change seen so far has been counted. */
	CONTENTVIEWSTEDS_API bool IsSceneCaughtUp();

	/** The last count's totals. */
	CONTENTVIEWSTEDS_API FSceneTotals ReadSceneTotals();

	/** View's rows as TEDS holds them, most scene triangles first. */
	CONTENTVIEWSTEDS_API TArray<FSceneCostRow> ReadSceneRows(const USceneCostView& View);

	/** The components placing Mesh in the open level, most instances first. */
	CONTENTVIEWSTEDS_API TArray<FScenePlacement> ReadPlacements(const FSoftObjectPath& Mesh);
}
