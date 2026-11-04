#include "ContentViewsFilterExtension.h"
#include "ContentViewsSettings.h"

#include "Algo/AllOf.h"
#include "ContentBrowserItem.h"
#include "ContentViewDefinition.h"
#include "ContentViewQuery.h"
#include "FrontendFilterBase.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ContentViewsFilterExtension)

#define LOCTEXT_NAMESPACE "ContentViews"

namespace
{
	/** A view as a Content Browser filter: its row class and filters, copied when the filter list is built. */
	class FContentViewFrontendFilter final : public FFrontendFilter
	{
	public:
		FContentViewFrontendFilter(TSharedPtr<FFrontendFilterCategory> InCategory, const UContentViewDefinition& View,
			UClass& InRowClass)
			: FFrontendFilter(MoveTemp(InCategory))
			, Name(TEXT("ContentView_") + View.GetName())
			, Label(View.GetViewName())
			, RowClass(&InRowClass)
			, bIncludeSubclasses(View.bIncludeSubclasses)
			, Filters(View.Filters)
		{
			TArray<FString> Conditions;
			for (const FContentViewFilter& Filter : Filters)
			{
				Conditions.Add(Filter.Describe());
			}
			Tooltip = Conditions.IsEmpty()
				? FText::Format(LOCTEXT("FilterTooltip", "The rows of the {0} view."), Label)
				: FText::Format(LOCTEXT("FilterTooltipConditions", "The rows of the {0} view: {1}."), Label,
					FText::FromString(FString::Join(Conditions, TEXT(", "))));
		}

		virtual FString GetName() const override { return Name; }
		virtual FText GetDisplayName() const override { return Label; }
		virtual FText GetToolTipText() const override { return Tooltip; }

		virtual bool PassesFilter(FAssetFilterType InItem) const override
		{
			FAssetData Asset;
			const UClass* Class = RowClass.Get();
			if (!Class || !InItem.Legacy_TryGetAssetData(Asset) || !UE::ContentViews::IsRowOf(Asset, *Class, bIncludeSubclasses))
			{
				return false;
			}
			return Algo::AllOf(Filters, [&Asset](const FContentViewFilter& Filter)
			{
				return UE::ContentViews::PassesFilter(Asset, Filter);
			});
		}

	private:
		FString Name;
		FText Label;
		TWeakObjectPtr<UClass> RowClass;
		bool bIncludeSubclasses;
		TArray<FContentViewFilter> Filters;
		FText Tooltip;
	};
}

void UContentViewsFilterExtension::AddFrontEndFilterExtensions(TSharedPtr<FFrontendFilterCategory> DefaultCategory,
	TArray<TSharedRef<FFrontendFilter>>& InOutFilterList) const
{
	if (!UContentViewsSettings::IsLegacyActive())
	{
		return;
	}
	const TSharedPtr<FFrontendFilterCategory> Category = MakeShared<FFrontendFilterCategory>(
		LOCTEXT("CategoryName", "Content Views"), LOCTEXT("CategoryTooltip", "The rows of each Content View."));
	for (const FAssetData& Asset : UE::ContentViews::FindViews())
	{
		const UContentViewDefinition* View = Cast<UContentViewDefinition>(Asset.GetAsset());
		UClass* RowClass = View ? UE::ContentViews::ResolveRowClass(*View) : nullptr;
		if (RowClass)
		{
			InOutFilterList.Add(MakeShared<FContentViewFrontendFilter>(Category, *View, *RowClass));
		}
	}
}

#undef LOCTEXT_NAMESPACE
