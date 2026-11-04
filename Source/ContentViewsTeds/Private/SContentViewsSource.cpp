#include "SContentViewsSource.h"

#include "ContentViewsTeds.h"
#include "ContentViewsTedsBackend.h"
#include "Elements/Framework/TypedElementMetaData.h"
#include "Elements/Interfaces/TypedElementDataStorageInterface.h"
#include "Elements/Interfaces/TypedElementDataStorageUiInterface.h"
#include "Filters/FilterBase.h"
#include "TedsAssetDataColumns.h"
#include "TedsFilter.h"
#include "TedsQueryNode.h"
#include "TedsQueryStackExecutor.h"
#include "TedsRowFilterNode.h"
#include "TedsRowQueryResultsNode.h"
#include "TedsRowSortNode.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/STedsFilterBar.h"
#include "Widgets/STedsSearchBox.h"
#include "Widgets/STedsTableViewer.h"
#include "Widgets/STedsTileViewer.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ContentViewsTeds"

using namespace UE::Editor::DataStorage;
using namespace UE::Editor::DataStorage::Queries;
using UE::ContentViews::Teds::FBackend;
using UE::ContentViews::Teds::FColumnSlot;
using UE::ContentViews::Teds::FViewState;

void SContentViewsSource::Construct(const FArguments& InArgs, const FSoftObjectPath& InView)
{
	ViewPath = InView;
	FBackend::Get().OnViewChanged().AddSP(this, &SContentViewsSource::HandleViewChanged);
	Build();
}

SContentViewsSource::~SContentViewsSource()
{
	FBackend::Get().OnViewChanged().RemoveAll(this);
	ReleaseQueries();
}

void SContentViewsSource::ReleaseQueries()
{
	if (ICoreProvider* Storage = FBackend::Get().GetStorage())
	{
		for (const QueryHandle Query : FilterQueries)
		{
			Storage->UnregisterQuery(Query);
		}
	}
	FilterQueries.Reset();
}

void SContentViewsSource::HandleViewChanged(const FSoftObjectPath& Changed)
{
	if (Changed == ViewPath)
	{
		Build();
	}
}

void SContentViewsSource::Build()
{
	ReleaseQueries();
	FBackend& Backend = FBackend::Get();
	ICoreProvider* Storage = Backend.GetStorage();
	const FViewState* State = Backend.FindView(ViewPath);
	const UContentViewDefinition* View = State ? State->View.Get() : nullptr;
	if (!Storage || !View)
	{
		ChildSlot[SNew(STextBlock).Text(LOCTEXT("NoView", "This view's asset is gone."))];
		return;
	}

	Rows = MakeShared<QueryStack::FRowQueryResultsNode>(*Storage, MakeShared<QueryStack::FQueryNode>(*Storage, State->MakeRowQuery()),
		QueryStack::FRowQueryResultsNode::ESyncActions::RefreshOnUpdate);
	const TSharedRef<STedsSearchBox> SearchBox = SNew(STedsSearchBox).InSearchableRowNode(Rows).OutSearchNode(&Searched);

	// The view's own filters, on as it opens, as the legacy backend applies them; then a shape filter per column.
	TArray<TSharedPtr<FTedsFilter>> Filters;
	const TSharedPtr<FFilterCategory> ViewCategory = MakeShared<FFilterCategory>(LOCTEXT("ViewFilters", "View"),
		LOCTEXT("ViewFiltersTooltip", "The view asset's filters."));
	for (const FContentViewFilter& Filter : View->Filters)
	{
		const FString Said = Filter.Describe();
		Filters.Add(MakeShared<FTedsFilter>(FName(*Said), FText::FromString(Said), FText::FromString(Said), NAME_None, ViewCategory,
			Backend.MakeValueFilter(Filter), /*bActiveByDefault*/ true));
	}
	const TSharedPtr<FFilterCategory> ShapeCategory = MakeShared<FFilterCategory>(LOCTEXT("HasFilters", "Has a value"),
		LOCTEXT("HasFiltersTooltip", "Rows with a value in a column."));
	for (const FColumnSlot& Slot : State->Columns)
	{
		if (Slot.Kind == UE::ContentViews::Teds::EColumnKind::AssetName)
		{
			continue;
		}
		// A shape filter is a query: the rows whose table holds the column.
		const QueryHandle Query = Storage->RegisterQuery(Select().ReadOnly<FAssetDataColumn_Experimental>().Where().All(Slot.Type).Compile());
		FilterQueries.Add(Query);
		const FText Label = FText::Format(LOCTEXT("Has", "Has {0}"), FText::FromString(FName::NameToDisplayString(Slot.Name.ToString(), false)));
		Filters.Add(MakeShared<FTedsFilter>(FName(*(TEXT("Has.") + Slot.Name.ToString())), Label, Label, NAME_None, ShapeCategory, Query));
	}
	const TSharedRef<STedsFilterBar> FilterBar = SNew(STedsFilterBar)
		.FilterBarIdentifier(FName(*(TEXT("ContentViews.") + ViewPath.GetAssetName())))
		.InFilterableRowNode(Searched)
		.OutFilteredNode(&Filtered)
		.Filters(Filters);

	TSharedPtr<SWidget> Body;
	if (View->Mode == EContentViewMode::Tiles)
	{
		Body = SNew(STedsTileViewer)
			.QueryStack(Filtered)
			.WidgetPurpose(IUiProvider::FPurposeInfo("ContentBrowser", "TileLabel", NAME_None).GeneratePurposeID())
			.Columns(State->ShownColumns())
			.SelectionMode(ESelectionMode::Multi);
	}
	else
	{
		const FColumnSlot* SortSlot = State->FindColumn(View->DefaultSort.Column.IsNone() ? FName(TEXT("Name")) : View->DefaultSort.Column);
		TSharedPtr<FColumnMetaData> SortMeta;
		TWeakObjectPtr<const UScriptStruct> SortType;
		if (SortSlot)
		{
			SortType = SortSlot->Type;
			SortMeta = MakeShared<FColumnMetaData>(SortSlot->Type, FColumnMetaData::EFlags::None);
			SortMeta->AddOrSetMutableData("bColumnInitialSortIsAscending", !View->DefaultSort.bDescending);
		}
		Body = SNew(STedsTableViewer)
			.TableViewerIdentifier(FName(*(TEXT("ContentViews.") + ViewPath.GetAssetName())))
			.QueryStack(Filtered)
			.Columns(State->ShownColumns())
			.CellWidgetPurpose(IUiProvider::FPurposeInfo("ContentBrowser", "RowLabel", NAME_None).GeneratePurposeID())
			.PrimaryColumn(SortType)
			.ColumnMetaData(SortMeta ? TArray<TSharedPtr<FColumnMetaData>>{SortMeta} : TArray<TSharedPtr<FColumnMetaData>>())
			.ListSelectionMode(ESelectionMode::Multi)
			.EmptyRowsMessage(LOCTEXT("NoRows", "No assets match."));
	}

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(8.f, 6.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
			[
				SBasicFilterBar<FTedsRowHandle&>::MakeAddFilterButton(FilterBar)
			]
			+ SHorizontalBox::Slot().FillWidth(1.f)
			[
				SearchBox
			]
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			FilterBar
		]
		+ SVerticalBox::Slot().FillHeight(1.f)
		[
			Body.ToSharedRef()
		]
	];
}

namespace UE::ContentViews::Teds
{
	namespace
	{
		/** Runs a stack to its end the way the table's model does, then gives its rows as asset paths. */
		TArray<FString> RunStack(ICoreProvider& Storage, const TSharedPtr<QueryStack::IRowNode>& Root,
			TFunctionRef<bool()> IsBusy = []() { return false; })
		{
			QueryStack::FExplicitUpdateExecutor Executor(TEXT("ContentViewsTest"), Root);
			for (int32 Pass = 0; Pass < 1000; ++Pass)
			{
				Executor.Update();
				if (!IsBusy())
				{
					break;
				}
			}
			TArray<FString> Paths;
			for (const RowHandle Row : Root->GetRows())
			{
				if (const FAssetDataColumn_Experimental* Asset = Storage.GetColumn<FAssetDataColumn_Experimental>(Row))
				{
					Paths.Add(Asset->AssetData.GetSoftObjectPath().ToString());
				}
			}
			return Paths;
		}
	}

	TArray<FString> SortRows(const UContentViewDefinition& View, FName Column, bool bDescending)
	{
		FBackend& Backend = FBackend::Get();
		const FViewState* State = Backend.FindView(FSoftObjectPath(&View));
		const FColumnSlot* Slot = State ? State->FindColumn(Column) : nullptr;
		if (!Slot)
		{
			return {};
		}
		ICoreProvider& Storage = *Backend.GetStorage();
		// The sorter TEDS generates from the column's Sortable property, as the table's header does.
		TArray<TSharedPtr<const FColumnSorterInterface>> Sorters;
		TArray<TWeakObjectPtr<const UScriptStruct>> Types = {Slot->Type};
		Backend.GetUi()->GeneratePropertySorters(Sorters, Types);
		if (Sorters.IsEmpty())
		{
			return {};
		}
		const TSharedPtr<QueryStack::IRowNode> Rows = MakeShared<QueryStack::FRowQueryResultsNode>(Storage,
			MakeShared<QueryStack::FQueryNode>(Storage, State->MakeRowQuery()), QueryStack::FRowQueryResultsNode::ESyncActions::RefreshOnUpdate);
		const TSharedPtr<QueryStack::FRowSortNode> Sorted = MakeShared<QueryStack::FRowSortNode>(Storage, Rows, Sorters[0]);
		TArray<FString> Paths = RunStack(Storage, Sorted, [&Sorted]() { return Sorted->IsSorting(); });
		if (bDescending)
		{
			Algo::Reverse(Paths);
		}
		return Paths;
	}

	TArray<FString> FilterRows(const UContentViewDefinition& View, const FContentViewFilter& Filter)
	{
		FBackend& Backend = FBackend::Get();
		const FViewState* State = Backend.FindView(FSoftObjectPath(&View));
		if (!State)
		{
			return {};
		}
		ICoreProvider& Storage = *Backend.GetStorage();
		const TSharedPtr<QueryStack::IRowNode> Rows = MakeShared<QueryStack::FRowQueryResultsNode>(Storage,
			MakeShared<QueryStack::FQueryNode>(Storage, State->MakeRowQuery()), QueryStack::FRowQueryResultsNode::ESyncActions::RefreshOnUpdate);
		return RunStack(Storage, MakeShared<QueryStack::FRowFilterNode>(&Storage, Rows, Backend.MakeValueFilter(Filter)));
	}
}

#undef LOCTEXT_NAMESPACE
