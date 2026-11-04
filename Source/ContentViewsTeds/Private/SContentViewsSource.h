#pragma once

#include "CoreMinimal.h"
#include "DataStorage/Handles.h"
#include "TedsQueryStackInterfaces.h"
#include "Widgets/SCompoundWidget.h"

/**
 * A Content View shown as a Content Source: a search box and filter bar over the view's rows, then the rows as a table
 * (one column per view column, sorted by the view's default sort) or tiles. Rebuilds itself when its view asset changes.
 */
class SContentViewsSource final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SContentViewsSource) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const FSoftObjectPath& InView);
	virtual ~SContentViewsSource() override;

private:
	void Build();
	void ReleaseQueries();
	void HandleViewChanged(const FSoftObjectPath& Changed);

	FSoftObjectPath ViewPath;
	TArray<UE::Editor::DataStorage::QueryHandle> FilterQueries;
	TSharedPtr<UE::Editor::DataStorage::QueryStack::IRowNode> Rows;
	TSharedPtr<UE::Editor::DataStorage::QueryStack::IRowNode> Searched;
	TSharedPtr<UE::Editor::DataStorage::QueryStack::IRowNode> Filtered;
};
