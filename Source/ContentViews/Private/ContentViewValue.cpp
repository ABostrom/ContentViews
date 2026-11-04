#include "ContentViewValue.h"

#include "Algo/AllOf.h"
#include "ContentViewColumns.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ContentViewValue)

namespace
{
	/** An object or class path as the registry writes one: /Pkg/Path.Obj, or Class'/Pkg/Path.Obj'. */
	bool ContentViewLooksLikePath(const FString& Text)
	{
		if (Text.Contains(TEXT(" ")) || Text.Len() < 3)
		{
			return false;
		}
		const int32 Quote = Text.Find(TEXT("'/"));
		const FString Inner = Quote != INDEX_NONE ? Text.Mid(Quote + 1) : Text;
		return Inner.StartsWith(TEXT("/")) && Inner.Contains(TEXT("."));
	}

	/** The object name a path ends in, a Blueprint class's _C dropped: BP_GA_Tackle for .../BP_GA_Tackle.BP_GA_Tackle_C. */
	FString ContentViewObjectName(const FString& Path)
	{
		FString Name = Path;
		int32 Quote = INDEX_NONE;
		if (Name.FindChar(TEXT('\''), Quote))
		{
			Name.RightChopInline(Quote + 1);
			Name.RemoveFromEnd(TEXT("'"));
		}
		int32 Split = INDEX_NONE;
		if (Name.FindLastChar(TEXT(':'), Split) || Name.FindLastChar(TEXT('.'), Split))
		{
			Name.RightChopInline(Split + 1);
		}
		Name.RemoveFromEnd(TEXT("_C"));
		return Name;
	}
}

FContentViewValue FContentViewValue::OfString(FString InValue, FString InDisplay)
{
	FContentViewValue Value;
	Value.Type = EContentViewValueType::String;
	Value.bIsSet = true;
	Value.String = MoveTemp(InValue);
	Value.Display = MoveTemp(InDisplay);
	return Value;
}

FContentViewValue FContentViewValue::OfNumber(double InValue)
{
	FContentViewValue Value;
	Value.Type = EContentViewValueType::Number;
	Value.bIsSet = true;
	Value.Number = InValue;
	return Value;
}

FContentViewValue FContentViewValue::OfBool(bool bInValue)
{
	FContentViewValue Value;
	Value.Type = EContentViewValueType::Bool;
	Value.bIsSet = true;
	Value.bBool = bInValue;
	return Value;
}

FContentViewValue FContentViewValue::FromTagString(const FString& Raw)
{
	const FString Text = Raw.TrimStartAndEnd();
	if (Text.IsEmpty())
	{
		return FContentViewValue();
	}
	if (Text.IsNumeric())
	{
		return OfNumber(FCString::Atod(*Text));
	}
	if (Text.Equals(TEXT("True"), ESearchCase::IgnoreCase) || Text.Equals(TEXT("False"), ESearchCase::IgnoreCase))
	{
		return OfBool(Text.Equals(TEXT("True"), ESearchCase::IgnoreCase));
	}

	const TArray<FString> Items = UE::ContentViews::SplitList(Text);
	if (Items.Num() > 0 && Algo::AllOf(Items, &ContentViewLooksLikePath))
	{
		TArray<FString> Names;
		for (const FString& Item : Items)
		{
			Names.Add(ContentViewObjectName(Item));
		}
		return OfString(Text, FString::Join(Names, TEXT(", ")));
	}
	return OfString(Text);
}

FString FContentViewValue::GetDisplay() const
{
	if (!bIsSet)
	{
		return FString();
	}
	if (!Display.IsEmpty())
	{
		return Display;
	}
	switch (Type)
	{
	case EContentViewValueType::Number:
		return FMath::IsNearlyEqual(Number, FMath::RoundToDouble(Number))
			? FString::Printf(TEXT("%lld"), static_cast<int64>(FMath::RoundToDouble(Number)))
			: FString::SanitizeFloat(FMath::RoundToDouble(Number * 100.0) / 100.0);
	case EContentViewValueType::Bool:
		return bBool ? TEXT("True") : TEXT("False");
	default:
		return String;
	}
}

int32 FContentViewValue::Compare(const FContentViewValue& A, const FContentViewValue& B)
{
	if (A.bIsSet != B.bIsSet)
	{
		return A.bIsSet ? -1 : 1;
	}
	if (!A.bIsSet)
	{
		return 0;
	}
	if (A.Type == EContentViewValueType::Number && B.Type == EContentViewValueType::Number)
	{
		return A.Number < B.Number ? -1 : (A.Number > B.Number ? 1 : 0);
	}
	if (A.Type == EContentViewValueType::Bool && B.Type == EContentViewValueType::Bool)
	{
		return static_cast<int32>(A.bBool) - static_cast<int32>(B.bBool);
	}
	return A.GetDisplay().Compare(B.GetDisplay(), ESearchCase::IgnoreCase);
}
