#pragma once

#include "CoreMinimal.h"
#include "ContentViewDefinition.h"
#include "SceneCostView.generated.h"

/**
 * Scene Cost, the Join archetype: its rows are the static meshes placed in the level open in the editor, each joined with
 * its asset's registry facts. It counts a mesh's placements (instanced components by their instances), its triangles
 * (Nanite triangles for a Nanite mesh), the triangles it puts in the scene, and alerts on a non-Nanite mesh past the
 * budget. TEDS only: the counts come from TEDS's component rows, and the view shows as a Content Source.
 */
UCLASS(BlueprintType, meta = (ShortTooltip = "The static meshes placed in the open level, with what they cost."))
class CONTENTVIEWSTEDS_API USceneCostView : public UContentViewBase
{
	GENERATED_BODY()

public:
	/** A mesh that is not Nanite and puts more triangles than this in the scene (placements x triangles) gets an alert. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Budget", meta = (ClampMin = "0"))
	int64 TriangleBudget = 1000000;
};
