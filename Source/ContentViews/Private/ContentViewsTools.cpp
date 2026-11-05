#include "ContentViewsTools.h"

#include "Kismet/KismetSystemLibrary.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ContentViewsTools)

TArray<FContentViewSummary> UContentViewsTools::ListViews()
{
	TArray<FContentViewSummary> Views;
	for (const FAssetData& Asset : UE::ContentViews::FindViews())
	{
		if (const UContentViewBase* View = Cast<UContentViewBase>(Asset.GetAsset()))
		{
			Views.Add(Summarise(*View));
		}
	}
	return Views;
}

FContentViewDescription UContentViewsTools::DescribeView(const FString& View)
{
	FContentViewDescription Description;
	if (const UContentViewDefinition* Found = FindViewOrRaise(View))
	{
		Description.View = Summarise(*Found);
		Description.Columns = UE::ContentViews::DescribeColumns(*Found);
		Description.Filters = Found->Filters;
		Description.DefaultSort = Found->DefaultSort;
		Description.RowCount = UE::ContentViews::GatherRows(*Found).Num();
	}
	return Description;
}

TArray<FContentViewRow> UContentViewsTools::RunView(const FString& View, const TArray<FContentViewFilter>& Filters,
	const FString& SortColumn, bool bSortDescending, int32 Limit)
{
	const UContentViewDefinition* Found = FindViewOrRaise(View);
	if (!Found)
	{
		return {};
	}
	if (Limit < 0)
	{
		UKismetSystemLibrary::RaiseScriptError(TEXT("Limit must be 0 (all rows) or more."));
		return {};
	}
	for (const FContentViewFilter& Filter : Filters)
	{
		const FText Problem = UE::ContentViews::CheckFilter(Filter);
		if (!Problem.IsEmpty())
		{
			UKismetSystemLibrary::RaiseScriptError(FString::Printf(TEXT("Filter '%s' %s."), *Filter.Describe(), *Problem.ToString()));
			return {};
		}
		if (!UE::ContentViews::IsKnownColumn(*Found, Filter.Column))
		{
			UKismetSystemLibrary::RaiseScriptError(FString::Printf(
				TEXT("Filter '%s' names a column no row of %s carries."), *Filter.Describe(), *View));
			return {};
		}
	}

	FContentViewSort Sort;
	Sort.Column = SortColumn.IsEmpty() ? NAME_None : FName(*SortColumn);
	Sort.bDescending = bSortDescending;
	if (!Sort.Column.IsNone() && !UE::ContentViews::IsKnownColumn(*Found, Sort.Column))
	{
		UKismetSystemLibrary::RaiseScriptError(FString::Printf(TEXT("No row of %s carries the column '%s'."), *View, *SortColumn));
		return {};
	}
	return UE::ContentViews::RunView(*Found, Filters, Sort, Limit);
}

FContentViewSpec UContentViewsTools::ExportView(const FString& View)
{
	const UContentViewDefinition* Found = FindViewOrRaise(View);
	return Found ? Found->ToSpec() : FContentViewSpec();
}

UContentViewDefinition* UContentViewsTools::FindViewOrRaise(const FString& View)
{
	UContentViewBase* Found = UE::ContentViews::FindViewAsset(View);
	if (UContentViewDefinition* Assets = Cast<UContentViewDefinition>(Found))
	{
		return Assets;
	}
	if (Found)
	{
		UKismetSystemLibrary::RaiseScriptError(FString::Printf(
			TEXT("'%s' is a %s, whose rows are not assets; ListViews names its archetype, and that archetype's own tools run it."),
			*View, *Found->GetClass()->GetName()));
		return nullptr;
	}
	TArray<FString> Names;
	for (const FAssetData& Asset : UE::ContentViews::FindViews())
	{
		Names.Add(Asset.AssetName.ToString());
	}
	UKismetSystemLibrary::RaiseScriptError(FString::Printf(TEXT("No view named '%s'. Views: %s"), *View,
		Names.IsEmpty() ? TEXT("(none)") : *FString::Join(Names, TEXT(", "))));
	return nullptr;
}

FContentViewSummary UContentViewsTools::Summarise(const UContentViewBase& View)
{
	FContentViewSummary Summary;
	Summary.Name = View.GetViewName().ToString();
	Summary.AssetPath = FSoftObjectPath(&View).ToString();
	Summary.Archetype = View.GetClass()->GetName();
	if (const UContentViewDefinition* Assets = Cast<UContentViewDefinition>(&View))
	{
		Summary.RowClass = Assets->RowClass.ToString();
		for (const FName Column : Assets->Columns)
		{
			Summary.Columns.Add(Column.ToString());
		}
	}
	return Summary;
}
