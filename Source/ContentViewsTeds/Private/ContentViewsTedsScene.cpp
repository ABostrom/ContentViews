// Scene Cost, the Join archetype: placed static meshes counted from TEDS's component rows, joined with their asset rows.
#include "ContentViewsTedsBackend.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "ContentViewQuery.h"
#include "ContentViewsTedsColumns.h"
#include "ContentViewsTedsFactory.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DataStorage/Features.h"
#include "Editor.h"
#include "Elements/Columns/TypedElementCompatibilityColumns.h"
#include "Elements/Interfaces/TypedElementDataStorageCompatibilityInterface.h"
#include "Elements/Interfaces/TypedElementDataStorageInterface.h"
#include "Elements/Interfaces/TypedElementDataStorageUiInterface.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "IContentBrowserSingleton.h"
#include "InstancedStaticMeshDelegates.h"
#include "Modules/ModuleManager.h"
#include "SceneCostView.h"
#include "TedsAlertColumns.h"
#include "TedsAssetDataColumns.h"

#define LOCTEXT_NAMESPACE "ContentViewsTeds"

namespace UE::ContentViews::Teds
{
	using namespace UE::Editor::DataStorage;
	using namespace UE::Editor::DataStorage::Queries;

	namespace
	{
		/** A recount waits this long after the last change, so a burst of edits (PCG writing a cell) counts once. */
		constexpr double SceneSettleSeconds = 0.1;

		UWorld* EditorWorld()
		{
			return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		}

		/** 1,234,567 */
		FString Grouped(int64 Value)
		{
			return FText::AsNumber(Value).ToString();
		}

		/** 2.4 MB */
		FString Bytes(int64 Value)
		{
			return Value > 0 ? FText::AsMemory(Value, EMemoryUnitStandard::SI).ToString() : FString(TEXT("-"));
		}

		/** The mesh's facts from its TEDS asset row (the join), or from the loaded mesh when the row lacks a tag. */
		void ReadMeshFacts(ICoreProvider& Storage, const FSoftObjectPath& Path, FBackend::FSceneMesh& Out)
		{
			FAssetData Asset;
			const RowHandle AssetRow = Storage.LookupMappedRow(UE::Editor::AssetData::MappingDomain, FMapKey(Path));
			if (const FAssetDataColumn_Experimental* Column = Storage.GetColumn<FAssetDataColumn_Experimental>(AssetRow))
			{
				Asset = Column->AssetData;
			}
			Out.Name = Path.GetAssetName();
			const FContentViewValue Nanite = ReadTag(Asset, TEXT("NaniteEnabled"));
			const FContentViewValue NaniteTriangles = ReadTag(Asset, TEXT("NaniteTriangles"));
			const FContentViewValue Triangles = ReadTag(Asset, TEXT("Triangles"));
			const UStaticMesh* Loaded = Cast<UStaticMesh>(Path.ResolveObject());
			Out.bNanite = Nanite.bIsSet ? Nanite.bBool : Loaded && Loaded->IsNaniteEnabled();
			if (Out.bNanite)
			{
				Out.Triangles = NaniteTriangles.bIsSet && NaniteTriangles.Number > 0.0
					? static_cast<int64>(NaniteTriangles.Number) : (Loaded ? Loaded->GetNumNaniteTriangles() : 0);
			}
			else
			{
				Out.Triangles = Triangles.bIsSet ? static_cast<int64>(Triangles.Number) : (Loaded ? Loaded->GetNumTriangles(0) : 0);
			}
			const FContentViewValue Memory = ReadTag(Asset, Out.bNanite ? TEXT("EstNaniteCompressedSize") : TEXT("EstTotalCompressedSize"));
			Out.Memory = Memory.bIsSet ? static_cast<int64>(Memory.Number) : 0;
		}

		FString CellOf(const AActor& Actor)
		{
			const UWorld* World = Actor.GetWorld();
			if (!World || !World->IsPartitionedWorld())
			{
				return TEXT("-");
			}
			if (!Actor.GetIsSpatiallyLoaded())
			{
				return TEXT("Always loaded");
			}
			const FName Grid = Actor.GetRuntimeGrid();
			return Grid.IsNone() ? FString(TEXT("Main grid")) : Grid.ToString();
		}
	}

	FQueryDescription FSceneViewState::MakeRowQuery() const
	{
		return Select().Where().All<FContentViewSceneMeshTag>().All(RowTag).Compile();
	}

	TArray<TWeakObjectPtr<const UScriptStruct>> FBackend::SceneShownColumns() const
	{
		TArray<TWeakObjectPtr<const UScriptStruct>> Shown;
		for (const FColumnSlot& Slot : SceneSlots)
		{
			Shown.Add(Slot.Type);
		}
		Shown.Add(FTedsAlertColumn::StaticStruct());
		return Shown;
	}

	const FColumnSlot* FBackend::FindSceneSlot(FName Name) const
	{
		return SceneSlots.FindByPredicate([Name](const FColumnSlot& Slot) { return Slot.Name == Name; });
	}

	void FBackend::StartScene()
	{
		SceneTable = Storage->FindTable(SceneTableName);
		PlacementQuery = Storage->RegisterQuery(Select().ReadOnly<FContentViewPlacementColumn>().Compile());
		// The engine's component rows carry this tag; it is not exported, so it is found by name. Without it every UObject
		// row is looked at once, which costs a pass but finds the same components.
		const UScriptStruct* ComponentTag = FindObject<UScriptStruct>(nullptr, TEXT("/Script/TedsActorCompatibility.ActorComponentTypeTag"));
		ComponentQuery = Storage->RegisterQuery(ComponentTag
			? Select().ReadOnly<FTypedElementUObjectColumn>().Where().All(ComponentTag).Compile()
			: Select().ReadOnly<FTypedElementUObjectColumn>().Compile());

		const auto Slot = [this](FName Name, const TCHAR* Label, EColumnKind Kind, bool bWide = false)
		{
			FColumnSlot& New = SceneSlots.AddDefaulted_GetRef();
			New.Name = Name;
			New.Label = Label;
			New.Kind = Kind;
			const UScriptStruct* Template = Kind == EColumnKind::Number ? FContentViewNumberColumn::StaticStruct() : FContentViewStringColumn::StaticStruct();
			New.Type = Storage->GenerateDynamicColumn({.TemplateType = Template, .Identifier = Name});
			const_cast<UScriptStruct*>(New.Type)->SetMetaData(TEXT("DisplayName"), Label);
			if (bWide)
			{
				const_cast<UScriptStruct*>(New.Type)->SetMetaData(WideColumnMeta, TEXT("true"));
			}
			EnsureCellWidget(*Template, Name);
		};
		Slot(SceneMeshColumn, TEXT("Mesh"), EColumnKind::Text, /*bWide*/ true);
		Slot(TEXT("Placed"), TEXT("Placed"), EColumnKind::Number);
		Slot(TEXT("Triangles"), TEXT("Triangles"), EColumnKind::Number);
		Slot(SceneTrianglesColumn, TEXT("Scene Tris"), EColumnKind::Number);
		Slot(TEXT("Nanite"), TEXT("Nanite"), EColumnKind::Text);
		Slot(TEXT("EstMemory"), TEXT("Est. Memory"), EColumnKind::Number);

		FInstancedStaticMeshDelegates::OnInstanceIndexUpdated.AddRaw(this, &FBackend::HandleInstancesChanged);
		if (GEngine)
		{
			GEngine->OnLevelActorAdded().AddRaw(this, &FBackend::HandleActorChanged);
			GEngine->OnLevelActorDeleted().AddRaw(this, &FBackend::HandleActorChanged);
		}
		FEditorDelegates::MapChange.AddRaw(this, &FBackend::HandleMapChange);
		FWorldDelegates::LevelAddedToWorld.AddRaw(this, &FBackend::HandleLevelChanged);
		FWorldDelegates::LevelRemovedFromWorld.AddRaw(this, &FBackend::HandleLevelChanged);
	}

	void FBackend::StopScene()
	{
		FInstancedStaticMeshDelegates::OnInstanceIndexUpdated.RemoveAll(this);
		if (GEngine)
		{
			GEngine->OnLevelActorAdded().RemoveAll(this);
			GEngine->OnLevelActorDeleted().RemoveAll(this);
		}
		FEditorDelegates::MapChange.RemoveAll(this);
		FWorldDelegates::LevelAddedToWorld.RemoveAll(this);
		FWorldDelegates::LevelRemovedFromWorld.RemoveAll(this);
		Storage->UnregisterQuery(PlacementQuery);
		Storage->UnregisterQuery(ComponentQuery);
	}

	bool FBackend::IsCountingScene() const
	{
		return IsActive() && !SceneViews.IsEmpty();
	}

	bool FBackend::IsSceneCaughtUp() const
	{
		return PendingComponents.IsEmpty() && PendingInstanced.IsEmpty() && !bSceneDirty;
	}

	void FBackend::NoteComponentRows(TConstArrayView<RowHandle> Rows)
	{
		if (IsCountingScene())
		{
			PendingComponents.Append(Rows);
		}
	}

	void FBackend::NoteSceneChanged()
	{
		if (IsCountingScene())
		{
			bSceneDirty = true;
			SceneChangedAt = FPlatformTime::Seconds();
		}
	}

	void FBackend::QueueAllComponentRows()
	{
		Storage->RunQuery(ComponentQuery, CreateDirectQueryCallbackBinding([this](IDirectQueryContext&, RowHandle Row)
		{
			PendingComponents.Add(Row);
		}));
		NoteSceneChanged();
	}

	const FSceneViewState* FBackend::FindSceneView(const FSoftObjectPath& Path) const
	{
		return SceneViews.Find(Path);
	}

	TArray<RowHandle> FBackend::RowsOf(const FSceneViewState& View) const
	{
		TArray<RowHandle> Rows;
		View.Rows.GenerateValueArray(Rows);
		return Rows;
	}

	void FBackend::BuildSceneView(USceneCostView& View)
	{
		const FSoftObjectPath Path(&View);
		if (FSceneViewState* Existing = SceneViews.Find(Path))
		{
			// A budget edit: the rows stay, their alerts are written again on the next count.
			Existing->View = &View;
			NoteSceneChanged();
			ViewChanged.Broadcast(Path);
			return;
		}
		const bool bFirst = SceneViews.IsEmpty();
		FSceneViewState& State = SceneViews.Add(Path);
		State.Path = Path;
		State.View = &View;
		State.RowTag = Storage->GenerateDynamicColumn({.TemplateType = FContentViewRowTag::StaticStruct(), .Identifier = FName(Path.ToString())});
		RegisterSource(Path);
		if (bFirst)
		{
			// Components synced before any Scene Cost view existed were not noted; look at each once.
			QueueAllComponentRows();
		}
		NoteSceneChanged();
	}

	void FBackend::DropSceneView(const FSoftObjectPath& Path)
	{
		if (FSceneViewState* State = SceneViews.Find(Path))
		{
			for (const TPair<FSoftObjectPath, RowHandle>& Row : State->Rows)
			{
				Storage->RemoveRow(Row.Value);
			}
			SceneViews.Remove(Path);
		}
		if (SceneViews.IsEmpty())
		{
			PendingComponents.Reset();
			PendingInstanced.Reset();
			bSceneDirty = false;
		}
		if (Registered.Remove(Path) > 0 && FModuleManager::Get().IsModuleLoaded("ContentBrowser"))
		{
			IContentBrowserSingleton::Get().UnregisterContentSourceFactory(FName(Path.ToString()));
		}
		ViewChanged.Broadcast(Path);
	}

	void FBackend::UpdateScene(double End)
	{
		if (!IsCountingScene())
		{
			return;
		}
		if (!PendingInstanced.IsEmpty())
		{
			// An instanced component whose instances changed: PCG adds and clears instances without Modify, so no sync tag
			// reaches its row. A component TEDS has no row for yet is registered, as the engine registers an actor's.
			ICompatibilityProvider* Compat = GetMutableDataStorageFeature<ICompatibilityProvider>(CompatibilityFeatureName);
			for (const TWeakObjectPtr<UInstancedStaticMeshComponent>& Weak : PendingInstanced)
			{
				UInstancedStaticMeshComponent* Component = Weak.Get();
				if (!Compat || !Component)
				{
					continue;
				}
				const RowHandle Row = Compat->FindRowWithCompatibleObjectExplicit(Component);
				if (Storage->IsRowAvailable(Row))
				{
					PendingComponents.Add(Row);
				}
				else
				{
					Compat->AddCompatibleObjectExplicit(Component);
				}
			}
			PendingInstanced.Reset();
		}
		while (!PendingComponents.IsEmpty() && FPlatformTime::Seconds() < End)
		{
			TSet<RowHandle>::TIterator It = PendingComponents.CreateIterator();
			const RowHandle Row = *It;
			It.RemoveCurrent();
			SyncPlacement(Row);
		}
		if (PendingComponents.IsEmpty() && bSceneDirty && FPlatformTime::Seconds() - SceneChangedAt >= SceneSettleSeconds)
		{
			bSceneDirty = false;
			CountScene();
		}
	}

	void FBackend::SyncPlacement(RowHandle Row)
	{
		if (!Storage->IsRowAvailable(Row))
		{
			return;
		}
		const FTypedElementUObjectColumn* Object = Storage->GetColumn<FTypedElementUObjectColumn>(Row);
		UStaticMeshComponent* Component = Object ? Cast<UStaticMeshComponent>(Object->Object.Get()) : nullptr;
		const UStaticMesh* Mesh = Component ? Component->GetStaticMesh() : nullptr;
		UWorld* World = Component ? Component->GetWorld() : nullptr;
		AActor* Owner = Component ? Component->GetOwner() : nullptr;
		const UInstancedStaticMeshComponent* Instanced = Cast<UInstancedStaticMeshComponent>(Component);
		const int32 Instances = Instanced ? Instanced->GetInstanceCount() : 1;
		const bool bPlaces = Mesh && World && World->WorldType == EWorldType::Editor && IsValid(Owner) && !Component->IsEditorOnly()
			&& Instances > 0;
		FContentViewPlacementColumn* Placement = Storage->GetColumn<FContentViewPlacementColumn>(Row);
		if (!bPlaces)
		{
			if (Placement)
			{
				Storage->RemoveColumn(Row, FContentViewPlacementColumn::StaticStruct());
				NoteSceneChanged();
			}
			return;
		}
		FContentViewPlacementColumn Now;
		Now.Mesh = FSoftObjectPath(Mesh);
		Now.Instances = Instances;
		Now.bInstanced = Instanced != nullptr;
		Now.bHierarchical = Cast<UHierarchicalInstancedStaticMeshComponent>(Component) != nullptr;
		Now.World = World;
		Now.Owner = Owner;
		if (Placement)
		{
			*Placement = MoveTemp(Now);
		}
		else
		{
			Storage->AddColumn(Row, MoveTemp(Now));
		}
		NoteSceneChanged();
	}

	void FBackend::CountScene()
	{
		UWorld* World = EditorWorld();
		TArray<RowHandle> Rows;
		Storage->RunQuery(PlacementQuery, CreateDirectQueryCallbackBinding([&Rows](IDirectQueryContext&, RowHandle Row)
		{
			Rows.Add(Row);
		}));

		SceneMeshes.Reset();
		for (const RowHandle Row : Rows)
		{
			const FContentViewPlacementColumn* Placement = Storage->GetColumn<FContentViewPlacementColumn>(Row);
			// A deleted actor's components keep their rows until garbage collection: its owner is no longer valid.
			if (Placement && World && Placement->World.Get() == World && IsValid(Placement->Owner.Get()))
			{
				FSceneMesh& Mesh = SceneMeshes.FindOrAdd(Placement->Mesh);
				Mesh.Placed += Placement->Instances;
				++Mesh.Components;
			}
		}

		SceneTotals = FSceneTotals();
		SceneTotals.Level = World ? World->GetMapName() : FString();
		SceneTotals.LoadedActors = World ? World->GetActorCount() : 0;
		for (TPair<FSoftObjectPath, FSceneMesh>& Pair : SceneMeshes)
		{
			ReadMeshFacts(*Storage, Pair.Key, Pair.Value);
			const int64 SceneTriangles = Pair.Value.Placed * Pair.Value.Triangles;
			++SceneTotals.Meshes;
			SceneTotals.Instances += Pair.Value.Placed;
			SceneTotals.Components += Pair.Value.Components;
			SceneTotals.SceneTriangles += SceneTriangles;
			SceneTotals.NonNaniteTriangles += Pair.Value.bNanite ? 0 : SceneTriangles;
		}
		for (TPair<FSoftObjectPath, FSceneViewState>& View : SceneViews)
		{
			WriteSceneRows(View.Value);
		}
		SceneCounted.Broadcast();
	}

	void FBackend::WriteSceneRows(FSceneViewState& View)
	{
		const USceneCostView* Asset = View.View.Get();
		const int64 Budget = Asset ? Asset->TriangleBudget : 0;
		for (auto It = View.Rows.CreateIterator(); It; ++It)
		{
			if (!SceneMeshes.Contains(It->Key))
			{
				Storage->RemoveRow(It->Value);
				It.RemoveCurrent();
			}
		}
		for (const TPair<FSoftObjectPath, FSceneMesh>& Pair : SceneMeshes)
		{
			RowHandle& Row = View.Rows.FindOrAdd(Pair.Key, InvalidRowHandle);
			if (!Storage->IsRowAvailable(Row))
			{
				Row = Storage->AddRow(SceneTable);
				Storage->AddColumn(Row, View.RowTag);
			}
			const FSceneMesh& Mesh = Pair.Value;
			const int64 SceneTriangles = Mesh.Placed * Mesh.Triangles;
			const auto Number = [](int64 Value, FString Display)
			{
				FContentViewValue Out = FContentViewValue::OfNumber(static_cast<double>(Value));
				Out.Display = MoveTemp(Display);
				return Out;
			};
			FContentViewValue Scene = Number(SceneTriangles, Grouped(SceneTriangles));
			if (!Mesh.bNanite && Budget > 0 && SceneTriangles > Budget)
			{
				Scene.Problem = FText::Format(LOCTEXT("OverBudget", "{0} in the scene, over the budget of {1}; not Nanite"),
					FText::AsNumber(SceneTriangles), FText::AsNumber(Budget)).ToString();
			}
			WriteValue(Row, *FindSceneSlot(SceneMeshColumn), FContentViewValue::OfString(Pair.Key.ToString(), Mesh.Name));
			WriteValue(Row, *FindSceneSlot(TEXT("Placed")), Number(Mesh.Placed, Grouped(Mesh.Placed)));
			WriteValue(Row, *FindSceneSlot(TEXT("Triangles")), Number(Mesh.Triangles, Grouped(Mesh.Triangles)));
			WriteValue(Row, *FindSceneSlot(SceneTrianglesColumn), Scene);
			WriteValue(Row, *FindSceneSlot(TEXT("Nanite")), FContentViewValue::OfString(Mesh.bNanite ? TEXT("Yes") : TEXT("No")));
			WriteValue(Row, *FindSceneSlot(TEXT("EstMemory")), Number(Mesh.Memory, Bytes(Mesh.Memory)));
		}
	}

	TArray<FScenePlacement> FBackend::PlacementsOf(const FSoftObjectPath& Mesh) const
	{
		TArray<FScenePlacement> Found;
		UWorld* World = EditorWorld();
		TArray<RowHandle> Rows;
		Storage->RunQuery(PlacementQuery, CreateDirectQueryCallbackBinding([&Rows](IDirectQueryContext&, RowHandle Row)
		{
			Rows.Add(Row);
		}));
		for (const RowHandle Row : Rows)
		{
			const FContentViewPlacementColumn* Placement = Storage->GetColumn<FContentViewPlacementColumn>(Row);
			const FTypedElementUObjectColumn* Object = Storage->GetColumn<FTypedElementUObjectColumn>(Row);
			AActor* Owner = Placement ? Placement->Owner.Get() : nullptr;
			UStaticMeshComponent* Component = Object ? Cast<UStaticMeshComponent>(Object->Object.Get()) : nullptr;
			if (!Placement || Placement->Mesh != Mesh || Placement->World.Get() != World || !IsValid(Owner) || !Component)
			{
				continue;
			}
			FScenePlacement& Out = Found.AddDefaulted_GetRef();
			Out.Actor = Owner;
			Out.Component = Component;
			Out.ActorLabel = Owner->GetActorLabel();
			Out.ComponentName = Component->GetName();
			Out.Kind = Placement->bHierarchical ? TEXT("Hierarchical instanced") : (Placement->bInstanced ? TEXT("Instanced") : TEXT("Static"));
			Out.Instances = Placement->Instances;
			Out.Cell = CellOf(*Owner);
		}
		Found.Sort([](const FScenePlacement& A, const FScenePlacement& B)
		{
			return A.Instances != B.Instances ? A.Instances > B.Instances : A.ActorLabel < B.ActorLabel;
		});
		return Found;
	}

	void FBackend::HandleInstancesChanged(UInstancedStaticMeshComponent* Component,
		TArrayView<const FInstancedStaticMeshDelegates::FInstanceIndexUpdateData> Updates)
	{
		const UWorld* World = Component ? Component->GetWorld() : nullptr;
		if (IsCountingScene() && World && World->WorldType == EWorldType::Editor)
		{
			PendingInstanced.Add(Component);
			NoteSceneChanged();
		}
	}

	void FBackend::HandleActorChanged(AActor* Actor)
	{
		NoteSceneChanged();
	}

	void FBackend::HandleMapChange(uint32 Flags)
	{
		if (IsCountingScene())
		{
			QueueAllComponentRows();
		}
	}

	void FBackend::HandleLevelChanged(ULevel* Level, UWorld* World)
	{
		if (World && World == EditorWorld())
		{
			HandleMapChange(0);
		}
	}

	// The public face (ContentViewsTeds.h).

	bool IsSceneCaughtUp()
	{
		return FBackend::Get().IsSceneCaughtUp();
	}

	FSceneTotals ReadSceneTotals()
	{
		return FBackend::Get().GetSceneTotals();
	}

	TArray<FSceneCostRow> ReadSceneRows(const USceneCostView& View)
	{
		TArray<FSceneCostRow> Rows;
		FBackend& Backend = FBackend::Get();
		const FSceneViewState* State = Backend.FindSceneView(FSoftObjectPath(&View));
		if (!State)
		{
			return Rows;
		}
		ICoreProvider* Storage = Backend.GetStorage();
		for (const TPair<FSoftObjectPath, RowHandle>& Pair : State->Rows)
		{
			FSceneCostRow& Out = Rows.AddDefaulted_GetRef();
			Out.Mesh = Pair.Key.ToString();
			const auto Read = [&Backend, &Pair](FName Column)
			{
				return Backend.ReadValue(Pair.Value, *Backend.FindSceneSlot(Column)).Number;
			};
			Out.Placed = static_cast<int64>(Read(TEXT("Placed")));
			Out.Triangles = static_cast<int64>(Read(TEXT("Triangles")));
			Out.SceneTriangles = static_cast<int64>(Read(FBackend::SceneTrianglesColumn));
			Out.Memory = static_cast<int64>(Read(TEXT("EstMemory")));
			Out.bNanite = Backend.ReadValue(Pair.Value, *Backend.FindSceneSlot(TEXT("Nanite"))).String == TEXT("Yes");
			if (const FTedsAlertColumn* Alert = Storage->GetColumn<FTedsAlertColumn>(Pair.Value))
			{
				Out.Alert = Alert->Message.ToString();
			}
		}
		Rows.Sort([](const FSceneCostRow& A, const FSceneCostRow& B) { return A.SceneTriangles > B.SceneTriangles; });
		return Rows;
	}

	TArray<FScenePlacement> ReadPlacements(const FSoftObjectPath& Mesh)
	{
		return FBackend::Get().PlacementsOf(Mesh);
	}
}

#undef LOCTEXT_NAMESPACE
