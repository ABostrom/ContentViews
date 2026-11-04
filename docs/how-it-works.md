# How Content Views works

This is the engineer's tour of the plugin: what a view asset holds, where column values come from, how the legacy
backend draws a view with the stock Content Browser widgets, why nothing is loaded, what the MCP toolset returns, and
what the planned TEDS backend will change. Everything here describes code in this repository unless it says *planned*.

## The view asset

A view is a `UContentViewDefinition` (`Source/ContentViews/Public/ContentViewDefinition.h`), a `UPrimaryDataAsset`
with its own primary asset type, `ContentView`. The module registers that type with the Asset Manager as editor-only,
scanning `/Game`, so a cook never takes a view.

| Field | What it does |
|---|---|
| `DisplayName` | The name in the Content Views tab, the filter menu and the MCP tools. |
| `Icon` | A brush name in the editor style set, such as `ClassIcon.Character`. |
| `RowClass` | The class whose assets are the rows (a soft class, so a Blueprint class works too). |
| `bIncludeSubclasses` | Whether assets of subclasses and Blueprint children are rows. On by default. |
| `Columns` | Column names, left to right. |
| `DefaultSort` | A column and a direction. None sorts by name. |
| `Filters` | `FContentViewFilter` rows: column, operator (`=`, `!=`, `>=`, `<=`, `contains`, `has tag`), value. A row must meet all of them. |
| `Mode` | List (a column view) or Tiles (thumbnails). |

`DisplayName` and `Icon` are themselves `AssetRegistrySearchable`, so the tab's list of views loads nothing either.

`IsDataValid` refuses an empty or missing row class, an empty column list, a column that is neither a registered
provider nor a tag any row carries, a filter that cannot run (no column, no value, a word where `>=` needs a number) and
a default sort on a column the view does not show. The same filter check, `UE::ContentViews::CheckFilter`, is what the
MCP tool refuses a bad filter with, so the editor and an agent hear the same reason.

`ToSpec()` returns the definition as a plain `FContentViewSpec` struct and `ExportJson()` writes that as JSON. Creating
a view from JSON is planned, not built.

## Which assets are rows

`UE::ContentViews::GatherRows` (`ContentViewQuery.cpp`) asks the asset registry for assets of the row class, plus
Blueprint assets when subclasses are included, then keeps a Blueprint only if its generated class descends from the row
class:

```cpp
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
```

`IsRowOf` reads a Blueprint's `GeneratedClass` tag and walks its parents: a loaded class answers with `IsChildOf`, and
an unloaded Blueprint parent answers through its own asset's `ParentClass` tag. No class or asset is loaded to decide.

## Columns

A column is an `IContentViewColumn` (`Public/ContentViewColumns.h`): a function from `FAssetData` to an
`FContentViewValue`, which holds a type (string, number or bool), the value, and optionally the words to show.

**The default provider** is the registry tag of the column's name. `UE::ContentViews::ReadTag` turns the tag's text
into a value: localised text gives its words, text that parses as a number becomes a number, `True` and `False` become a
bool, and everything else is a string. An object path, or a comma list of them, keeps the full path as its value (so an
agent can act on it) and shows the object names. Four columns are built in: `Name`, `Path`, `Plugin` (the content root)
and `Class`.

So the cheapest column is a field marked `AssetRegistrySearchable`. A value that has to be worked out goes in the
class's `GetAssetRegistryTags`, which runs when the asset is saved:

```cpp
#if WITH_EDITOR
void UMyTeamDefinition::GetAssetRegistryTags(FAssetRegistryTagsContext Context) const
{
	Super::GetAssetRegistryTags(Context);
	Context.AddTag(FAssetRegistryTag(TEXT("Players"), FString::FromInt(Players.Num()), FAssetRegistryTag::TT_Numerical));
}
#endif
```

**A computed provider** is for a value no single asset's tags hold: an average of several tags, or a reverse lookup
across assets. Register it by name from an editor module's `StartupModule`, and unregister it in `ShutdownModule`:

```cpp
#include "ContentViewColumns.h"

UE::ContentViews::RegisterColumn(TEXT("Overall"), UE::ContentViews::MakeColumn(EContentViewValueType::Number,
	[](const FAssetData& Man)
	{
		double Sum = 0.0;
		for (const TCHAR* Rating : {TEXT("Pace"), TEXT("Strength"), TEXT("Vision")})
		{
			const FContentViewValue Value = UE::ContentViews::ReadTag(Man, Rating);
			if (!Value.bIsSet || Value.Type != EContentViewValueType::Number)
			{
				return FContentViewValue();
			}
			Sum += Value.Number;
		}
		return FContentViewValue::OfNumber(Sum / 3.0);
	},
	LOCTEXT("Overall", "The mean of his ratings.")));
```

A provider reads `FAssetData` only. If it needs another asset's facts (which team lists this player), it asks the
registry for those assets and reads their tags too. Any view can then list `Overall` like any other column.

## The legacy backend

The Content Views tab (`SContentViewsTab.cpp`) is a nomad tab with two panes. The left is a list of view assets from
the registry. The right is built from the chosen view with `IContentBrowserSingleton::CreateAssetPicker`, the same
widget the Content Browser's asset view is made of, so it looks and behaves like the Content Browser:

- `FAssetPickerConfig::Filter` is `MakeRowFilter` for the row class.
- `OnShouldFilterAsset` does the rest per asset: `IsRowOf`, then every filter of the view.
- `CustomColumns` gets one `FAssetViewCustomColumn` per view column. Its data delegate gives the sortable text (numbers
  as numbers, with `TT_Numerical` so the column sorts numerically), its display delegate gives the words.
- Every other column is hidden: `HiddenColumnNames` for the fixed ones (class, path, size, revision control) and
  `OnAssetTagWantsToBeDisplayed` returning false for the tag columns the asset view would add on its own.
- `InitialAssetViewType` is the column view for List and the tile view for Tiles.
- The picker has no option for its starting sort, so the tab finds the picker's `SAssetView` in its widget tree and calls
  `SetSortParameters` with the view's default sort.
- Double-click opens the asset's editor. Right-click builds the Content Browser's registered
  `ContentBrowser.AssetContextMenu.<Class>` menu for the selection, with Edit, Browse to Asset and Copy Reference in
  front. The Content Browser's own common section (rename, delete, duplicate) is private to it, so it is not there.

**Frontend filters.** `UContentViewsFilterExtension` is a `UContentBrowserFrontEndFilterExtension`. When a Content
Browser builds its filter list, it adds one `FFrontendFilter` per view under a *Content Views* category. The filter
passes an item if it is a row of the view and meets the view's filters, so any Content Browser can show a view's
selection inside its normal folders.

## Why nothing loads

Every value comes from `FAssetData`: the registry's record of an asset, read from the package header when the editor
scans content. Rows, columns, sorting, filtering and the MCP tools all work from it. The only objects loaded are the
view assets themselves (they are small) and, when a view's row class is a Blueprint, that one class. This is why a view
over hundreds of Blueprints opens at once, and why an agent can read a table of every ability without loading any.

The cost is that a value must already be a tag. See the limits below.

## The MCP toolset

`UContentViewsTools` (`ContentViewsTools.h`) is a `UToolsetDefinition` registered with the engine's `ToolsetRegistry`
once the engine has started, the same way the engine's own editor toolsets register. The editor's MCP server then
exposes it as `ContentViews.ContentViewsTools`:

| Tool | Arguments | Returns |
|---|---|---|
| `ListViews` | none | each view's name, asset path, row class and columns |
| `DescribeView` | `view` | its columns with types, its filters, its default sort and its row count |
| `RunView` | `view`, `filters`, `sortColumn`, `bSortDescending`, `limit` | rows: `assetPath` and `values` |
| `ExportView` | `view` | the view's definition as data |

A view is named by display name, asset name or object path. Values arrive typed: an `FContentViewValueConverter`
registered with the toolset registry writes each `FContentViewValue` as a JSON number, bool, string, or `null` when the
asset has no value.

Every argument is required over MCP, even where the C++ function has a default: pass `[]`, `""`, `false` and `0` for
no filters, the default sort and all rows.

A `ListViews` call (no arguments) answers, in a project with an Arenas view (one entry shown):

```json
[{"name": "Arenas", "assetPath": "/Game/Editor/ContentViews/CV_Arenas.CV_Arenas",
  "rowClass": "/Script/ArenaCore.ArenaDefinition", "columns": ["Name", "Plugin", "Level", "Looks", "District"]}]
```

A `RunView` call:

```json
{"view": "Arenas", "filters": [], "sortColumn": "", "bSortDescending": false, "limit": 0}
```

answers with one row per arena (two shown). `Looks` is a number, an empty reference is `null`, and a reference keeps
its full path so an agent can open or edit it:

```json
[{"assetPath": "/Arena_Docks/Data/DA_Arena_Docks.DA_Arena_Docks",
  "values": {"Name": "DA_Arena_Docks", "Plugin": "Arena_Docks", "Level": "/Arena_Docks/Maps/L_Arena_Docks.L_Arena_Docks",
             "Looks": 2, "District": "/Script/ArenaCore.ArenaDistrictRules'/Kit_Docks/Data/DA_District_Docks.DA_District_Docks'"}},
 {"assetPath": "/Arena_Rooftop/Data/DA_Arena_Rooftop.DA_Arena_Rooftop",
  "values": {"Name": "DA_Arena_Rooftop", "Plugin": "Arena_Rooftop", "Level": "/Arena_Rooftop/Maps/L_Arena_Rooftop.L_Arena_Rooftop",
             "Looks": 2, "District": null}}]
```

A bad filter, an unknown view or a column no row carries is refused with a script error naming it.

## The TEDS backend (planned)

Stage two is not built. The plan (see the repository's first issue): one generic TEDS `IContentSource` that registers a
Content Source per view asset, registry tags as dynamic string columns with typed columns where a provider exists,
computed columns as gated processors rather than per-frame work over every row, and filters compiled to `FTedsFilter`.
A project setting would pick the backend. TEDS is experimental and its API moves between engine versions, so every TEDS
call is meant to sit behind one module boundary, with an automation test per view so an engine upgrade that breaks it
fails a gate.

**Why both backends read one asset.** A view is a description of a table, not of a widget. Keeping rows, columns,
sort and filters in one data asset means a view made today keeps working when the TEDS backend lands, and the MCP
tools, the frontend filters and either backend all agree on what a view is.

## Limits

- **Tags need a resave.** A tag exists only once an asset has been saved by code that writes it. After adding
  `AssetRegistrySearchable` or a `GetAssetRegistryTags` entry, resave the assets once (a Blueprint's tags come from its
  class default, so resave the Blueprint).
- **Tags are text.** The registry stores every tag as text. The default provider types a value by reading it (numeric
  text becomes a number), and a column's type in `DescribeView` is the type most of its rows hold. A provider that
  declares a type always returns that type.
- **Editor only.** The module is `Type: Editor` with `TargetAllowList: ["Editor"]`. Registry tags written in
  `GetAssetRegistryTags` should sit under `WITH_EDITOR` in runtime classes.
- **A computed column shadows a tag.** A registered column answers for its name in every view, so a registry tag of the
  same name can no longer be read as a column. Name computed columns for what they compute.
- **Tiles show names.** In Tiles mode the asset view shows each row's thumbnail and name; the other columns show in List.
- **Stock menu, not all of it.** The right-click menu has the Content Browser's registered entries and three of its
  own, not the Content Browser's private common section.
