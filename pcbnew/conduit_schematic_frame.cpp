/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Conduit Schematic Editor — experimental editor for grouping nets into conduits.
 */

#include "conduit_schematic_frame.h"

#include <base_units.h>
#include <wx/menu.h>


BEGIN_EVENT_TABLE( CONDUIT_SCHEMATIC_FRAME, KIWAY_PLAYER )
    EVT_CLOSE( CONDUIT_SCHEMATIC_FRAME::onClose )
END_EVENT_TABLE()


CONDUIT_SCHEMATIC_FRAME::CONDUIT_SCHEMATIC_FRAME( KIWAY* aKiway, wxWindow* aParent ) :
    KIWAY_PLAYER( aKiway, aParent, FRAME_CONDUIT_SCHEMATIC,
                  _( "Conduit Schematic Editor" ),
                  wxDefaultPosition, wxSize( 1000, 700 ),
                  wxDEFAULT_FRAME_STYLE, "ConduitSchematicFrame", unityScale )
{
    setupMenuBar();
    CreateStatusBar();
    SetStatusText( _( "Conduit Schematic Editor  |  No project loaded" ) );
}


CONDUIT_SCHEMATIC_FRAME::~CONDUIT_SCHEMATIC_FRAME()
{
}


void CONDUIT_SCHEMATIC_FRAME::setupMenuBar()
{
    wxMenuBar* menuBar = new wxMenuBar();

    wxMenu* fileMenu = new wxMenu();
    fileMenu->Append( wxID_CLOSE, _( "&Close" ) );
    menuBar->Append( fileMenu, _( "&File" ) );

    SetMenuBar( menuBar );
    Bind( wxEVT_MENU, [this]( wxCommandEvent& ) { Close(); }, wxID_CLOSE );
}


void CONDUIT_SCHEMATIC_FRAME::onClose( wxCloseEvent& aEvent )
{
    Destroy();
}
