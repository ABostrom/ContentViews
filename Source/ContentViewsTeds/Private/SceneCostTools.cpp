#include "SceneCostTools.h"

#include "Components/StaticMeshComponent.h"
#include "ContentViewQuery.h"
#include "ContentViewsTeds.h"
#include "ContentViewsTedsBackend.h"
#include "GameFramework/Actor.h"
#include "Kismet/KismetSystemLibrary.h"
#include "SceneCostView.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(SceneCostTools)

FSceneCostToolResult USceneCostTools::RunSceneCost(const FString& View, int32 Limit)
{
	FSceneCostToolResult Result;
	const USceneCostView* Found = FindSceneViewOrRaise(View);
	if (!Found)
	{
		return Result;
	}
	if (Limit < 0)
	{
		UKismetSystemLibrary::RaiseScriptError(TEXT("Limit must be 0 (all rows) or more."));
		return Result;
	}
	using namespace UE::ContentViews::Teds;
	const FSceneTotals Totals = ReadSceneTotals();
	Result.View = Found->GetViewName().ToString();
	Result.Level = Totals.Level;
	Result.LoadedActors = Totals.LoadedActors;
	Result.Meshes = Totals.Meshes;
	Result.Components = Totals.Components;
	Result.Instances = Totals.Instances;
	Result.SceneTriangles = Totals.SceneTriangles;
	Result.NonNaniteTriangles = Totals.NonNaniteTriangles;
	Result.TriangleBudget = Found->TriangleBudget;
	Result.bCaughtUp = IsSceneCaughtUp();
	for (const FSceneCostRow& Row : ReadSceneRows(*Found))
	{
		if (Limit > 0 && Result.Rows.Num() >= Limit)
		{
			break;
		}
		FSceneCostToolRow& Out = Result.Rows.AddDefaulted_GetRef();
		Out.Mesh = Row.Mesh;
		Out.Placed = Row.Placed;
		Out.Triangles = Row.Triangles;
		Out.SceneTriangles = Row.SceneTriangles;
		Out.bNanite = Row.bNanite;
		Out.EstMemory = Row.Memory;
		Out.Alert = Row.Alert;
	}
	return Result;
}

TArray<FSceneCostToolPlacement> USceneCostTools::Placements(const FString& View, const FString& Mesh)
{
	TArray<FSceneCostToolPlacement> Found;
	const USceneCostView* Scene = FindSceneViewOrRaise(View);
	if (!Scene)
	{
		return Found;
	}
	using namespace UE::ContentViews::Teds;
	const TArray<FSceneCostRow> Rows = ReadSceneRows(*Scene);
	const FSceneCostRow* Row = Rows.FindByPredicate([&Mesh](const FSceneCostRow& Each)
	{
		return Each.Mesh.Equals(Mesh, ESearchCase::IgnoreCase)
			|| FSoftObjectPath(Each.Mesh).GetAssetName().Equals(Mesh, ESearchCase::IgnoreCase);
	});
	if (!Row)
	{
		UKismetSystemLibrary::RaiseScriptError(FString::Printf(
			TEXT("No mesh '%s' is placed in %s; RunSceneCost lists the placed meshes."), *Mesh, *ReadSceneTotals().Level));
		return Found;
	}
	for (const FScenePlacement& Placement : ReadPlacements(FSoftObjectPath(Row->Mesh)))
	{
		FSceneCostToolPlacement& Out = Found.AddDefaulted_GetRef();
		Out.Actor = Placement.Actor.IsValid() ? FSoftObjectPath(Placement.Actor.Get()).ToString() : FString();
		Out.ActorLabel = Placement.ActorLabel;
		Out.Component = Placement.Component.IsValid() ? FSoftObjectPath(Placement.Component.Get()).ToString() : FString();
		Out.Kind = Placement.Kind;
		Out.Instances = Placement.Instances;
		Out.Cell = Placement.Cell;
	}
	return Found;
}

USceneCostView* USceneCostTools::FindSceneViewOrRaise(const FString& View)
{
	USceneCostView* Found = Cast<USceneCostView>(UE::ContentViews::FindViewAsset(View));
	if (!Found)
	{
		TArray<FString> Names;
		for (const FAssetData& Asset : UE::ContentViews::FindViews())
		{
			if (Asset.IsInstanceOf(USceneCostView::StaticClass()))
			{
				Names.Add(Asset.AssetName.ToString());
			}
		}
		UKismetSystemLibrary::RaiseScriptError(FString::Printf(TEXT("No Scene Cost view named '%s'. Scene Cost views: %s"), *View,
			Names.IsEmpty() ? TEXT("(none)") : *FString::Join(Names, TEXT(", "))));
		return nullptr;
	}
	if (!UE::ContentViews::Teds::FBackend::Get().FindSceneView(FSoftObjectPath(Found)))
	{
		UKismetSystemLibrary::RaiseScriptError(TEXT("Scene Cost counts only in an editor with the TEDS backend on: Project Settings > "
			"Plugins > Content Views > Backend, TEDS or Both."));
		return nullptr;
	}
	return Found;
}
