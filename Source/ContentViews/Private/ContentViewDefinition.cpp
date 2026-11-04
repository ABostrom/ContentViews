#include "ContentViewDefinition.h"

#include "ContentViewColumns.h"
#include "ContentViewQuery.h"
#include "JsonObjectConverter.h"
#include "Misc/DataValidation.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ContentViewDefinition)

#define LOCTEXT_NAMESPACE "ContentViews"

FString FContentViewFilter::Describe() const
{
	const UEnum* Ops = StaticEnum<EContentViewFilterOp>();
	return FString::Printf(TEXT("%s %s %s"), *Column.ToString(),
		*Ops->GetDisplayNameTextByValue(static_cast<int64>(Operator)).ToString(), *Value);
}

FPrimaryAssetType UContentViewDefinition::StaticAssetType()
{
	static const FPrimaryAssetType Type(TEXT("ContentView"));
	return Type;
}

FPrimaryAssetId UContentViewDefinition::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(StaticAssetType(), GetFName());
}

FText UContentViewDefinition::GetViewName() const
{
	return DisplayName.IsEmpty() ? FText::FromName(GetFName()) : DisplayName;
}

FContentViewSpec UContentViewDefinition::ToSpec() const
{
	FContentViewSpec Spec;
	Spec.DisplayName = GetViewName().ToString();
	Spec.Icon = Icon;
	Spec.RowClass = RowClass.ToString();
	Spec.bIncludeSubclasses = bIncludeSubclasses;
	Spec.Columns = Columns;
	Spec.DefaultSort = DefaultSort;
	Spec.Filters = Filters;
	Spec.Mode = Mode;
	return Spec;
}

FString UContentViewDefinition::ExportJson() const
{
	FString Json;
	FJsonObjectConverter::UStructToJsonObjectString(ToSpec(), Json);
	return Json;
}

#if WITH_EDITOR
EDataValidationResult UContentViewDefinition::IsDataValid(FDataValidationContext& Context) const
{
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		return EDataValidationResult::NotValidated;
	}

	const uint32 ErrorsBefore = Context.GetNumErrors();
	const FText ViewName = GetViewName();

	if (RowClass.IsNull())
	{
		Context.AddError(FText::Format(LOCTEXT("NoRowClass",
			"View {0}: Row Class is empty, so the view lists nothing. Pick the class whose assets are its rows."), ViewName));
	}
	else if (!UE::ContentViews::ResolveRowClass(*this))
	{
		Context.AddError(FText::Format(LOCTEXT("MissingRowClass",
			"View {0}: Row Class {1} does not exist, so the view lists nothing. Pick a class that does."),
			ViewName, FText::FromString(RowClass.ToString())));
	}

	if (Columns.IsEmpty())
	{
		Context.AddError(FText::Format(LOCTEXT("NoColumns",
			"View {0} has no columns, so its list shows nothing about its rows. Add at least Name."), ViewName));
	}

	const auto CheckColumn = [this, &Context, &ViewName](FName Column, const FText& Where)
	{
		if (Column.IsNone())
		{
			Context.AddError(FText::Format(LOCTEXT("EmptyColumn",
				"View {0}: {1} names no column. Name a column or remove the entry."), ViewName, Where));
		}
		else if (!UE::ContentViews::IsKnownColumn(*this, Column))
		{
			Context.AddError(FText::Format(LOCTEXT("UnknownColumn",
				"View {0}: {1} names '{2}', which is neither a computed column nor a registry tag any row carries, so it "
				"stays empty. Check the spelling, or make the field AssetRegistrySearchable and resave the rows."),
				ViewName, Where, FText::FromName(Column)));
		}
	};

	for (int32 Index = 0; Index < Columns.Num(); ++Index)
	{
		CheckColumn(Columns[Index], FText::Format(LOCTEXT("ColumnWhere", "column {0}"), Index));
	}

	for (int32 Index = 0; Index < Filters.Num(); ++Index)
	{
		const FContentViewFilter& Filter = Filters[Index];
		const FText Where = FText::Format(LOCTEXT("FilterWhere", "filter {0} ({1})"), Index,
			FText::FromString(Filter.Describe()));
		const FText Problem = UE::ContentViews::CheckFilter(Filter);
		if (!Problem.IsEmpty())
		{
			Context.AddError(FText::Format(LOCTEXT("BadFilter",
				"View {0}: {1} {2}, so no row passes it. Fix the filter or remove it."), ViewName, Where, Problem));
		}
		else
		{
			CheckColumn(Filter.Column, Where);
		}
	}

	if (!DefaultSort.Column.IsNone() && !Columns.Contains(DefaultSort.Column))
	{
		Context.AddError(FText::Format(LOCTEXT("SortNotAColumn",
			"View {0}: Default Sort names '{1}', which is not one of its columns, so the list opens in no visible order. "
			"Pick one of the columns, or None for Name."), ViewName, FText::FromName(DefaultSort.Column)));
	}

	return Context.GetNumErrors() > ErrorsBefore ? EDataValidationResult::Invalid : EDataValidationResult::Valid;
}
#endif

#undef LOCTEXT_NAMESPACE
