// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#include "UMGEditorPrivatePCH.h"

#include "PaletteTabSummoner.h"
#include "SPaletteView.h"

#define LOCTEXT_NAMESPACE "UMG"

const FName FPaletteTabSummoner::TabID(TEXT("WidgetTemplates"));

FPaletteTabSummoner::FPaletteTabSummoner(TSharedPtr<class FWidgetBlueprintEditor> InBlueprintEditor)
		: FWorkflowTabFactory(TabID, InBlueprintEditor)
		, BlueprintEditor(InBlueprintEditor)
{
	TabLabel = LOCTEXT("WidgetTemplatesTabLabel", "Palette");
	TabIcon = FUMGStyle::Get().GetBrush("UMGEditor.Tabs.Palette");

	bIsSingleton = true;

	ViewMenuDescription = LOCTEXT("WidgetTemplates_ViewMenu_Desc", "Palette");
	ViewMenuTooltip = LOCTEXT("WidgetTemplates_ViewMenu_ToolTip", "Show the Palette");
}

TSharedRef<SWidget> FPaletteTabSummoner::CreateTabBody(const FWorkflowTabSpawnInfo& Info) const
{
	TSharedPtr<FWidgetBlueprintEditor> BlueprintEditorPtr = StaticCastSharedPtr<FWidgetBlueprintEditor>(BlueprintEditor.Pin());

	return SNew(SPaletteView, BlueprintEditorPtr)
		.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("Palette")));
}

#undef LOCTEXT_NAMESPACE 
