#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ContentViewsTestRow.generated.h"

/** The row class the plugin's tests build views over: one field of each kind a column reads. */
UCLASS(Blueprintable, HideDropdown, NotPlaceable)
class CONTENTVIEWS_API UContentViewsTestRow : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, AssetRegistrySearchable, Category = "Test")
	float Speed = 0.f;

	UPROPERTY(EditAnywhere, AssetRegistrySearchable, Category = "Test")
	FString Crew;

	UPROPERTY(EditAnywhere, AssetRegistrySearchable, Category = "Test")
	bool bKeeper = false;

	/** Gameplay-tag names, comma separated. */
	UPROPERTY(EditAnywhere, AssetRegistrySearchable, Category = "Test")
	FString Roles;
};

/** An asset of another class, which a view over UContentViewsTestRow must leave out. */
UCLASS(HideDropdown, NotPlaceable)
class UContentViewsTestOther : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, AssetRegistrySearchable, Category = "Test")
	float Speed = 0.f;
};
