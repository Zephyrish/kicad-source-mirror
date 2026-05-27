/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Conduit Schematic Editor — Phase 3 starter: visual canvas + cable assignment.
 */

#include "conduit_schematic_frame.h"

#include <algorithm>
#include <map>
#include <unordered_map>

#include <base_units.h>
#include <board.h>
#include <footprint.h>
#include <netinfo.h>
#include <pad.h>

#include <unordered_set>

#include "conduit/conduit_canvas_panel.h"

#include <wx/artprov.h>
#include <wx/choice.h>
#include <wx/dialog.h>
#include <wx/listctrl.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/splitter.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/toolbar.h>


// Local control IDs
enum
{
    ID_CONDUIT_ADD = wxID_HIGHEST + 100,
    ID_CABLES_REFRESH,
    ID_ASSIGN_CABLE,
    ID_CONDUIT_LIST,
    ID_CABLE_LIST,
};


BEGIN_EVENT_TABLE( CONDUIT_SCHEMATIC_FRAME, KIWAY_PLAYER )
    EVT_CLOSE( CONDUIT_SCHEMATIC_FRAME::onClose )
    EVT_MENU( ID_CONDUIT_ADD, CONDUIT_SCHEMATIC_FRAME::onAddConduit )
    EVT_TOOL( ID_CONDUIT_ADD, CONDUIT_SCHEMATIC_FRAME::onAddConduit )
    EVT_TOOL( ID_CABLES_REFRESH, CONDUIT_SCHEMATIC_FRAME::onRefreshCables )
    EVT_TOOL( ID_ASSIGN_CABLE, CONDUIT_SCHEMATIC_FRAME::onAssignCable )
    EVT_LIST_ITEM_ACTIVATED( ID_CONDUIT_LIST, CONDUIT_SCHEMATIC_FRAME::onConduitListActivated )
    EVT_LIST_ITEM_SELECTED ( ID_CONDUIT_LIST, CONDUIT_SCHEMATIC_FRAME::onConduitListSelected )
    EVT_LIST_ITEM_ACTIVATED( ID_CABLE_LIST,   CONDUIT_SCHEMATIC_FRAME::onCableActivated )
END_EVENT_TABLE()


CONDUIT_SCHEMATIC_FRAME::CONDUIT_SCHEMATIC_FRAME( KIWAY* aKiway, wxWindow* aParent, BOARD* aBoard ) :
    KIWAY_PLAYER( aKiway, aParent, FRAME_CONDUIT_SCHEMATIC,
                  _( "Conduit Schematic Editor" ),
                  wxDefaultPosition, wxSize( 1200, 750 ),
                  wxDEFAULT_FRAME_STYLE, CONDUIT_SCHEMATIC_FRAME_NAME, unityScale ),
    m_board( aBoard ),
    m_nextConduitNumber( 101 ),
    m_toolBar( nullptr ),
    m_mainSplitter( nullptr ),
    m_leftSplitter( nullptr ),
    m_conduitListCtrl( nullptr ),
    m_cableListCtrl( nullptr ),
    m_canvasPanel( nullptr )
{
    setupMenuBar();
    setupToolBar();
    setupBody();

    CreateStatusBar();
    refreshCableList();

    // When the user clicks a conduit in the canvas, mirror the selection in the list.
    m_canvasPanel->SetOnSelectionChanged(
            [this]( CONDUIT* selected )
            {
                if( !selected )
                    return;
                for( size_t i = 0; i < m_conduits.size(); ++i )
                {
                    if( m_conduits[i].get() == selected )
                    {
                        m_conduitListCtrl->SetItemState( static_cast<long>( i ),
                                wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED );
                        break;
                    }
                }
            } );

    // Double-click in canvas opens properties dialog.
    m_canvasPanel->SetOnActivated(
            [this]( CONDUIT* c ) { if( c ) editConduit( c ); } );

    // Right-click → Remove cable from conduit.
    m_canvasPanel->SetOnCableRemove(
            [this]( CONDUIT* conduit, CABLE* cable )
            {
                if( !conduit || !cable )
                    return;
                conduit->RemoveCable( cable );
                refreshConduitList();
                refreshCableList();   // also refreshes canvas
            } );

    // Right-click on orphan → Re-link to currently-selected list cable.
    m_canvasPanel->SetOnCableRelink(
            [this]( CONDUIT* conduit, CABLE* cable )
            {
                if( !conduit || !cable )
                    return;

                int      targetCode = getSelectedNetCode();
                wxString targetName = getSelectedNetName();
                if( targetCode <= 0 )
                    return;

                // If the user already has a CABLE for the target net (e.g., it's
                // assigned to another conduit), point this conduit at that CABLE
                // and discard the orphan record.
                CABLE* targetCable = findOrCreateCable( targetCode, targetName );

                if( targetCable == cable )
                    return;     // nothing to do

                if( std::find( conduit->GetCables().begin(),
                               conduit->GetCables().end(),
                               targetCable ) != conduit->GetCables().end() )
                {
                    // The conduit already contains the target — just drop the orphan.
                    conduit->RemoveCable( cable );
                }
                else
                {
                    // Swap orphan out, target in.
                    conduit->RemoveCable( cable );
                    conduit->AddCable( targetCable );
                }

                refreshConduitList();
                refreshCableList();
            } );

    // Provide the "what's selected in the cable list" hint for the context menu.
    m_canvasPanel->SetRelinkTargetProvider(
            [this]() -> wxString
            {
                return getSelectedNetName();
            } );
}


CONDUIT_SCHEMATIC_FRAME::~CONDUIT_SCHEMATIC_FRAME()
{
}


wxWindow* CONDUIT_SCHEMATIC_FRAME::GetToolCanvas() const
{
    return m_canvasPanel;
}


void CONDUIT_SCHEMATIC_FRAME::setupMenuBar()
{
    wxMenuBar* menuBar = new wxMenuBar();

    wxMenu* fileMenu = new wxMenu();
    fileMenu->Append( wxID_CLOSE, _( "&Close" ) );
    menuBar->Append( fileMenu, _( "&File" ) );

    wxMenu* conduitMenu = new wxMenu();
    conduitMenu->Append( ID_CONDUIT_ADD, _( "&Add Test Conduit\tCtrl+N" ) );
    conduitMenu->Append( ID_ASSIGN_CABLE,
                         _( "Assign Selected Cable to Selected Conduit\tCtrl+Enter" ) );
    menuBar->Append( conduitMenu, _( "&Conduit" ) );

    SetMenuBar( menuBar );
    Bind( wxEVT_MENU, [this]( wxCommandEvent& ) { Close(); }, wxID_CLOSE );
    Bind( wxEVT_MENU, [this]( wxCommandEvent& e ) { onAssignCable( e ); }, ID_ASSIGN_CABLE );
}


void CONDUIT_SCHEMATIC_FRAME::setupToolBar()
{
    m_toolBar = CreateToolBar( wxTB_HORIZONTAL | wxTB_FLAT | wxTB_TEXT );
    m_toolBar->AddTool( ID_CONDUIT_ADD, _( "Add Conduit" ),
                        wxArtProvider::GetBitmap( wxART_NEW, wxART_TOOLBAR ),
                        _( "Add a new test conduit" ) );
    m_toolBar->AddSeparator();
    m_toolBar->AddTool( ID_ASSIGN_CABLE, _( "Assign Cable to Conduit" ),
                        wxArtProvider::GetBitmap( wxART_GO_FORWARD, wxART_TOOLBAR ),
                        _( "Add the selected cable to the selected conduit" ) );
    m_toolBar->AddSeparator();
    m_toolBar->AddTool( ID_CABLES_REFRESH, _( "Refresh Cables" ),
                        wxArtProvider::GetBitmap( wxART_REDO, wxART_TOOLBAR ),
                        _( "Re-read cables from the active board" ) );
    m_toolBar->Realize();
}


void CONDUIT_SCHEMATIC_FRAME::setupBody()
{
    m_mainSplitter = new wxSplitterWindow( this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                           wxSP_LIVE_UPDATE | wxSP_3D );

    // ----- Left: stacked cables (top) + conduits (bottom) -----
    m_leftSplitter = new wxSplitterWindow( m_mainSplitter, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                           wxSP_LIVE_UPDATE | wxSP_3D );

    // Cables panel
    wxPanel* cablesPanel = new wxPanel( m_leftSplitter, wxID_ANY );
    wxBoxSizer* cablesSizer = new wxBoxSizer( wxVERTICAL );
    cablesSizer->Add( new wxStaticText( cablesPanel, wxID_ANY,
                                        _( "Cables (from electrical schematic)" ) ),
                      0, wxLEFT | wxTOP | wxRIGHT, 4 );
    m_cableListCtrl = new wxListCtrl( cablesPanel, ID_CABLE_LIST,
                                      wxDefaultPosition, wxDefaultSize,
                                      wxLC_REPORT | wxLC_SINGLE_SEL );
    m_cableListCtrl->AppendColumn( _( "Net" ),         wxLIST_FORMAT_LEFT,  180 );
    m_cableListCtrl->AppendColumn( _( "Code" ),        wxLIST_FORMAT_RIGHT, 50 );
    m_cableListCtrl->AppendColumn( _( "Class" ),       wxLIST_FORMAT_LEFT,  100 );
    m_cableListCtrl->AppendColumn( _( "Assigned to" ), wxLIST_FORMAT_LEFT,  140 );
    cablesSizer->Add( m_cableListCtrl, 1, wxEXPAND | wxALL, 2 );
    cablesPanel->SetSizer( cablesSizer );

    // Conduits panel
    wxPanel* conduitsPanel = new wxPanel( m_leftSplitter, wxID_ANY );
    wxBoxSizer* conduitsSizer = new wxBoxSizer( wxVERTICAL );
    conduitsSizer->Add( new wxStaticText( conduitsPanel, wxID_ANY, _( "Conduits" ) ),
                        0, wxLEFT | wxTOP | wxRIGHT, 4 );
    m_conduitListCtrl = new wxListCtrl( conduitsPanel, ID_CONDUIT_LIST,
                                        wxDefaultPosition, wxDefaultSize,
                                        wxLC_REPORT | wxLC_SINGLE_SEL );
    m_conduitListCtrl->AppendColumn( _( "Name" ),     wxLIST_FORMAT_LEFT,  100 );
    m_conduitListCtrl->AppendColumn( _( "Type" ),     wxLIST_FORMAT_LEFT,  60 );
    m_conduitListCtrl->AppendColumn( _( "Dia (in)" ), wxLIST_FORMAT_RIGHT, 70 );
    m_conduitListCtrl->AppendColumn( _( "Cables" ),   wxLIST_FORMAT_RIGHT, 60 );
    m_conduitListCtrl->AppendColumn( _( "Fill %" ),   wxLIST_FORMAT_RIGHT, 60 );
    conduitsSizer->Add( m_conduitListCtrl, 1, wxEXPAND | wxALL, 2 );
    conduitsPanel->SetSizer( conduitsSizer );

    m_leftSplitter->SplitHorizontally( cablesPanel, conduitsPanel, 360 );
    m_leftSplitter->SetMinimumPaneSize( 80 );

    // ----- Right: visual canvas -----
    m_canvasPanel = new CONDUIT_CANVAS_PANEL( m_mainSplitter, &m_conduits );

    m_mainSplitter->SplitVertically( m_leftSplitter, m_canvasPanel, 480 );
    m_mainSplitter->SetMinimumPaneSize( 240 );

    wxBoxSizer* topSizer = new wxBoxSizer( wxVERTICAL );
    topSizer->Add( m_mainSplitter, 1, wxEXPAND );
    SetSizer( topSizer );
    Layout();
}


void CONDUIT_SCHEMATIC_FRAME::RefreshFromBoard()
{
    refreshCableList();
}


void CONDUIT_SCHEMATIC_FRAME::refreshConduitList()
{
    long selected = m_conduitListCtrl->GetNextItem( -1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED );

    m_conduitListCtrl->DeleteAllItems();

    for( size_t i = 0; i < m_conduits.size(); ++i )
    {
        const CONDUIT* c = m_conduits[i].get();

        long idx = m_conduitListCtrl->InsertItem( static_cast<long>( i ), c->GetName() );
        m_conduitListCtrl->SetItem( idx, 1, ConduitTypeToString( c->GetType() ) );
        m_conduitListCtrl->SetItem( idx, 2, wxString::Format( wxT( "%.2f" ), c->GetDiameterInches() ) );
        m_conduitListCtrl->SetItem( idx, 3, wxString::Format( wxT( "%zu" ), c->GetCables().size() ) );
        m_conduitListCtrl->SetItem( idx, 4, wxString::Format( wxT( "%.1f%%" ), c->ComputeFillPercent() ) );
        m_conduitListCtrl->SetItemData( idx, static_cast<long>( i ) );
    }

    if( selected >= 0 && selected < static_cast<long>( m_conduits.size() ) )
    {
        m_conduitListCtrl->SetItemState( selected, wxLIST_STATE_SELECTED,
                                         wxLIST_STATE_SELECTED );
    }
}


void CONDUIT_SCHEMATIC_FRAME::syncCablesFromBoard()
{
    if( !m_board )
        return;

    // Build: netCode -> name
    std::unordered_map<int, wxString> codeToName;
    for( NETINFO_ITEM* net : m_board->GetNetInfo() )
    {
        if( !net || net->GetNetCode() <= 0 )
            continue;
        codeToName[ net->GetNetCode() ] = net->GetNetname();
    }

    // Build: netCode -> {pad UUIDs}. Pad UUIDs are stable across net renames.
    std::unordered_map<int, std::vector<KIID>> padsByNet;
    for( FOOTPRINT* fp : m_board->Footprints() )
    {
        for( PAD* pad : fp->Pads() )
        {
            int code = pad->GetNetCode();
            if( code > 0 )
                padsByNet[ code ].push_back( pad->m_Uuid );
        }
    }

    for( const std::unique_ptr<CABLE>& cable : m_cables )
    {
        int code = cable->GetNetCode();

        // --- Direct match by net code (net unchanged this cycle) ---
        if( code > 0 && codeToName.find( code ) != codeToName.end() )
        {
            cable->SetName( codeToName[ code ] );
            auto pit = padsByNet.find( code );
            if( pit != padsByNet.end() )
                cable->SetPadIds( pit->second );    // refresh pad snapshot
            cable->SetOrphan( false );
            continue;
        }

        // --- Net code is gone. Try to re-find the same net via pad overlap. ---
        const std::vector<KIID>& cablePads = cable->GetPadIds();
        if( cablePads.empty() )
        {
            cable->SetOrphan( true );
            continue;
        }

        std::unordered_set<KIID> cablePadSet( cablePads.begin(), cablePads.end() );

        int    bestCode    = -1;
        size_t bestCommon  = 0;
        for( const auto& [boardCode, boardPads] : padsByNet )
        {
            size_t common = 0;
            for( const KIID& id : boardPads )
                if( cablePadSet.count( id ) )
                    common++;

            if( common > bestCommon )
            {
                bestCommon = common;
                bestCode   = boardCode;
            }
        }

        // Adopt the new net if at least half the cable's original pads are still on it.
        // (Catches the common KiCad rename pattern: drop old net, create new one with
        // the same pads but a different code.)
        if( bestCode > 0 && bestCommon * 2 >= cablePads.size() )
        {
            cable->SetNetCode( bestCode );
            cable->SetName( codeToName[ bestCode ] );
            cable->SetPadIds( padsByNet[ bestCode ] );
            cable->SetOrphan( false );
        }
        else
        {
            cable->SetOrphan( true );
        }
    }
}


void CONDUIT_SCHEMATIC_FRAME::refreshCableList()
{
    m_cableListCtrl->DeleteAllItems();

    if( !m_board )
    {
        SetStatusText( _( "No board loaded — open a PCB to see cables" ) );
        if( m_canvasPanel )
            m_canvasPanel->RefreshLayout();
        return;
    }

    // Step 1: pull the latest names from the board into our owned CABLE objects.
    syncCablesFromBoard();

    // Step 2: build a map of (current) net name -> comma-joined conduit names that contain it.
    std::map<wxString, wxString> assignments;
    for( const std::unique_ptr<CONDUIT>& c : m_conduits )
    {
        for( const CABLE* cable : c->GetCables() )
        {
            wxString& accum = assignments[ cable->GetName() ];
            if( !accum.IsEmpty() )
                accum += wxT( ", " );
            accum += c->GetName();
        }
    }

    long idx = 0;
    int  shown = 0;

    for( NETINFO_ITEM* net : m_board->GetNetInfo() )
    {
        if( !net )
            continue;
        if( net->GetNetCode() <= 0 )
            continue;
        if( net->GetNetname().IsEmpty() )
            continue;

        m_cableListCtrl->InsertItem( idx, net->GetNetname() );
        m_cableListCtrl->SetItem( idx, 1, wxString::Format( wxT( "%d" ), net->GetNetCode() ) );

        wxString className;
        if( net->GetNetClass() )
            className = net->GetNetClass()->GetName();
        m_cableListCtrl->SetItem( idx, 2, className );

        auto it = assignments.find( net->GetNetname() );
        if( it != assignments.end() )
            m_cableListCtrl->SetItem( idx, 3, it->second );

        // Store net code in item data so we can look it up after selection without
        // re-parsing the visible name (which may not be unique forever).
        m_cableListCtrl->SetItemData( idx, static_cast<wxIntPtr>( net->GetNetCode() ) );

        idx++;
        shown++;
    }

    SetStatusText( wxString::Format( _( "%d cable(s) from board  |  %zu conduit(s)" ),
                                     shown, m_conduits.size() ) );

    // Cable names inside conduits may have changed — redraw the canvas too.
    if( m_canvasPanel )
        m_canvasPanel->RefreshLayout();
}


CABLE* CONDUIT_SCHEMATIC_FRAME::findOrCreateCable( int aNetCode, const wxString& aCurrentName )
{
    for( const std::unique_ptr<CABLE>& c : m_cables )
    {
        if( c->GetNetCode() == aNetCode && aNetCode > 0 )
        {
            c->SetName( aCurrentName );
            return c.get();
        }
    }
    m_cables.push_back( std::make_unique<CABLE>( aCurrentName, aNetCode ) );
    return m_cables.back().get();
}


int CONDUIT_SCHEMATIC_FRAME::getSelectedNetCode() const
{
    long sel = m_cableListCtrl->GetNextItem( -1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED );
    if( sel < 0 )
        return -1;
    return static_cast<int>( m_cableListCtrl->GetItemData( sel ) );
}


wxString CONDUIT_SCHEMATIC_FRAME::getSelectedNetName() const
{
    long sel = m_cableListCtrl->GetNextItem( -1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED );
    if( sel < 0 )
        return wxEmptyString;
    return m_cableListCtrl->GetItemText( sel, 0 );
}


CONDUIT* CONDUIT_SCHEMATIC_FRAME::getSelectedConduit() const
{
    // Canvas selection takes priority — if nothing selected there, fall back to list.
    if( CONDUIT* fromCanvas = m_canvasPanel ? m_canvasPanel->GetSelected() : nullptr )
        return fromCanvas;

    long sel = m_conduitListCtrl->GetNextItem( -1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED );
    if( sel < 0 || sel >= static_cast<long>( m_conduits.size() ) )
        return nullptr;
    return m_conduits[ sel ].get();
}


void CONDUIT_SCHEMATIC_FRAME::assignSelectedCableToSelectedConduit()
{
    int      netCode = getSelectedNetCode();
    wxString netName = getSelectedNetName();
    CONDUIT* conduit = getSelectedConduit();

    if( netCode <= 0 )
    {
        wxMessageBox( _( "Select a cable from the Cables list first." ),
                      _( "No cable selected" ), wxICON_INFORMATION );
        return;
    }
    if( !conduit )
    {
        wxMessageBox( _( "Select a conduit (in the canvas or the Conduits list) first." ),
                      _( "No conduit selected" ), wxICON_INFORMATION );
        return;
    }

    CABLE* cable = findOrCreateCable( netCode, netName );

    // Reject duplicates within the same conduit.
    const auto& existing = conduit->GetCables();
    if( std::find( existing.begin(), existing.end(), cable ) != existing.end() )
    {
        SetStatusText( wxString::Format( _( "Cable '%s' is already in '%s'." ),
                                         netName, conduit->GetName() ) );
        return;
    }

    conduit->AddCable( cable );

    refreshConduitList();
    refreshCableList();   // also refreshes canvas
}


void CONDUIT_SCHEMATIC_FRAME::onAddConduit( wxCommandEvent& aEvent )
{
    wxString name = wxString::Format( wxT( "C-%d" ), m_nextConduitNumber++ );
    m_conduits.push_back( std::make_unique<CONDUIT>( name ) );

    refreshConduitList();
    refreshCableList();
    m_canvasPanel->RefreshLayout();

    // Auto-select the new conduit so subsequent cable assignments target it.
    m_canvasPanel->SetSelected( m_conduits.back().get() );
}


void CONDUIT_SCHEMATIC_FRAME::onRefreshCables( wxCommandEvent& aEvent )
{
    refreshCableList();
}


void CONDUIT_SCHEMATIC_FRAME::onAssignCable( wxCommandEvent& aEvent )
{
    assignSelectedCableToSelectedConduit();
}


void CONDUIT_SCHEMATIC_FRAME::onConduitListActivated( wxListEvent& aEvent )
{
    CONDUIT* conduit = getSelectedConduit();
    if( conduit )
        editConduit( conduit );
}


void CONDUIT_SCHEMATIC_FRAME::onConduitListSelected( wxListEvent& aEvent )
{
    long sel = aEvent.GetIndex();
    if( sel >= 0 && sel < static_cast<long>( m_conduits.size() ) )
        m_canvasPanel->SetSelected( m_conduits[ sel ].get() );
}


void CONDUIT_SCHEMATIC_FRAME::onCableActivated( wxListEvent& aEvent )
{
    // Double-click on a cable assigns it to the currently selected conduit.
    assignSelectedCableToSelectedConduit();
}


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
        refreshCableList();
        m_canvasPanel->RefreshLayout();
    }
}


void CONDUIT_SCHEMATIC_FRAME::onClose( wxCloseEvent& aEvent )
{
    Destroy();
}
