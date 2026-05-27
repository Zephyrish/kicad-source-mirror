/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Conduit Schematic Editor — Phase 1 implementation.
 */

#include "conduit_schematic_frame.h"

#include <base_units.h>

#include <wx/artprov.h>
#include <wx/choice.h>
#include <wx/dialog.h>
#include <wx/listctrl.h>
#include <wx/menu.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/splitter.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/toolbar.h>


// Local control IDs (kept inside the frame's namespace by using high numbers)
enum
{
    ID_CONDUIT_ADD = wxID_HIGHEST + 100,
    ID_CONDUIT_LIST,
};


BEGIN_EVENT_TABLE( CONDUIT_SCHEMATIC_FRAME, KIWAY_PLAYER )
    EVT_CLOSE( CONDUIT_SCHEMATIC_FRAME::onClose )
    EVT_MENU( ID_CONDUIT_ADD, CONDUIT_SCHEMATIC_FRAME::onAddConduit )
    EVT_TOOL( ID_CONDUIT_ADD, CONDUIT_SCHEMATIC_FRAME::onAddConduit )
    EVT_LIST_ITEM_ACTIVATED( ID_CONDUIT_LIST, CONDUIT_SCHEMATIC_FRAME::onConduitActivated )
END_EVENT_TABLE()


CONDUIT_SCHEMATIC_FRAME::CONDUIT_SCHEMATIC_FRAME( KIWAY* aKiway, wxWindow* aParent ) :
    KIWAY_PLAYER( aKiway, aParent, FRAME_CONDUIT_SCHEMATIC,
                  _( "Conduit Schematic Editor" ),
                  wxDefaultPosition, wxSize( 1100, 700 ),
                  wxDEFAULT_FRAME_STYLE, CONDUIT_SCHEMATIC_FRAME_NAME, unityScale ),
    m_nextConduitNumber( 101 ),
    m_toolBar( nullptr ),
    m_splitter( nullptr ),
    m_listCtrl( nullptr ),
    m_canvasPanel( nullptr )
{
    setupMenuBar();
    setupToolBar();
    setupBody();

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

    wxMenu* conduitMenu = new wxMenu();
    conduitMenu->Append( ID_CONDUIT_ADD, _( "&Add Test Conduit\tCtrl+N" ) );
    menuBar->Append( conduitMenu, _( "&Conduit" ) );

    SetMenuBar( menuBar );
    Bind( wxEVT_MENU, [this]( wxCommandEvent& ) { Close(); }, wxID_CLOSE );
}


void CONDUIT_SCHEMATIC_FRAME::setupToolBar()
{
    m_toolBar = CreateToolBar( wxTB_HORIZONTAL | wxTB_FLAT | wxTB_TEXT );
    wxBitmap addIcon = wxArtProvider::GetBitmap( wxART_NEW, wxART_TOOLBAR );
    m_toolBar->AddTool( ID_CONDUIT_ADD, _( "Add Conduit" ), addIcon,
                        _( "Add a new test conduit" ) );
    m_toolBar->Realize();
}


void CONDUIT_SCHEMATIC_FRAME::setupBody()
{
    m_splitter = new wxSplitterWindow( this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                       wxSP_LIVE_UPDATE | wxSP_3D );

    // Left: conduit list
    m_listCtrl = new wxListCtrl( m_splitter, ID_CONDUIT_LIST, wxDefaultPosition, wxDefaultSize,
                                 wxLC_REPORT | wxLC_SINGLE_SEL );
    m_listCtrl->AppendColumn( _( "Name" ), wxLIST_FORMAT_LEFT, 110 );
    m_listCtrl->AppendColumn( _( "Type" ), wxLIST_FORMAT_LEFT, 70 );
    m_listCtrl->AppendColumn( _( "Diameter (in)" ), wxLIST_FORMAT_RIGHT, 100 );
    m_listCtrl->AppendColumn( _( "Cables" ), wxLIST_FORMAT_RIGHT, 70 );
    m_listCtrl->AppendColumn( _( "Fill %" ), wxLIST_FORMAT_RIGHT, 80 );

    // Right: placeholder canvas — real GAL canvas comes in Phase 3.
    m_canvasPanel = new wxPanel( m_splitter, wxID_ANY );
    m_canvasPanel->SetBackgroundColour( wxColour( 30, 30, 30 ) );

    wxBoxSizer* canvasSizer = new wxBoxSizer( wxVERTICAL );
    wxStaticText* placeholder = new wxStaticText( m_canvasPanel, wxID_ANY,
            _( "Conduit schematic canvas (Phase 3)" ),
            wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER_HORIZONTAL );
    placeholder->SetForegroundColour( wxColour( 160, 160, 160 ) );
    canvasSizer->AddStretchSpacer();
    canvasSizer->Add( placeholder, 0, wxALIGN_CENTER | wxALL, 8 );
    canvasSizer->AddStretchSpacer();
    m_canvasPanel->SetSizer( canvasSizer );

    m_splitter->SplitVertically( m_listCtrl, m_canvasPanel, 460 );
    m_splitter->SetMinimumPaneSize( 200 );

    wxBoxSizer* topSizer = new wxBoxSizer( wxVERTICAL );
    topSizer->Add( m_splitter, 1, wxEXPAND );
    SetSizer( topSizer );
    Layout();
}


void CONDUIT_SCHEMATIC_FRAME::refreshConduitList()
{
    // Preserve selection by index
    long selected = m_listCtrl->GetNextItem( -1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED );

    m_listCtrl->DeleteAllItems();

    for( size_t i = 0; i < m_conduits.size(); ++i )
    {
        const CONDUIT* c = m_conduits[i].get();

        long idx = m_listCtrl->InsertItem( static_cast<long>( i ), c->GetName() );
        m_listCtrl->SetItem( idx, 1, ConduitTypeToString( c->GetType() ) );
        m_listCtrl->SetItem( idx, 2, wxString::Format( wxT( "%.2f" ), c->GetDiameterInches() ) );
        m_listCtrl->SetItem( idx, 3, wxString::Format( wxT( "%zu" ), c->GetCables().size() ) );
        m_listCtrl->SetItem( idx, 4, wxString::Format( wxT( "%.1f%%" ), c->ComputeFillPercent() ) );

        // Store the index in item data so we can map back from list to conduit.
        m_listCtrl->SetItemData( idx, static_cast<long>( i ) );
    }

    if( selected >= 0 && selected < static_cast<long>( m_conduits.size() ) )
        m_listCtrl->SetItemState( selected, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED );

    SetStatusText( wxString::Format( _( "%zu conduit(s)" ), m_conduits.size() ) );
}


CONDUIT* CONDUIT_SCHEMATIC_FRAME::getSelectedConduit() const
{
    long sel = m_listCtrl->GetNextItem( -1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED );
    if( sel < 0 || sel >= static_cast<long>( m_conduits.size() ) )
        return nullptr;
    return m_conduits[ sel ].get();
}


void CONDUIT_SCHEMATIC_FRAME::onAddConduit( wxCommandEvent& aEvent )
{
    wxString name = wxString::Format( wxT( "C-%d" ), m_nextConduitNumber++ );
    m_conduits.push_back( std::make_unique<CONDUIT>( name ) );
    refreshConduitList();
}


void CONDUIT_SCHEMATIC_FRAME::onConduitActivated( wxListEvent& aEvent )
{
    CONDUIT* conduit = getSelectedConduit();
    if( conduit )
        editConduit( conduit );
}


// Inline properties dialog — kept here for Phase 1 simplicity.
// Refactor into its own file when it grows (Phase 2+).
void CONDUIT_SCHEMATIC_FRAME::editConduit( CONDUIT* aConduit )
{
    wxDialog dlg( this, wxID_ANY, _( "Conduit Properties" ),
                  wxDefaultPosition, wxSize( 340, 260 ) );

    wxBoxSizer* sizer = new wxBoxSizer( wxVERTICAL );
    wxFlexGridSizer* grid = new wxFlexGridSizer( 4, 2, 8, 8 );
    grid->AddGrowableCol( 1, 1 );

    grid->Add( new wxStaticText( &dlg, wxID_ANY, _( "Name:" ) ),
               0, wxALIGN_CENTER_VERTICAL );
    wxTextCtrl* nameCtrl = new wxTextCtrl( &dlg, wxID_ANY, aConduit->GetName() );
    grid->Add( nameCtrl, 1, wxEXPAND );

    grid->Add( new wxStaticText( &dlg, wxID_ANY, _( "Type:" ) ),
               0, wxALIGN_CENTER_VERTICAL );
    wxArrayString typeChoices;
    for( int i = 0; i <= static_cast<int>( CONDUIT_TYPE::LFNC ); ++i )
        typeChoices.Add( ConduitTypeToString( static_cast<CONDUIT_TYPE>( i ) ) );
    wxChoice* typeCtrl = new wxChoice( &dlg, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                       typeChoices );
    typeCtrl->SetSelection( static_cast<int>( aConduit->GetType() ) );
    grid->Add( typeCtrl, 1, wxEXPAND );

    grid->Add( new wxStaticText( &dlg, wxID_ANY, _( "Diameter (in):" ) ),
               0, wxALIGN_CENTER_VERTICAL );
    wxTextCtrl* diaCtrl = new wxTextCtrl( &dlg, wxID_ANY,
            wxString::Format( wxT( "%.3f" ), aConduit->GetDiameterInches() ) );
    grid->Add( diaCtrl, 1, wxEXPAND );

    grid->Add( new wxStaticText( &dlg, wxID_ANY, _( "Max Fill %:" ) ),
               0, wxALIGN_CENTER_VERTICAL );
    wxTextCtrl* fillCtrl = new wxTextCtrl( &dlg, wxID_ANY,
            wxString::Format( wxT( "%.1f" ), aConduit->GetMaxFillPercent() ) );
    grid->Add( fillCtrl, 1, wxEXPAND );

    sizer->Add( grid, 1, wxALL | wxEXPAND, 12 );
    sizer->Add( dlg.CreateButtonSizer( wxOK | wxCANCEL ), 0, wxEXPAND | wxALL, 8 );
    dlg.SetSizer( sizer );

    if( dlg.ShowModal() == wxID_OK )
    {
        aConduit->SetName( nameCtrl->GetValue() );
        aConduit->SetType( static_cast<CONDUIT_TYPE>( typeCtrl->GetSelection() ) );

        double dia = aConduit->GetDiameterInches();
        diaCtrl->GetValue().ToDouble( &dia );
        aConduit->SetDiameterInches( dia );

        double fill = aConduit->GetMaxFillPercent();
        fillCtrl->GetValue().ToDouble( &fill );
        aConduit->SetMaxFillPercent( fill );

        refreshConduitList();
    }
}


void CONDUIT_SCHEMATIC_FRAME::onClose( wxCloseEvent& aEvent )
{
    Destroy();
}
