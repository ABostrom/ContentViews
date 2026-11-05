#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/AssetData.h"
#include "ContentViewAssetActions.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class SBox;
class UContentViewBase;

/**
 * The Content Views tab: every view asset of every archetype on the left; on the right the chosen view, an asset view as
 * the stock Content Browser asset view (its columns, sort, filters and list or tiles), another archetype as the widget
 * its module registered (MakeViewWidget). Reads the asset registry; rows are never loaded to be listed.
 */
class SContentViewsTab : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SContentViewsTab) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SContentViewsTab() override;

	/** Shows View on the right and selects it on the left. */
	void ShowView(const UContentViewBase& View);

	/** The view assets the list on the left shows, in order. */
	TArray<FSoftObjectPath> GetListedViews() const;

private:
	using FViewItem = TSharedPtr<FAssetData>;

	void RefreshViewList();
	void RebuildPicker();
	TSharedRef<ITableRow> MakeViewRow(FViewItem Item, const TSharedRef<STableViewBase>& Owner);
	void HandleViewSelected(FViewItem Item, ESelectInfo::Type SelectInfo);
	void HandleAssetsChanged(const FAssetData& Asset);
	void HandleAssetRenamed(const FAssetData& Asset, const FString& OldPath);
	void HandleObjectChanged(UObject* Object, struct FPropertyChangedEvent& Event);

	TArray<FViewItem> ViewItems;
	TSharedPtr<SListView<FViewItem>> ViewList;
	TSharedPtr<SBox> PickerHost;
	TWeakObjectPtr<UContentViewBase> Shown;
};
