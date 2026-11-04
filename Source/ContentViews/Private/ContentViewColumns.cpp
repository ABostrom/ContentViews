#include "ContentViewColumns.h"

#include "AssetRegistry/AssetData.h"

namespace
{
	/** A column from a function: what MakeColumn returns. */
	class FContentViewFunctionColumn final : public IContentViewColumn
	{
	public:
		FContentViewFunctionColumn(TOptional<EContentViewValueType> InType,
			TFunction<FContentViewValue(const FAssetData&)> InRead, FText InTooltip, EContentViewColumnShows InShows)
			: Type(InType), Read(MoveTemp(InRead)), Tooltip(MoveTemp(InTooltip)), Shows(InShows)
		{
		}

		virtual TOptional<EContentViewValueType> GetType() const override { return Type; }
		virtual FContentViewValue GetValue(const FAssetData& Asset) const override { return Read(Asset); }
		virtual FText GetTooltip() const override { return Tooltip; }
		virtual EContentViewColumnShows GetShows() const override { return Shows; }

	private:
		TOptional<EContentViewValueType> Type;
		TFunction<FContentViewValue(const FAssetData&)> Read;
		FText Tooltip;
		EContentViewColumnShows Shows;
	};

	/** The registry tag of the column's own name. */
	class FContentViewTagColumn final : public IContentViewColumn
	{
	public:
		explicit FContentViewTagColumn(FName InTag) : Tag(InTag) {}

		virtual FContentViewValue GetValue(const FAssetData& Asset) const override
		{
			return UE::ContentViews::ReadTag(Asset, Tag);
		}

	private:
		FName Tag;
	};

	TMap<FName, TSharedRef<const IContentViewColumn>>& ContentViewColumnTable()
	{
		static TMap<FName, TSharedRef<const IContentViewColumn>> Table;
		return Table;
	}
}

namespace UE::ContentViews
{
	void RegisterColumn(FName Name, TSharedRef<const IContentViewColumn> Column)
	{
		check(IsInGameThread());
		ContentViewColumnTable().Add(Name, MoveTemp(Column));
	}

	void UnregisterColumn(FName Name)
	{
		check(IsInGameThread());
		ContentViewColumnTable().Remove(Name);
	}

	bool IsColumnRegistered(FName Name)
	{
		return ContentViewColumnTable().Contains(Name);
	}

	TSharedRef<const IContentViewColumn> GetColumn(FName Name)
	{
		if (const TSharedRef<const IContentViewColumn>* Found = ContentViewColumnTable().Find(Name))
		{
			return *Found;
		}
		return MakeShared<FContentViewTagColumn>(Name);
	}

	TSharedRef<const IContentViewColumn> MakeColumn(TOptional<EContentViewValueType> Type,
		TFunction<FContentViewValue(const FAssetData&)> Read, FText Tooltip, EContentViewColumnShows Shows)
	{
		return MakeShared<FContentViewFunctionColumn>(Type, MoveTemp(Read), MoveTemp(Tooltip), Shows);
	}

	FContentViewValue ReadTag(const FAssetData& Asset, FName Tag)
	{
		const FAssetTagValueRef Found = Asset.TagsAndValues.FindTag(Tag);
		if (!Found.IsSet())
		{
			return FContentViewValue();
		}
		// A plain string can read back as empty text, so only text with words is taken as text.
		FText Text;
		if (Found.TryGetAsText(Text) && !Text.IsEmpty())
		{
			return FContentViewValue::OfString(Text.ToString());
		}
		return FContentViewValue::FromTagString(Found.AsString());
	}

	TArray<FString> SplitList(const FString& Raw)
	{
		TArray<FString> Items;
		Raw.ParseIntoArray(Items, TEXT(","), /*bCullEmpty*/ true);
		for (FString& Item : Items)
		{
			Item.TrimStartAndEndInline();
		}
		Items.RemoveAll([](const FString& Item) { return Item.IsEmpty(); });
		return Items;
	}

	FString JoinList(TConstArrayView<FString> Items)
	{
		return FString::Join(Items, TEXT(","));
	}
}
