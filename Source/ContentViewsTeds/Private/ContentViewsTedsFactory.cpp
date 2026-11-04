#include "ContentViewsTedsFactory.h"

#include "ContentSources/Columns/ContentSourcesColumns.h"
#include "ContentSources/IContentSource.h"
#include "ContentViewValue.h"
#include "ContentViewsTedsBackend.h"
#include "ContentViewsTedsColumns.h"
#include "DataStorage/Features.h"
#include "Elements/Framework/TypedElementAttributeBinding.h"
#include "Elements/Framework/TypedElementQueryBuilder.h"
#include "SContentViewsSource.h"
#include "TedsAssetDataColumns.h"
#include "TedsQueryNode.h"
#include "TedsRowQueryResultsNode.h"
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

void UContentViewsTedsFactory::RegisterQueries(ICoreProvider& DataStorage)
{
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

void UContentViewsTedsFactory::RegisterWidgetConstructors(ICoreProvider& DataStorage, IUiProvider& DataStorageUi) const
{
	// The purpose is unique by name, so this replaces TedsContentBrowser's host, which this factory's order runs after.
	DataStorageUi.RegisterWidgetFactory<FContentViewsAssetViewConstructor>(DataStorageUi.FindPurpose(ContentViewsAssetViewPurpose()));
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
	if (const UE::ContentViews::Teds::FViewState* View = UE::ContentViews::Teds::FBackend::Get().FindViewOfSource(Source.Get()))
	{
		return SNew(SContentViewsSource, View->Path);
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
	void RegisterContentViewCell(IUiProvider& Ui, const UScriptStruct& Template, FName Identifier)
	{
		Ui.RegisterWidgetFactory<FContentViewsCellConstructor>(Ui.FindPurpose(Ui.GetGeneralWidgetPurposeID()),
			FConditions(TColumn<void>(TWeakObjectPtr<const UScriptStruct>(&Template), Identifier)));
	}
}
