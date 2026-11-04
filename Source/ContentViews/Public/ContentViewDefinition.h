#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ContentViewDefinition.generated.h"

/** How a view lays its rows out. */
UENUM(BlueprintType)
enum class EContentViewMode : uint8
{
	/** One row per asset, a column per field. */
	List,

	/** Thumbnails. */
	Tiles,
};

UENUM(BlueprintType)
enum class EContentViewFilterOp : uint8
{
	Equals UMETA(DisplayName = "="),
	NotEquals UMETA(DisplayName = "!="),
	AtLeast UMETA(DisplayName = ">="),
	AtMost UMETA(DisplayName = "<="),

	/** The column's words hold Value, ignoring case. */
	Contains UMETA(DisplayName = "contains"),

	/** The column holds the gameplay tag Value, or a tag under it (Role holds Role.Striker). */
	HasTag UMETA(DisplayName = "has tag"),
};

/** One condition a row must meet: Column Operator Value, such as Pace >= 80. */
USTRUCT(BlueprintType)
struct CONTENTVIEWS_API FContentViewFilter
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content Views")
	FName Column;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content Views")
	EContentViewFilterOp Operator = EContentViewFilterOp::Equals;

	/** Compared with the column's value: a number for >= and <=. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content Views")
	FString Value;

	/** "Pace >= 80". */
	FString Describe() const;
};

USTRUCT(BlueprintType)
struct CONTENTVIEWS_API FContentViewSort
{
	GENERATED_BODY()

	/** None sorts by Name. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content Views")
	FName Column;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content Views")
	bool bDescending = false;
};

/** A view definition as plain data: what a view asset exports as JSON. */
USTRUCT(BlueprintType)
struct CONTENTVIEWS_API FContentViewSpec
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Content Views")
	FString DisplayName;

	/** A brush in the editor's style set (ClassIcon.Character). */
	UPROPERTY(BlueprintReadWrite, Category = "Content Views")
	FName Icon;

	/** The class whose assets are the rows, as a path (/Script/Module.Class or a Blueprint's generated class). */
	UPROPERTY(BlueprintReadWrite, Category = "Content Views")
	FString RowClass;

	UPROPERTY(BlueprintReadWrite, Category = "Content Views")
	bool bIncludeSubclasses = true;

	UPROPERTY(BlueprintReadWrite, Category = "Content Views")
	TArray<FName> Columns;

	UPROPERTY(BlueprintReadWrite, Category = "Content Views")
	FContentViewSort DefaultSort;

	UPROPERTY(BlueprintReadWrite, Category = "Content Views")
	TArray<FContentViewFilter> Filters;

	UPROPERTY(BlueprintReadWrite, Category = "Content Views")
	EContentViewMode Mode = EContentViewMode::List;
};

/**
 * A way to browse content: which assets are its rows, which columns it shows, its sort, its filters, and list or tiles.
 * Columns read registry tags or registered computed columns, so a view never loads its rows. Make one in the editor and
 * it appears in Window > Content Views and as a filter in every Content Browser.
 */
UCLASS(BlueprintType, meta = (ShortTooltip = "A way to browse content: rows, columns, sort and filters."))
class CONTENTVIEWS_API UContentViewDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** What the view is called in the Content Views tab and the filter menu. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, AssetRegistrySearchable, Category = "View")
	FText DisplayName;

	/** A brush in the editor's style set, such as ClassIcon.Character. None shows the data asset icon. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, AssetRegistrySearchable, Category = "View")
	FName Icon;

	/** The class whose assets are the rows. Blueprints count by their parent class. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rows", meta = (AllowAbstract = "true"))
	TSoftClassPtr<UObject> RowClass;

	/** Whether assets of subclasses and Blueprint children are rows too. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rows")
	bool bIncludeSubclasses = true;

	/**
	 * The columns, left to right, by name: a registry tag on the rows (an AssetRegistrySearchable field, or a tag from
	 * GetAssetRegistryTags), or a computed column a module registered. Name, Path, Plugin and Class are always there.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Columns")
	TArray<FName> Columns;

	/** The order rows open in. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Columns")
	FContentViewSort DefaultSort;

	/** Every condition a row must meet. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rows")
	TArray<FContentViewFilter> Filters;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "View")
	EContentViewMode Mode = EContentViewMode::List;

	/** The type half of every view's id. */
	static FPrimaryAssetType StaticAssetType();

	virtual FPrimaryAssetId GetPrimaryAssetId() const override;

	/** The name shown for the view: DisplayName, else the asset's name. */
	FText GetViewName() const;

	FContentViewSpec ToSpec() const;

	/** ToSpec as JSON text. */
	FString ExportJson() const;

#if WITH_EDITOR
	/** Refuses an empty row class or column list, a column nothing provides, a bad filter and a sort on no column. */
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
