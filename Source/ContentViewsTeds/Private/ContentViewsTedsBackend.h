#pragma once

#include "CoreMinimal.h"
#include "ContentSources/IContentSource.h"
#include "ContentViewColumns.h"
#include "ContentViewDefinition.h"
#include "DataStorage/Handles.h"
#include "Elements/Framework/TypedElementQueryBuilder.h"

struct FAssetData;
struct FPropertyChangedEvent;

namespace UE::Editor::DataStorage
{
	class ICoreProvider;
	class IUiProvider;
}

namespace UE::ContentViews::Teds
{
	using UE::Editor::DataStorage::ICoreProvider;
	using UE::Editor::DataStorage::IUiProvider;
	using UE::Editor::DataStorage::QueryHandle;
	using UE::Editor::DataStorage::RowHandle;

	/** Which TEDS column a view column fills. */
	enum class EColumnKind : uint8
	{
		/** Name: TedsAssetData's own FAssetNameColumn. */
		AssetName,
		/** FContentViewNumberColumn under the column's name: sorts as a number. */
		Number,
		/** FContentViewStringColumn under the column's name: a computed column's value and words. */
		Text,
		/** FItemStringAttributeColumn_Experimental under the tag's name: a registry tag read as it is. */
		Tag,
		/** FContentViewColourColumn under the column's name, drawn as a colour chip. */
		Colour,
	};

	struct FColumnSlot
	{
		FName Name;
		/** The header: the view's label for the column, else its name in words. */
		FString Label;
		TSharedPtr<const IContentViewColumn> Reader;
		EColumnKind Kind = EColumnKind::Tag;
		bool bComputed = false;
		/** The column type the table shows. */
		const UScriptStruct* Type = nullptr;
	};

	/** One view asset as the backend holds it. */
	struct FViewState
	{
		FSoftObjectPath Path;
		TWeakObjectPtr<UContentViewDefinition> View;
		TWeakObjectPtr<UClass> RowClass;
		bool bIncludeSubclasses = true;
		/** The tag every row of the view carries. */
		const UScriptStruct* RowTag = nullptr;
		TArray<FColumnSlot> Columns;
		/** Rows of the view: Select AssetData Where All RowTag. */
		QueryHandle RowQuery = UE::Editor::DataStorage::InvalidQueryHandle;

		bool HasComputed() const { return Columns.ContainsByPredicate([](const FColumnSlot& Slot) { return Slot.bComputed; }); }
		TArray<TWeakObjectPtr<const UScriptStruct>> ShownColumns() const;
		const FColumnSlot* FindColumn(FName Name) const { return Columns.FindByPredicate([Name](const FColumnSlot& Slot) { return Slot.Name == Name; }); }
		UE::Editor::DataStorage::FQueryDescription MakeRowQuery() const;
	};

	/** A view's Content Source. The factory makes one per Content Browser, so several can stand for one view. */
	class FContentViewSource final : public UE::Editor::ContentBrowser::IContentSource
	{
	public:
		explicit FContentViewSource(const FSoftObjectPath& InPath) : Path(InPath) {}

		virtual FName GetName() override;
		virtual FText GetDisplayName() override;
		virtual FSlateIcon GetIcon() override;
		virtual void GetAssetViewInitParams(UE::Editor::ContentBrowser::FTableViewerInitParams& OutInitParams) override;

		const FSoftObjectPath Path;
	};

	/**
	 * The TEDS backend. TedsAssetData mirrors the asset registry into asset rows; a processor gated on FUpdatedAssetDataTag
	 * notes the rows that changed, and a cooperative task tags each noted row with the views it belongs to and writes its
	 * columns. Computed columns read other assets (a team's roster), so any change queues a recount of them, also cooperative.
	 */
	class FBackend
	{
	public:
		static FBackend& Get();

		/** Called by the factory once TEDS has registered every factory. */
		void Start(ICoreProvider& InStorage, IUiProvider& InUi);
		void Stop();

		/** The backend runs and the setting asks for it. */
		bool IsActive() const;
		bool IsCaughtUp() const;

		/** From the processor: rows whose asset data was added or changed this frame. */
		void NoteUpdatedRows(TConstArrayView<RowHandle> Rows);

		const FViewState* FindView(const FSoftObjectPath& Path) const;
		bool HasSource(const FSoftObjectPath& Path) const { return Registered.Contains(Path); }

		/** The view the source stands for, when it is one of this backend's. */
		const FViewState* FindViewOfSource(const UE::Editor::ContentBrowser::IContentSource* Source) const;

		TArray<RowHandle> RowsOf(const FViewState& View) const;

		/** A view filter as the predicate the filter bar runs: the legacy backend's own test on the row's asset data. */
		UE::Editor::DataStorage::Queries::TConstQueryFunction<bool> MakeValueFilter(const FContentViewFilter& Filter) const;

		/** Read back a row's value in a column, in the legacy backend's types. */
		FContentViewValue ReadValue(RowHandle Row, const FColumnSlot& Slot) const;

		ICoreProvider* GetStorage() const { return Storage; }
		IUiProvider* GetUi() const { return Ui; }

		/** A view was rebuilt (its asset changed) or removed. */
		DECLARE_MULTICAST_DELEGATE_OneParam(FOnViewChanged, const FSoftObjectPath&);
		FOnViewChanged& OnViewChanged() { return ViewChanged; }

		/** Every source the factory has handed out, for FindViewOfSource. */
		TArray<TWeakPtr<FContentViewSource>> Sources;

	private:
		void ApplySetting();
		void RefreshViews();
		void BuildView(UContentViewDefinition& View);
		void DropView(const FSoftObjectPath& Path);
		void RegisterSource(const FViewState& View);
		void QueueAllAssetRows();
		void CooperativeUpdate(FTimespan Allowance);
		void UpdateRow(RowHandle Row, bool bComputedOnly);
		void WriteValue(RowHandle Row, const FColumnSlot& Slot, const FContentViewValue& Value);
		void SetAlert(RowHandle Row, const FColumnSlot& Slot, const FString& Problem);
		void EnsureCellWidget(const UScriptStruct& Template, FName Identifier);

		void HandleAssetAdded(const FAssetData& Asset);
		void HandleAssetRemoved(const FAssetData& Asset);
		void HandleObjectChanged(UObject* Object, FPropertyChangedEvent& Event);

		ICoreProvider* Storage = nullptr;
		IUiProvider* Ui = nullptr;
		QueryHandle AllAssetsQuery = UE::Editor::DataStorage::InvalidQueryHandle;
		TMap<FSoftObjectPath, FViewState> Views;
		TSet<FSoftObjectPath> Registered;
		TSet<RowHandle> Pending;
		TSet<RowHandle> PendingComputed;
		bool bComputedDirty = false;
		bool bShowedSourceBar = false;
		TSet<TPair<RowHandle, FName>> Alerted;
		TSet<TPair<const UScriptStruct*, FName>> CellWidgets;
		FOnViewChanged ViewChanged;
	};
}
