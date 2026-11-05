#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/AssetData.h"
#include "DataStorage/Handles.h"
#include "TedsQueryStackInterfaces.h"
#include "Widgets/SCompoundWidget.h"

namespace UE::Editor::DataStorage
{
	class ITableViewer;
}

/**
 * A Content View shown as a Content Source: a search box and filter bar over the view's rows, then the rows as a table
 * (one column per view column, sorted by the view's default sort) or tiles, also in that order. Double-click opens a row's
 * asset; right-click gives the legacy tab's asset menu. Rebuilds itself when its view asset changes.
 */
class SContentViewsSource final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SContentViewsSource) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const FSoftObjectPath& InView);
	virtual ~SContentViewsSource() override;

	/** The table or tile viewer showing the rows. */
	TSharedPtr<UE::Editor::DataStorage::ITableViewer> GetViewer() const { return Viewer; }

	/** The rows the viewer shows, in its order, once its stack has run. */
	TSharedPtr<UE::Editor::DataStorage::QueryStack::IRowNode> GetShownRows() const { return Shown; }

	/** The selected rows' assets. */
	TArray<FAssetData> GetSelectedAssets() const;

private:
	void Build();
	void BindRowActions();
	void ReleaseQueries();
	void HandleViewChanged(const FSoftObjectPath& Changed);

	FSoftObjectPath ViewPath;
	TArray<UE::Editor::DataStorage::QueryHandle> FilterQueries;
	TSharedPtr<UE::Editor::DataStorage::QueryStack::IRowNode> Searched;
	TSharedPtr<UE::Editor::DataStorage::QueryStack::IRowNode> Filtered;
	TSharedPtr<UE::Editor::DataStorage::QueryStack::IRowNode> Shown;
	TSharedPtr<UE::Editor::DataStorage::ITableViewer> Viewer;
};
