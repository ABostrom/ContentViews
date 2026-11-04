#pragma once

#include "CoreMinimal.h"
#include "Elements/Interfaces/TypedElementDataStorageFactory.h"
#include "Elements/Interfaces/TypedElementDataStorageUiInterface.h"
#include "ContentViewsTedsFactory.generated.h"

/**
 * Registers the backend with TEDS: the processor that notes changed asset rows, the Content Source host widget and the
 * cell widget. Ordered after the engine's factories so its host replaces the stock one (which has no filter bar).
 */
UCLASS()
class UContentViewsTedsFactory : public UEditorDataStorageFactory
{
	GENERATED_BODY()

public:
	virtual uint8 GetOrder() const override { return 200; }
	virtual void RegisterQueries(UE::Editor::DataStorage::ICoreProvider& DataStorage) override;
	virtual void RegisterWidgetConstructors(UE::Editor::DataStorage::ICoreProvider& DataStorage,
		UE::Editor::DataStorage::IUiProvider& DataStorageUi) const override;
	virtual void PostRegister(UE::Editor::DataStorage::ICoreProvider& DataStorage) override;
	virtual void PreShutdown(UE::Editor::DataStorage::ICoreProvider& DataStorage) override;
};

/**
 * The widget the Content Browser shows for an active Content Source (purpose ContentBrowser.AssetView). A Content View's
 * source gets its search box, filter bar and table or tiles; any other source gets what the engine's host gives it.
 */
USTRUCT()
struct FContentViewsAssetViewConstructor : public FSimpleWidgetConstructor
{
	GENERATED_BODY()

	FContentViewsAssetViewConstructor() : FSimpleWidgetConstructor(StaticStruct()) {}

	virtual TSharedPtr<SWidget> CreateWidget(UE::Editor::DataStorage::ICoreProvider* DataStorage,
		UE::Editor::DataStorage::IUiProvider* DataStorageUi, UE::Editor::DataStorage::RowHandle TargetRow,
		UE::Editor::DataStorage::RowHandle WidgetRow, const UE::Editor::DataStorage::FMetaDataView& Arguments) override;
};

/** A cell of a view's number, computed text or registry tag column: the words the value shows. */
USTRUCT()
struct FContentViewsCellConstructor : public FSimpleWidgetConstructor
{
	GENERATED_BODY()

	FContentViewsCellConstructor() : FSimpleWidgetConstructor(StaticStruct()) {}

	virtual TSharedPtr<SWidget> CreateWidget(UE::Editor::DataStorage::ICoreProvider* DataStorage,
		UE::Editor::DataStorage::IUiProvider* DataStorageUi, UE::Editor::DataStorage::RowHandle TargetRow,
		UE::Editor::DataStorage::RowHandle WidgetRow, const UE::Editor::DataStorage::FMetaDataView& Arguments) override;
};

namespace UE::ContentViews::Teds
{
	/** Registers the cell widget for the column Template generates under Identifier. */
	void RegisterContentViewCell(UE::Editor::DataStorage::IUiProvider& Ui, const UScriptStruct& Template, FName Identifier);
}
