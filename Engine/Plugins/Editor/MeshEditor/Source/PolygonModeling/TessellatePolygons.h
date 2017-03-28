// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MeshEditorCommands.h"
#include "TessellatePolygons.generated.h"


/** Tessellates selected polygons into smaller polygons */
UCLASS()
class UTessellatePolygonsCommand : public UMeshEditorPolygonCommand
{
	GENERATED_BODY()

protected:

	// Overrides
	virtual void RegisterUICommand( class FBindingContext* BindingContext ) override;
	virtual void Execute( class IMeshEditorModeEditingContract& MeshEditorMode, bool& bOutWasSuccessful ) override;

};
