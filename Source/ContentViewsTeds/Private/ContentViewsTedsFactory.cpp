#include "ContentViewsTedsFactory.h"

#include "ContentSources/Columns/ContentSourcesColumns.h"
#include "ContentSources/IContentSource.h"
#include "ContentViewValue.h"
#include "ContentViewsTedsBackend.h"
#include "ContentViewsTedsColumns.h"
#include "DataStorage/Features.h"
#include "Elements/Framework/TypedElementAttributeBinding.h"
#include "Elements/Framework/TypedElementQueryBuilder.h"
#include "Components/StaticMeshComponent.h"
#include "Elements/Columns/TypedElementCompatibilityColumns.h"
#include "Elements/Columns/TypedElementMiscColumns.h"
#include "SContentViewsSource.h"
#include "SSceneCostView.h"
#include "TedsAssetDataColumns.h"
#include "TedsQueryNode.h"
#include "TedsRowQueryResultsNode.h"
#include "Columns/SlateHeaderColumns.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/STedsTableViewer.h"
#include "Widgets/STedsTileViewer.h"
#include "Widgets/Text/STextBlock.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ContentViewsTedsFactory)

using namespace UE::Editor::DataStorage;
using namespace UE::Editor::DataStorage::Queries;

namespace
{
	const IUiProvider::FPurposeID& ContentViewsAssetViewPurpose()
	{
		static const IUiProvider::FPurposeID Purpose(IUiProvider::FPurposeInfo("ContentBrowser", "AssetView", NAME_None).GeneratePurposeID());
		return Purpose;
	}

	/** What the engine's own host builds for a source: its query's rows in a table or tiles, refreshed every update. */
	class SContentViewsPlainSource final : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SContentViewsPlainSource) {}
			SLATE_ARGUMENT(UE::Editor::ContentBrowser::FTableViewerInitParams, InitParams)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs, ICoreProvider& Storage)
		{
			const UE::Editor::ContentBrowser::FTableViewerInitParams& Params = InArgs._InitParams;
			Rows = MakeShared<QueryStack::FRowQueryResultsNode>(Storage, MakeShared<QueryStack::FQueryNode>(Storage, Params.QueryDescription),
				QueryStack::FRowQueryResultsNode::ESyncActions::ForceRefreshOnUpdate);
			if (Params.TableViewMode == ETableViewMode::Tile)
			{
				ChildSlot[SNew(STedsTileViewer).QueryStack(Rows).WidgetPurpose(Params.CellWidgetPurpose).Columns(Params.Columns)
					.SelectionMode(ESelectionMode::Multi)];
			}
			else
			{
				ChildSlot[SNew(STedsTableViewer).QueryStack(Rows).CellWidgetPurpose(Params.CellWidgetPurpose).Columns(Params.Columns)
					.ListSelectionMode(ESelectionMode::Multi)];
			}
		}

	private:
		TSharedPtr<QueryStack::FRowQueryResultsNode> Rows;
	};
}

void UContentViewsTedsFactory::RegisterTables(ICoreProvider& DataStorage)
{
	DataStorage.RegisterTable<FContentViewSceneMeshTag>(UE::ContentViews::Teds::FBackend::SceneTableName);
}

void UContentViewsTedsFactory::RegisterQueries(ICoreProvider& DataStorage)
{
	// Scene Cost: a row syncs from the world when its object is added, loaded or modified, for one frame. Only static mesh
	// components are noted; the cooperative task reads each one's mesh and instances into its placement column.
	DataStorage.RegisterQuery(
		Select(
			TEXT("Content Views: note synced static mesh components"),
			FProcessor(EQueryTickPhase::DuringPhysics, DataStorage.GetQueryTickGroupName(EQueryTickGroups::Update))
				.SetExecutionMode(EExecutionMode::GameThread),
			[](IQueryContext& Context, const RowHandle* Rows, const FTypedElementUObjectColumn* Objects)
			{
				UE::ContentViews::Teds::FBackend& Backend = UE::ContentViews::Teds::FBackend::Get();
				if (!Backend.IsCountingScene())
				{
					return;
				}
				TArray<RowHandle, TInlineAllocator<32>> Components;
				for (uint32 Index = 0; Index < Context.GetRowCount(); ++Index)
				{
					if (Cast<UStaticMeshComponent>(Objects[Index].Object.Get()))
					{
						Components.Add(Rows[Index]);
					}
				}
				Backend.NoteComponentRows(Components);
			})
		.Where()
			.All<FTypedElementSyncFromWorldTag>()
		.Compile());

	// A component row that goes (its actor deleted, unloaded or garbage collected) takes its placement with it.
	DataStorage.RegisterQuery(
		Select(
			TEXT("Content Views: a placement went"),
			FObserver::OnRemove<FContentViewPlacementColumn>().SetExecutionMode(EExecutionMode::GameThread),
			[](IQueryContext& Context, RowHandle Row, const FContentViewPlacementColumn& Placement)
			{
				UE::ContentViews::Teds::FBackend::Get().NoteSceneChanged();
			})
		.Compile());

	// Gated on the tag TedsAssetData stamps on a new or changed asset row for one frame, so it runs only on changes.
	DataStorage.RegisterQuery(
		Select(
			TEXT("Content Views: note changed asset rows"),
			FProcessor(EQueryTickPhase::DuringPhysics, DataStorage.GetQueryTickGroupName(EQueryTickGroups::Update))
				.SetExecutionMode(EExecutionMode::GameThread),
			[](IQueryContext& Context, const RowHandle* Rows)
			{
				UE::ContentViews::Teds::FBackend::Get().NoteUpdatedRows(TConstArrayView<RowHandle>(Rows, Context.GetRowCount()));
			})
		.Where()
			.All<FUpdatedAssetDataTag, FAssetDataColumn_Experimental>()
		.Compile());
}

void UContentViewsTedsFactory::RegisterWidgetPurposes(IUiProvider& DataStorageUi) const
{
	DataStorageUi.RegisterWidgetPurpose(IUiProvider::FPurposeInfo("ContentViews", "Header", NAME_None,
		IUiProvider::EPurposeType::UniqueByNameAndColumn, NSLOCTEXT("ContentViewsTeds", "HeaderPurpose", "A Content View table's column headers."),
		IUiProvider::FPurposeInfo("General", "Header", NAME_None).GeneratePurposeID()));
}

void UContentViewsTedsFactory::RegisterWidgetConstructors(ICoreProvider& DataStorage, IUiProvider& DataStorageUi) const
{
	// The purpose is unique by name, so this replaces TedsContentBrowser's host, which this factory's order runs after.
	DataStorageUi.RegisterWidgetFactory<FContentViewsAssetViewConstructor>(DataStorageUi.FindPurpose(ContentViewsAssetViewPurpose()));
	DataStorageUi.RegisterWidgetFactory<FContentViewsHeaderConstructor>(DataStorageUi.FindPurpose(UE::ContentViews::Teds::HeaderPurpose()),
		FConditions(TColumn<FAssetNameColumn>()));
}

TSharedPtr<SWidget> FContentViewsHeaderConstructor::CreateWidget(ICoreProvider* DataStorage, IUiProvider* DataStorageUi,
	RowHandle TargetRow, RowHandle WidgetRow, const FMetaDataView& Arguments)
{
	const TArray<TWeakObjectPtr<const UScriptStruct>>& Matched = GetMatchedColumns();
	const UScriptStruct* Column = Matched.IsEmpty() ? nullptr : Matched[0].Get();
	const bool bWide = Column && (Column == FAssetNameColumn::StaticStruct() || Column->HasMetaData(UE::ContentViews::Teds::WideColumnMeta));
	DataStorage->AddColumn(WidgetRow, FHeaderWidgetSizeColumn{.ColumnSizeMode = EColumnSizeMode::Fill, .Width = bWide ? 3.f : 1.f});
	return SNew(STextBlock).Text(CreateWidgetDisplayNameText(DataStorage));
}

void UContentViewsTedsFactory::PostRegister(ICoreProvider& DataStorage)
{
	if (IUiProvider* Ui = GetMutableDataStorageFeature<IUiProvider>(UiFeatureName))
	{
		UE::ContentViews::Teds::FBackend::Get().Start(DataStorage, *Ui);
	}
}

void UContentViewsTedsFactory::PreShutdown(ICoreProvider& DataStorage)
{
	UE::ContentViews::Teds::FBackend::Get().Stop();
}

TSharedPtr<SWidget> FContentViewsAssetViewConstructor::CreateWidget(ICoreProvider* DataStorage, IUiProvider* DataStorageUi,
	RowHandle TargetRow, RowHandle WidgetRow, const FMetaDataView& Arguments)
{
	const FContentSourceColumn* SourceColumn = DataStorage->GetColumn<FContentSourceColumn>(WidgetRow);
	const TSharedPtr<UE::Editor::ContentBrowser::IContentSource> Source = SourceColumn ? SourceColumn->ContentSource.Pin() : nullptr;
	if (!Source)
	{
		return SNullWidget::NullWidget;
	}
	const UE::ContentViews::Teds::FBackend& Backend = UE::ContentViews::Teds::FBackend::Get();
	if (const TOptional<FSoftObjectPath> Path = Backend.FindPathOfSource(Source.Get()))
	{
		if (Backend.FindView(*Path))
		{
			return SNew(SContentViewsSource, *Path);
		}
		if (Backend.FindSceneView(*Path))
		{
			return SNew(SSceneCostView, *Path);
		}
	}
	UE::Editor::ContentBrowser::FTableViewerInitParams Params;
	Source->GetAssetViewInitParams(Params);
	return SNew(SContentViewsPlainSource, *DataStorage).InitParams(Params);
}

TSharedPtr<SWidget> FContentViewsCellConstructor::CreateWidget(ICoreProvider* DataStorage, IUiProvider* DataStorageUi,
	RowHandle TargetRow, RowHandle WidgetRow, const FMetaDataView& Arguments)
{
	FAttributeBinder Binder(TargetRow, DataStorage);
	TAttribute<FText> Text;
	if (const FName* Number = MatchedDynamicTemplates.Find(FContentViewNumberColumn::StaticStruct()))
	{
		Text = Binder.BindText(*Number, &FContentViewNumberColumn::Display);
	}
	else if (const FName* Computed = MatchedDynamicTemplates.Find(FContentViewStringColumn::StaticStruct()))
	{
		Text = Binder.BindText(*Computed, &FContentViewStringColumn::Display);
	}
	else if (const FName* Colour = MatchedDynamicTemplates.Find(FContentViewColourColumn::StaticStruct()))
	{
		const TAttribute<FText> Hex = Binder.BindText(*Colour, &FContentViewColourColumn::Hex);
		return SNew(SBox).HAlign(HAlign_Left).VAlign(VAlign_Center).WidthOverride(48.f).HeightOverride(14.f)
		[
			SNew(SColorBlock).Color(Binder.BindData(*Colour, &FContentViewColourColumn::Colour)).ToolTipText(Hex)
		];
	}
	else if (const FName* Tag = MatchedDynamicTemplates.Find(FItemStringAttributeColumn_Experimental::StaticStruct()))
	{
		// A registry tag holds its value; a reference shows the object's name, as in the legacy backend.
		Text = Binder.BindData(*Tag, &FItemStringAttributeColumn_Experimental::Value, [](const FString& Value)
		{
			return FText::FromString(FContentViewValue::FromTagString(Value).GetDisplay());
		});
	}
	else
	{
		return SNullWidget::NullWidget;
	}
	return SNew(STextBlock).Text(Text).ToolTipText(Text);
}

namespace UE::ContentViews::Teds
{
	const TCHAR* const WideColumnMeta = TEXT("ContentViewsWide");

	IUiProvider::FPurposeID HeaderPurpose()
	{
		return IUiProvider::FPurposeInfo("ContentViews", "Header", NAME_None).GeneratePurposeID();
	}

	void RegisterContentViewCell(IUiProvider& Ui, const UScriptStruct& Template, FName Identifier)
	{
		const FConditions Column(TColumn<void>(TWeakObjectPtr<const UScriptStruct>(&Template), Identifier));
		Ui.RegisterWidgetFactory<FContentViewsCellConstructor>(Ui.FindPurpose(Ui.GetGeneralWidgetPurposeID()), Column);
		Ui.RegisterWidgetFactory<FContentViewsHeaderConstructor>(Ui.FindPurpose(HeaderPurpose()), Column);
	}
}
