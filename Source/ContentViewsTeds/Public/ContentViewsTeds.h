#pragma once

#include "CoreMinimal.h"
#include "ContentViewQuery.h"

class UContentViewDefinition;

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
}
