#pragma once

#include "CoreMinimal.h"
#include "ContentViewsTeds.h"
#include "DataStorage/Handles.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class USceneCostView;

namespace UE::Editor::DataStorage
{
	class ITableViewer;
	namespace QueryStack { class IRowNode; }
}

/**
 * Scene Cost as a Content Source. The top bar names the level being counted and holds the view's budget. The table has
 * one row per placed mesh, most scene triangles first. Below it are the selected mesh's placements, with Select in Level
 * and Frame, and a status line with the totals. Every count comes from the backend; the widget only reads it.
 */
class SSceneCostView final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSceneCostView) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const FSoftObjectPath& InView);
	virtual ~SSceneCostView() override;

	/** The mesh whose placements the bottom panel lists. */
	void ShowPlacements(const FSoftObjectPath& Mesh);
	const TArray<TSharedPtr<UE::ContentViews::Teds::FScenePlacement>>& GetPlacements() const { return Placements; }

	/** What the Select in Level and Frame buttons do. */
	void SelectPlacements() const;
	void FramePlacements() const;

	FText GetTitle() const;
	FText GetStatus() const;

private:
	using FPlacementItem = TSharedPtr<UE::ContentViews::Teds::FScenePlacement>;

	USceneCostView* GetView() const;
	void HandleSelection(UE::Editor::DataStorage::RowHandle Row);
	void HandleCounted();
	FText GetPlacementsTitle() const;
	TArray<AActor*> PlacedActors() const;
	TSharedRef<ITableRow> MakePlacementRow(FPlacementItem Item, const TSharedRef<STableViewBase>& Owner);

	FSoftObjectPath ViewPath;
	FSoftObjectPath ShownMesh;
	TSharedPtr<UE::Editor::DataStorage::QueryStack::IRowNode> Rows;
	TSharedPtr<UE::Editor::DataStorage::ITableViewer> Viewer;
	TArray<FPlacementItem> Placements;
	TSharedPtr<SListView<FPlacementItem>> PlacementList;
};
