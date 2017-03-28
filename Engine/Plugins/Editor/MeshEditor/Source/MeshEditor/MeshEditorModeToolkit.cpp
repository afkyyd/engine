// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "MeshEditorModeToolkit.h"
#include "IMeshEditorModeUIContract.h"
#include "MeshEditorCommands.h"
#include "MeshEditorStyle.h"
#include "SWidgetSwitcher.h"
#include "SSeparator.h"
#include "SScrollBox.h"

#define LOCTEXT_NAMESPACE "MeshEditorModeToolkit"


class SMeshEditorModeControlWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS( SMeshEditorModeControlWidget ) {}
	SLATE_END_ARGS()

public:

	/** SCompoundWidget functions */
	void Construct( const FArguments& InArgs, const TArray<TTuple<TSharedPtr<FUICommandInfo>, FUIAction>>& Actions )
	{
		TSharedRef<SVerticalBox> Buttons = SNew( SVerticalBox );
		TSharedRef<SVerticalBox> RadioButtons = SNew( SVerticalBox );

		for( const TTuple<TSharedPtr<FUICommandInfo>, FUIAction>& Action : Actions )
		{
			const FUICommandInfo& CommandInfo = *Action.Get<0>();
			const FUIAction& UIAction = Action.Get<1>();

			if( CommandInfo.GetUserInterfaceType() == EUserInterfaceActionType::Button )
			{
				Buttons->AddSlot()
				.AutoHeight()
				.Padding( 3.0f )
				[
					SNew( SButton )
					.HAlign( HAlign_Center )
					.VAlign( VAlign_Center )
					.ContentPadding( FMargin( 4.0f ) )
					.Text( CommandInfo.GetLabel() )
					.ToolTip( SNew( SToolTip ).Text( CommandInfo.GetDescription() ) )
					.OnClicked_Lambda( [UIAction] { UIAction.Execute(); return FReply::Handled(); } )
					.IsEnabled_Lambda( [UIAction] { return UIAction.CanExecute(); } )
				];
			}
			else if( CommandInfo.GetUserInterfaceType() == EUserInterfaceActionType::RadioButton )
			{
				RadioButtons->AddSlot()
				.AutoHeight()
				.Padding( 1.0f )
				[
					SNew( SCheckBox )
					.Style( FMeshEditorStyle::Get(), "EditingMode.Entry" )
					.ToolTip( SNew( SToolTip ).Text( CommandInfo.GetDescription() ) )
					.IsChecked_Lambda( [UIAction] { return UIAction.GetCheckState(); } )
					.OnCheckStateChanged_Lambda( [UIAction]( ECheckBoxState State ) { if( State == ECheckBoxState::Checked ) { UIAction.Execute(); } } )
					[
						SNew( SOverlay )
						+SOverlay::Slot()
						.VAlign( VAlign_Center )
						[
							SNew( SSpacer )
							.Size( FVector2D( 1, 30 ) )
						]
						+SOverlay::Slot()
						.Padding( FMargin( 20, 0, 20, 0 ) )
						.HAlign( HAlign_Center )
						.VAlign( VAlign_Center )
						[
							SNew( STextBlock )
							.TextStyle( FMeshEditorStyle::Get(), "EditingMode.Entry.Text" )
							.Text( CommandInfo.GetLabel() )
						]
					]
				];
			}
		}

		// This is the basic layout for each selected element type:
		// First, a list of buttons, then a separator, and then a list of radio buttons.
		// @todo mesheditor: if we want to make these UI elements bigger (e.g. for ease of use with VR), we can easily change these
		// to icons with text to the side. The icon name is already registered with the FUICommandInfo (e.g. "MeshEditorVertex.MoveAction").
		ChildSlot
		[
			SNew( SVerticalBox )
			+SVerticalBox::Slot()
			.AutoHeight()
			.Padding( 6.0f )
			.HAlign( HAlign_Center )
			[
				Buttons
			]
			+SVerticalBox::Slot()
			.AutoHeight()
			.Padding( 6.0f )
			[
				SNew( SSeparator )
				.Orientation( Orient_Horizontal )
			]
			+SVerticalBox::Slot()
			.AutoHeight()
			.Padding( 6.0f )
			.HAlign( HAlign_Center )
			[
				RadioButtons
			]
		];
	}
};


class SMeshEditorSelectionModeWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS( SMeshEditorModeControlWidget ) {}
	SLATE_END_ARGS()

public:
	void Construct( const FArguments& InArgs, IMeshEditorModeUIContract& MeshEditorMode, EEditableMeshElementType ElementType, const FText& Label )
	{
		ChildSlot
		[
			SNew( SBox )
			.HAlign( HAlign_Fill )
			.VAlign( VAlign_Center )
			[
				SNew( SCheckBox )
				.Style( FMeshEditorStyle::Get(), "SelectionMode.Entry" )
				.HAlign( HAlign_Fill )
				.IsChecked_Lambda( [ &MeshEditorMode, ElementType ] { return ( MeshEditorMode.GetMeshElementSelectionMode() == ElementType ? ECheckBoxState::Checked : ECheckBoxState::Unchecked ); } )
				.OnCheckStateChanged_Lambda( [ &MeshEditorMode, ElementType ]( ECheckBoxState State ) { if( State == ECheckBoxState::Checked ) { MeshEditorMode.SetMeshElementSelectionMode( ElementType ); } } )
				[
					SNew( SHorizontalBox )
					+SHorizontalBox::Slot()
					.FillWidth( 1 )
					.HAlign( HAlign_Center )
					[
						SNew( STextBlock )
						.TextStyle( FMeshEditorStyle::Get(), "SelectionMode.Entry.Text" )
						.Text( Label )
					]
				]
			]
		];
	}
};


void SMeshEditorModeControls::Construct( const FArguments& InArgs, IMeshEditorModeUIContract& MeshEditorMode )
{
	// Uses the widget switcher widget so only the widget in the slot which corresponds to the selected mesh element type will be shown
	TSharedRef<SWidgetSwitcher> WidgetSwitcher = SNew( SWidgetSwitcher )
		.WidgetIndex_Lambda( [&MeshEditorMode]() -> int32
			{
				if( MeshEditorMode.GetMeshElementSelectionMode() != EEditableMeshElementType::Any )
				{
					return static_cast<int32>( MeshEditorMode.GetMeshElementSelectionMode() );
				}
				else
				{
					return static_cast<int32>( MeshEditorMode.GetSelectedMeshElementType() );
				}
			} 
		);

	WidgetSwitcher->AddSlot( static_cast<int32>( EEditableMeshElementType::Vertex ) )
	[
		SNew( SMeshEditorModeControlWidget, MeshEditorMode.GetVertexActions() )
	];

	WidgetSwitcher->AddSlot( static_cast<int32>( EEditableMeshElementType::Edge ) )
	[
		SNew( SMeshEditorModeControlWidget, MeshEditorMode.GetEdgeActions() )
	];

	WidgetSwitcher->AddSlot( static_cast<int32>( EEditableMeshElementType::Polygon ) )
	[
		SNew( SMeshEditorModeControlWidget, MeshEditorMode.GetPolygonActions() )
	];

	WidgetSwitcher->AddSlot( static_cast<int32>( EEditableMeshElementType::Invalid ) )
	[
		SNew( SBox )
		.Padding( 20.0f )
		.HAlign( HAlign_Center )
		[
			SNew( STextBlock )
			.Text( LOCTEXT( "NothingSelected", "Nothing is selected" ) )
		]
	];

	ChildSlot
	[
		SNew( SScrollBox )
		+SScrollBox::Slot()
		.Padding( 6.0f )
		[
			SNew( SVerticalBox )
			+SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew( SHorizontalBox )
				+SHorizontalBox::Slot()
				.VAlign( VAlign_Center )
				.AutoWidth()
				.Padding( 2, 2, 8, 2 )
				[
					SNew( STextBlock )
					.Text( LOCTEXT( "SelectLabel", "Select" ) )
				]
				+SHorizontalBox::Slot()
				.FillWidth( 1 )
				.Padding( 2 )
				[
					SNew( SMeshEditorSelectionModeWidget, MeshEditorMode, EEditableMeshElementType::Vertex, LOCTEXT( "Vertices", "Vertices" ) )
				]
				+SHorizontalBox::Slot()
				.FillWidth( 1 )
				.Padding( 2 )
				[
					SNew( SMeshEditorSelectionModeWidget, MeshEditorMode, EEditableMeshElementType::Edge, LOCTEXT( "Edges", "Edges" ) )
				]
				+SHorizontalBox::Slot()
				.FillWidth( 1 )
				.Padding( 2 )
				[
					SNew( SMeshEditorSelectionModeWidget, MeshEditorMode, EEditableMeshElementType::Polygon, LOCTEXT( "Polygons", "Polygons" ) )
				]
				+SHorizontalBox::Slot()
				.FillWidth( 1 )
				.Padding( 2 )
				[
					SNew( SMeshEditorSelectionModeWidget, MeshEditorMode, EEditableMeshElementType::Any, LOCTEXT( "Any", "Any" ) )
				]
			]
			+SVerticalBox::Slot()
			.AutoHeight()
			.Padding( 0, 4, 0, 0 )
			.HAlign( HAlign_Right )
			[
				SNew( SHorizontalBox )
				+SHorizontalBox::Slot()
				.AutoWidth()
				.Padding( 2 )
				[
					SNew( SButton )
					.HAlign( HAlign_Center )
					.VAlign( VAlign_Center )
					.Text( LOCTEXT( "Propagate", "Propagate" ) )
					.ToolTip( SNew( SToolTip ).Text( LOCTEXT( "PropagateTooltip", "Propagates per-instance changes to the static mesh asset itself." ) ) )
					.IsEnabled_Lambda( [ &MeshEditorMode ] { return false; /*MeshEditorMode.CanPropagateInstanceChanges(); */ } )
					.OnClicked_Lambda( [ &MeshEditorMode ] { MeshEditorMode.PropagateInstanceChanges(); return FReply::Handled(); } )
				]
				+SHorizontalBox::Slot()
				.AutoWidth()
				.Padding( 2 )
				[
					SNew( SBox )
					.HAlign( HAlign_Fill )
					.VAlign( VAlign_Center )
					[
						SNew( SCheckBox )
						.Style( FMeshEditorStyle::Get(), "SelectionMode.Entry" )
						.HAlign( HAlign_Fill )
						.ToolTip( SNew( SToolTip ).Text( LOCTEXT( "PerInstanceTooltip", "Toggles editing mode between editing instances and editing the original static mesh asset." ) ) )
						.IsChecked_Lambda( [ &MeshEditorMode ] { return ( MeshEditorMode.IsEditingPerInstance() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked ); } )
						.OnCheckStateChanged_Lambda( [ &MeshEditorMode ]( ECheckBoxState State ) { MeshEditorMode.SetEditingPerInstance( State == ECheckBoxState::Checked ); } )
						[
							SNew( SHorizontalBox )
							+SHorizontalBox::Slot()
							.AutoWidth()
							.HAlign( HAlign_Center )
							[
								SNew( STextBlock )
								.TextStyle( FMeshEditorStyle::Get(), "SelectionMode.Entry.Text" )
								.Text( LOCTEXT( "PerInstance", "Per Instance" ) )
							]
						]
					]
				]
			]
			+SVerticalBox::Slot()
			[
				SNew( SBorder )
				.BorderImage( FEditorStyle::GetBrush( "ToolPanel.GroupBorder" ) )
				[
					WidgetSwitcher
				]
			]
		]
	];
}


void FMeshEditorModeToolkit::RegisterTabSpawners( const TSharedRef<FTabManager>& TabManager )
{
}


void FMeshEditorModeToolkit::UnregisterTabSpawners( const TSharedRef<FTabManager>& TabManager )
{
}


void FMeshEditorModeToolkit::Init( const TSharedPtr<IToolkitHost>& InitToolkitHost )
{
	ToolkitWidget = SNew( SMeshEditorModeControls, MeshEditorMode );

	FModeToolkit::Init( InitToolkitHost );
}


FName FMeshEditorModeToolkit::GetToolkitFName() const
{
	return FName( "MeshEditorMode" );
}


FText FMeshEditorModeToolkit::GetBaseToolkitName() const
{
	return LOCTEXT( "ToolkitName", "Mesh Editor Mode" );
}


class FEdMode* FMeshEditorModeToolkit::GetEditorMode() const
{
	return GLevelEditorModeTools().GetActiveMode( "MeshEditor" );
}


TSharedPtr<SWidget> FMeshEditorModeToolkit::GetInlineContent() const
{
	return ToolkitWidget;
}

#undef LOCTEXT_NAMESPACE
