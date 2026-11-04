# Content Views

An Unreal Engine editor plugin for data-driven views of assets.

A **view** is an asset: which class its rows are (or a query), which columns to show, the default sort, filters, and list or tile mode. Make a new view in the editor and it appears as a new way to browse your content, with no code.

Two backends read the same view assets:

- **Legacy**: the stock Content Browser asset view (`IContentBrowserSingleton::CreateAssetPicker`) with custom columns from asset registry tags, plus frontend filters that also work in the normal Content Browser. Works today.
- **TEDS**: one Content Source per view in the Content Browser's source toolbar, built on Editor Data Storage. Experimental, like TEDS itself.

Editor-only. Nothing in this plugin reaches a cooked game.

## Status

Scaffold. Plan and stages: see the first issue on this repository.

## Using it in a project

Columns come from asset registry tags, so mark the fields you want to browse `AssetRegistrySearchable`, or emit computed ones from `GetAssetRegistryTags`. Computed columns that need code are small column providers registered by name.

Targets Unreal Engine 5.8.
