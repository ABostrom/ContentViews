#pragma once

#include "CoreMinimal.h"
#include "ContentViewValue.generated.h"

/** What kind of value a column holds. */
UENUM(BlueprintType)
enum class EContentViewValueType : uint8
{
	String,
	Number,
	Bool,
};

/** One cell of a view: a typed value read from an asset's registry data, and the words the column shows. */
USTRUCT(BlueprintType)
struct CONTENTVIEWS_API FContentViewValue
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Content Views")
	EContentViewValueType Type = EContentViewValueType::String;

	/** False when the asset has no value for the column. */
	UPROPERTY(BlueprintReadOnly, Category = "Content Views")
	bool bIsSet = false;

	UPROPERTY(BlueprintReadOnly, Category = "Content Views")
	FString String;

	UPROPERTY(BlueprintReadOnly, Category = "Content Views")
	double Number = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Content Views")
	bool bBool = false;

	/** What the column shows when it differs from the value (an asset path shown as the asset's name). */
	UPROPERTY(BlueprintReadOnly, Category = "Content Views")
	FString Display;

	static FContentViewValue OfString(FString InValue, FString InDisplay = FString());
	static FContentViewValue OfNumber(double InValue);
	static FContentViewValue OfBool(bool bInValue);

	/**
	 * A registry tag's text as a value: a number when it parses as one, True/False as a bool, otherwise a string. An object
	 * path, or a comma list of them, keeps the path and shows the object names. Empty text, or None (an empty reference), is no value.
	 */
	static FContentViewValue FromTagString(const FString& Raw);

	/** The words a column shows: Display when set, else the value formatted (whole numbers without decimals). */
	FString GetDisplay() const;

	/** Orders two cells of one column: numbers and bools by value, strings without case; unset cells last. */
	static int32 Compare(const FContentViewValue& A, const FContentViewValue& B);
};
