#include "SSceneCostView.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Columns/SlateDelegateColumns.h"
#include "Components/StaticMeshComponent.h"
#include "ContentViewAssetActions.h"
#include "ContentViewsTedsBackend.h"
#include "ContentViewsTedsColumns.h"
#include "ContentViewsTedsFactory.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "Elements/Framework/TypedElementMetaData.h"
#include "Elements/Interfaces/TypedElementDataStorageInterface.h"
#include "Elements/Interfaces/TypedElementDataStorageUiInterface.h"
#include "GameFramework/Actor.h"
#include "SceneCostView.h"
#include "ScopedTransaction.h"
#include "TedsQueryNode.h"
#include "TedsRowQueryResultsNode.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Styling/AppStyle.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/STedsTableViewer.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SHeaderRow.h"

#define LOCTEXT_NAMESPACE "ContentViewsTeds"

using namespace UE::Editor::DataStorage;
using UE::ContentViews::Teds::FBackend;
using UE::ContentViews::Teds::FScenePlacement;
using UE::ContentViews::Teds::FSceneTotals;

namespace
{
	const FName PlacementActorColumn(TEXT("Actor"));
	const FName PlacementComponentColumn(TEXT("Component"));
	const FName PlacementKindColumn(TEXT("Kind"));
	const FName PlacementInstancesColumn(TEXT("Instances"));
	const FName PlacementCellColumn(TEXT("Cell"));

	class SSceneCostPlacementRow final : public SMultiColumnTableRow<TSharedPtr<FScenePlacement>>
	{
	public:
		SLATE_BEGIN_ARGS(SSceneCostPlacementRow) {}
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& Owner, TSharedPtr<FScenePlacement> InItem)
		{
			Item = InItem;
			SMultiColumnTableRow::Construct(FSuperRowType::FArguments(), Owner);
		}

		virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& Column) override
		{
			FString Text;
			if (Column == PlacementActorColumn) { Text = Item->ActorLabel; }
			else if (Column == PlacementComponentColumn) { Text = Item->ComponentName; }
			else if (Column == PlacementKindColumn) { Text = Item->Kind; }
			else if (Column == PlacementInstancesColumn) { Text = FText::AsNumber(Item->Instances).ToString(); }
			else if (Column == PlacementCellColumn) { Text = Item->Cell; }
			return SNew(STextBlock).Text(FText::FromString(Text)).ToolTipText(FText::FromString(Text)).Margin(FMargin(4.f, 2.f));
		}

	private:
		TSharedPtr<FScenePlacement> Item;
	};

	/** 12.3M, 45.6K: the totals line's short numbers. */
	FText Short(int64 Value)
	{
		FNumberFormattingOptions One;
		One.SetMaximumFractionalDigits(1);
		if (Value >= 1000000)
		{
			return FText::Format(LOCTEXT("Millions", "{0}M"), FText::AsNumber(Value / 1000000.0, &One));
		}
		if (Value >= 1000)
		{
			return FText::Format(LOCTEXT("Thousands", "{0}K"), FText::AsNumber(Value / 1000.0, &One));
		}
		return FText::AsNumber(Value);
	}
}

void SSceneCostView::Construct(const FArguments& InArgs, const FSoftObjectPath& InView)
{
	ViewPath = InView;
	FBackend& Backend = FBackend::Get();
	ICoreProvider* Storage = Backend.GetStorage();
	const UE::ContentViews::Teds::FSceneViewState* State = Backend.FindSceneView(ViewPath);
	if (!Storage || !State)
	{
		ChildSlot[SNew(STextBlock).Text(LOCTEXT("NoSceneView", "This view's asset is gone."))];
		return;
	}
	Backend.OnSceneCounted().AddSP(this, &SSceneCostView::HandleCounted);

	Rows = MakeShared<QueryStack::FRowQueryResultsNode>(*Storage, MakeShared<QueryStack::FQueryNode>(*Storage, State->MakeRowQuery()),
		QueryStack::FRowQueryResultsNode::ESyncActions::RefreshOnUpdate);
	const UE::ContentViews::Teds::FColumnSlot* SortSlot = Backend.FindSceneSlot(FBackend::SceneTrianglesColumn);
	const TSharedRef<FColumnMetaData> SortMeta = MakeShared<FColumnMetaData>(SortSlot->Type, FColumnMetaData::EFlags::None);
	SortMeta->AddOrSetMutableData("bColumnInitialSortIsAscending", false);
	const TSharedRef<STedsTableViewer> Table = SNew(STedsTableViewer)
		.TableViewerIdentifier(FName(*(TEXT("ContentViews.") + ViewPath.GetAssetName())))
		.QueryStack(Rows)
		.Columns(Backend.SceneShownColumns())
		.CellWidgetPurpose(IUiProvider::FPurposeInfo("ContentBrowser", "RowLabel", NAME_None).GeneratePurposeID())
		.HeaderWidgetPurpose(UE::ContentViews::Teds::HeaderPurpose())
		.PrimaryColumn(SortSlot->Type)
		.ColumnMetaData(TArray<TSharedPtr<FColumnMetaData>>{SortMeta})
		.ListSelectionMode(ESelectionMode::Single)
		.OnSelectionChanged_Lambda([this](RowHandle Row, ESelectInfo::Type) { HandleSelection(Row); })
		.EmptyRowsMessage(LOCTEXT("NoMeshes", "No static meshes are placed in this level."));
	Viewer = Table;

	// A mesh row opens its mesh on double-click and gives the asset menu on right-click, as an asset view's rows do.
	const auto MeshOf = [Storage](RowHandle Row)
	{
		const FContentViewStringColumn* Mesh = Storage->GetColumn<FContentViewStringColumn>(Row, FBackend::SceneMeshColumn);
		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(AssetRegistryConstants::ModuleName).Get();
		return Mesh ? Registry.GetAssetByObjectPath(FSoftObjectPath(Mesh->Value)) : FAssetData();
	};
	const RowHandle WidgetRow = Table->GetWidgetRowHandle();
	if (FWidgetDoubleClickedColumn* DoubleClick = Storage->GetColumn<FWidgetDoubleClickedColumn>(WidgetRow))
	{
		DoubleClick->OnMouseButtonDoubleClick = MakeShared<FWidgetDoubleClickedColumn::FOnMouseButtonDoubleClick>();
		DoubleClick->OnMouseButtonDoubleClick->BindLambda([MeshOf](FTedsRowHandle Row)
		{
			if (const FAssetData Mesh = MeshOf(Row); Mesh.IsValid())
			{
				UE::ContentViews::OpenAssets({Mesh});
			}
		});
	}
	if (FWidgetContextMenuColumn* Menu = Storage->GetColumn<FWidgetContextMenuColumn>(WidgetRow))
	{
		TWeakPtr<UE::Editor::DataStorage::ITableViewer> WeakViewer = Viewer;
		Menu->OnContextMenuOpening = MakeShared<FOnContextMenuOpening>();
		Menu->OnContextMenuOpening->BindLambda([WeakViewer, MeshOf]() -> TSharedPtr<SWidget>
		{
			TArray<FAssetData> Meshes;
			if (const TSharedPtr<UE::Editor::DataStorage::ITableViewer> Pinned = WeakViewer.Pin())
			{
				Pinned->ForEachSelectedRow([&Meshes, &MeshOf](RowHandle Row)
				{
					if (const FAssetData Mesh = MeshOf(Row); Mesh.IsValid())
					{
						Meshes.Add(Mesh);
					}
				});
			}
			return UE::ContentViews::MakeAssetContextMenu(Meshes);
		});
	}

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(8.f, 6.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(this, &SSceneCostView::GetTitle).Font(FAppStyle::GetFontStyle("NormalFontBold"))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12.f, 0.f, 6.f, 0.f)
			[
				SNew(STextBlock).Text(LOCTEXT("Budget", "Non-Nanite budget (scene triangles)"))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(140.f)
				[
					SNew(SNumericEntryBox<int64>)
					.MinValue(0)
					.Value_Lambda([this]() -> TOptional<int64>
					{
						const USceneCostView* View = GetView();
						return View ? TOptional<int64>(View->TriangleBudget) : TOptional<int64>();
					})
					.OnValueCommitted_Lambda([this](int64 Value, ETextCommit::Type)
					{
						if (USceneCostView* View = GetView(); View && View->TriangleBudget != Value)
						{
							const FScopedTransaction Transaction(LOCTEXT("SetBudget", "Set Scene Cost Budget"));
							View->Modify();
							View->TriangleBudget = Value;
							FProperty* Budget = USceneCostView::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(USceneCostView, TriangleBudget));
							FPropertyChangedEvent Changed(Budget, EPropertyChangeType::ValueSet);
							View->PostEditChangeProperty(Changed);
						}
					})
				]
			]
		]
		+ SVerticalBox::Slot().FillHeight(1.f)
		[
			SNew(SSplitter).Orientation(Orient_Vertical)
			+ SSplitter::Slot().Value(0.65f)
			[
				Table
			]
			+ SSplitter::Slot().Value(0.35f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(8.f, 6.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text(this, &SSceneCostView::GetPlacementsTitle)
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(4.f, 0.f)
					[
						SNew(SButton).Text(LOCTEXT("SelectInLevel", "Select in Level"))
						.IsEnabled_Lambda([this]() { return !Placements.IsEmpty(); })
						.OnClicked_Lambda([this]() { SelectPlacements(); return FReply::Handled(); })
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(4.f, 0.f)
					[
						SNew(SButton).Text(LOCTEXT("Frame", "Frame"))
						.IsEnabled_Lambda([this]() { return !Placements.IsEmpty(); })
						.OnClicked_Lambda([this]() { FramePlacements(); return FReply::Handled(); })
					]
				]
				+ SVerticalBox::Slot().FillHeight(1.f)
				[
					SAssignNew(PlacementList, SListView<FPlacementItem>)
					.ListItemsSource(&Placements)
					.SelectionMode(ESelectionMode::Multi)
					.OnGenerateRow(this, &SSceneCostView::MakePlacementRow)
					.HeaderRow(SNew(SHeaderRow)
						+ SHeaderRow::Column(PlacementActorColumn).DefaultLabel(LOCTEXT("Actor", "Actor")).FillWidth(3.f)
						+ SHeaderRow::Column(PlacementComponentColumn).DefaultLabel(LOCTEXT("Component", "Component")).FillWidth(2.f)
						+ SHeaderRow::Column(PlacementKindColumn).DefaultLabel(LOCTEXT("Kind", "Kind")).FillWidth(1.5f)
						+ SHeaderRow::Column(PlacementInstancesColumn).DefaultLabel(LOCTEXT("Instances", "Instances")).FillWidth(1.f)
						+ SHeaderRow::Column(PlacementCellColumn).DefaultLabel(LOCTEXT("Cell", "Cell")).FillWidth(1.5f))
				]
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(8.f, 4.f)
		[
			SNew(STextBlock).Text(this, &SSceneCostView::GetStatus)
		]
	];
	HandleCounted();
}

SSceneCostView::~SSceneCostView()
{
	FBackend::Get().OnSceneCounted().RemoveAll(this);
}

USceneCostView* SSceneCostView::GetView() const
{
	const UE::ContentViews::Teds::FSceneViewState* State = FBackend::Get().FindSceneView(ViewPath);
	return State ? State->View.Get() : nullptr;
}

FText SSceneCostView::GetTitle() const
{
	const FSceneTotals& Totals = FBackend::Get().GetSceneTotals();
	const USceneCostView* View = GetView();
	return FText::Format(LOCTEXT("Title", "{0} > {1} (loaded: {2} actors)"), View ? View->GetViewName() : FText::GetEmpty(),
		FText::FromString(Totals.Level.IsEmpty() ? TEXT("no level") : Totals.Level), FText::AsNumber(Totals.LoadedActors));
}

FText SSceneCostView::GetStatus() const
{
	const FSceneTotals& Totals = FBackend::Get().GetSceneTotals();
	const double Share = Totals.SceneTriangles > 0 ? 100.0 * Totals.NonNaniteTriangles / Totals.SceneTriangles : 0.0;
	FNumberFormattingOptions One;
	One.SetMaximumFractionalDigits(1);
	return FText::Format(LOCTEXT("Status", "{0} meshes . {1} instances in {2} components . {3} scene triangles . {4}% not Nanite"),
		FText::AsNumber(Totals.Meshes), FText::AsNumber(Totals.Instances), FText::AsNumber(Totals.Components),
		Short(Totals.SceneTriangles), FText::AsNumber(Share, &One));
}

FText SSceneCostView::GetPlacementsTitle() const
{
	if (ShownMesh.IsNull())
	{
		return LOCTEXT("PickAMesh", "Select a mesh to list its placements.");
	}
	int64 Instances = 0;
	for (const FPlacementItem& Item : Placements)
	{
		Instances += Item->Instances;
	}
	return FText::Format(LOCTEXT("PlacementsOf", "Placements of {0} . {1} instances in {2} components"),
		FText::FromString(ShownMesh.GetAssetName()), FText::AsNumber(Instances), FText::AsNumber(Placements.Num()));
}

void SSceneCostView::HandleSelection(RowHandle Row)
{
	ICoreProvider* Storage = FBackend::Get().GetStorage();
	const FContentViewStringColumn* Mesh = Storage ? Storage->GetColumn<FContentViewStringColumn>(Row, FBackend::SceneMeshColumn) : nullptr;
	ShowPlacements(Mesh ? FSoftObjectPath(Mesh->Value) : FSoftObjectPath());
}

void SSceneCostView::ShowPlacements(const FSoftObjectPath& Mesh)
{
	ShownMesh = Mesh;
	Placements.Reset();
	if (!Mesh.IsNull())
	{
		for (FScenePlacement& Placement : FBackend::Get().PlacementsOf(Mesh))
		{
			Placements.Add(MakeShared<FScenePlacement>(MoveTemp(Placement)));
		}
	}
	if (PlacementList)
	{
		PlacementList->RequestListRefresh();
	}
}

void SSceneCostView::HandleCounted()
{
	if (!ShownMesh.IsNull())
	{
		ShowPlacements(ShownMesh);
		return;
	}
	// Nothing picked yet: the costliest mesh, so the panel says something the moment the view opens.
	const USceneCostView* View = GetView();
	const TArray<UE::ContentViews::Teds::FSceneCostRow> Counted = View ? UE::ContentViews::Teds::ReadSceneRows(*View) : TArray<UE::ContentViews::Teds::FSceneCostRow>();
	const UE::ContentViews::Teds::FSceneViewState* State = FBackend::Get().FindSceneView(ViewPath);
	if (!Counted.IsEmpty() && State && Viewer)
	{
		const FSoftObjectPath Top(Counted[0].Mesh);
		if (const RowHandle* Row = State->Rows.Find(Top))
		{
			Viewer->SetSelection(*Row, true, ESelectInfo::Direct);
		}
		ShowPlacements(Top);
	}
}

TArray<AActor*> SSceneCostView::PlacedActors() const
{
	TArray<AActor*> Actors;
	for (const FPlacementItem& Item : Placements)
	{
		if (AActor* Actor = Item->Actor.Get())
		{
			Actors.AddUnique(Actor);
		}
	}
	return Actors;
}

void SSceneCostView::SelectPlacements() const
{
	const TArray<AActor*> Actors = PlacedActors();
	if (!GEditor || Actors.IsEmpty())
	{
		return;
	}
	const FScopedTransaction Transaction(LOCTEXT("SelectPlacements", "Select Placements"));
	GEditor->SelectNone(/*bNoteSelectionChange*/ false, /*bDeselectBSPSurfs*/ true);
	for (AActor* Actor : Actors)
	{
		GEditor->SelectActor(Actor, /*bInSelected*/ true, /*bNotify*/ false);
	}
	GEditor->NoteSelectionChange();
}

void SSceneCostView::FramePlacements() const
{
	const TArray<AActor*> Actors = PlacedActors();
	if (GEditor && !Actors.IsEmpty())
	{
		GEditor->MoveViewportCamerasToActor(Actors, /*bActiveViewportOnly*/ false);
	}
}

TSharedRef<ITableRow> SSceneCostView::MakePlacementRow(FPlacementItem Item, const TSharedRef<STableViewBase>& Owner)
{
	return SNew(SSceneCostPlacementRow, Owner, Item);
}

#undef LOCTEXT_NAMESPACE
