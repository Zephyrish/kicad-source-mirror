/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Conduit Schematic Editor — Phase 3 starter: visual canvas + cable assignment.
 */

#include "conduit_schematic_frame.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>

#include <nlohmann/json.hpp>

#include <base_units.h>
#include <board.h>
#include <footprint.h>
#include <netinfo.h>
#include <pad.h>

#include <project.h>
#include <project/project_file.h>
#include <project/net_settings.h>
#include <netclass.h>

#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/richmsgdlg.h>

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
    EVT_MENU( ID_CONDUIT_ADD, CONDUIT_SCHEMATIC_FRAME::onAddConduit )
    EVT_TOOL( ID_CONDUIT_ADD, CONDUIT_SCHEMATIC_FRAME::onAddConduit )
    EVT_TOOL( ID_CABLES_REFRESH, CONDUIT_SCHEMATIC_FRAME::onRefreshCables )
    EVT_TOOL( ID_ASSIGN_CABLE, CONDUIT_SCHEMATIC_FRAME::onAssignCable )
    EVT_MENU( wxID_SAVE,    CONDUIT_SCHEMATIC_FRAME::onSave )
    EVT_MENU( wxID_SAVEAS,  CONDUIT_SCHEMATIC_FRAME::onSaveAs )
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
    m_dirty( false ),
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
                setDirty( true );
            } );

    // Right-click on orphan → Re-link to currently-selected list cable.
    m_canvasPanel->SetOnCableRelink(
            [this]( CONDUIT* conduit, CABLE* cable )
            {
                if( !conduit || !cable )
                    return;

                int      targetCode = getSelectedNetCode();
                wxString targetName = getSelectedNetName();
                wxString targetFrom = getSelectedFromRef();
                wxString targetTo   = getSelectedToRef();
                if( targetCode <= 0 )
                    return;

                // If the user already has a CABLE for the target net (e.g., it's
                // assigned to another conduit), point this conduit at that CABLE
                // and discard the orphan record.
                CABLE* targetCable = findOrCreateCable( targetCode, targetFrom,
                                                       targetName, targetTo );

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
                setDirty( true );
            } );

    // Provide the "what's selected in the cable list" hint for the context menu.
    m_canvasPanel->SetRelinkTargetProvider(
            [this]() -> wxString
            {
                return getSelectedNetName();
            } );

    // Right-click on conduit body → Delete conduit (with confirmation).
    m_canvasPanel->SetOnConduitDelete(
            [this]( CONDUIT* c )
            {
                if( c )
                    deleteConduit( c );
            } );

    // Conduit dragged to a new location → mark dirty so user is prompted to save.
    m_canvasPanel->SetOnConduitMoved(
            [this]( CONDUIT* )
            {
                setDirty( true );
            } );

    // Auto-load the sidecar .kicad_cnd file if it exists for the current board.
    tryAutoLoad();
    updateTitle();
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
    fileMenu->Append( wxID_SAVE,   _( "&Save\tCtrl+S" ) );
    fileMenu->Append( wxID_SAVEAS, _( "Save &As...\tCtrl+Shift+S" ) );
    fileMenu->AppendSeparator();
    fileMenu->Append( wxID_CLOSE,  _( "&Close" ) );
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
    m_cableListCtrl->AppendColumn( _( "Net" ),         wxLIST_FORMAT_LEFT,  160 );
    m_cableListCtrl->AppendColumn( _( "From" ),        wxLIST_FORMAT_LEFT,  60 );
    m_cableListCtrl->AppendColumn( _( "To" ),          wxLIST_FORMAT_LEFT,  60 );
    m_cableListCtrl->AppendColumn( _( "Code" ),        wxLIST_FORMAT_RIGHT, 50 );
    m_cableListCtrl->AppendColumn( _( "Class" ),       wxLIST_FORMAT_LEFT,  90 );
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
        if( c->HasAllCableSizesKnown() )
        {
            m_conduitListCtrl->SetItem( idx, 4,
                wxString::Format( wxT( "%.1f%%" ), c->ComputeFillPercent() ) );
        }
        else
        {
            m_conduitListCtrl->SetItem( idx, 4, _( "error" ) );
        }
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
    // Also: netCode -> sorted set of footprint references on that net.
    std::unordered_map<int, std::vector<KIID>> padsByNet;
    std::unordered_map<int, std::set<wxString>> refsByNet;
    for( FOOTPRINT* fp : m_board->Footprints() )
    {
        const wxString ref = fp->GetReference();
        for( PAD* pad : fp->Pads() )
        {
            int code = pad->GetNetCode();
            if( code > 0 )
            {
                padsByNet[ code ].push_back( pad->m_Uuid );
                refsByNet[ code ].insert( ref );
            }
        }
    }

    // Helper to write From/To onto a CABLE for a known net code.
    // - For shared (<=2 component) nets, From/To are alphabetical first/last.
    // - For per-perspective (>=3 component) nets, From is the cable's existing
    //   identity component; To is the comma-joined list of other components.
    auto applyEndpoints = [&]( CABLE* cable, int netCode )
    {
        auto it = refsByNet.find( netCode );
        if( it == refsByNet.end() || it->second.empty() )
        {
            cable->SetFromRef( wxEmptyString );
            cable->SetToRef( wxEmptyString );
            return;
        }

        const std::set<wxString>& refs = it->second;

        if( refs.size() <= 2 )
        {
            cable->SetFromRef( *refs.begin() );
            cable->SetToRef( refs.size() == 1 ? wxString() : *refs.rbegin() );
            return;
        }

        // Per-perspective: keep this cable's stored From component if it still
        // exists on the net; otherwise fall back to alphabetical first.
        wxString fromRef = cable->GetFromRef();
        if( fromRef.IsEmpty() || refs.find( fromRef ) == refs.end() )
            fromRef = *refs.begin();
        cable->SetFromRef( fromRef );

        wxString to;
        for( const wxString& r : refs )
        {
            if( r == fromRef )
                continue;
            if( !to.IsEmpty() )
                to += wxT( ", " );
            to += r;
        }
        cable->SetToRef( to );
    };

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
            applyEndpoints( cable.get(), code );
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
            applyEndpoints( cable.get(), bestCode );
            cable->SetOrphan( false );
        }
        else
        {
            cable->SetOrphan( true );
            // Keep stale From/To so the engineer can still see what it used to be.
        }
    }

    // ---- Resolve cable area (mm^2) from the project's Cable Specs ----
    //
    // Lookup order:
    //   1. m_CableSpecsByNet[ "<netName>||<fromRef>" ]   (per-perspective override)
    //   2. m_CableSpecs[ <netClassName> ]                (per-class default)
    //   3. Unknown → leave area cleared, conduit fill will show error
    PROJECT_FILE& proj = Prj().GetProjectFile();
    std::shared_ptr<NET_SETTINGS> netSettings = proj.NetSettings();

    // Build a map of net name -> class name from NET_SETTINGS (default class as fallback).
    wxString defaultClassName = wxT( "Default" );
    if( netSettings )
    {
        if( auto def = netSettings->GetDefaultNetclass() )
        {
            wxString nm = def->GetName();
            if( !nm.IsEmpty() )
                defaultClassName = nm;
        }
    }

    auto areaFromOdInches = []( double aOdIn ) -> double
    {
        double radiusMm = ( aOdIn * 25.4 ) / 2.0;
        return M_PI * radiusMm * radiusMm;
    };

    for( const std::unique_ptr<CABLE>& cable : m_cables )
    {
        cable->ClearAreaKnown();
        const wxString& netName = cable->GetName();
        if( netName.IsEmpty() )
            continue;

        // 1. Per-perspective net override
        wxString key = netName + wxT( "||" ) + cable->GetFromRef();
        auto it = proj.m_CableSpecsByNet.find( key );
        if( it != proj.m_CableSpecsByNet.end() && it->second.outer_diameter_in > 0.0 )
        {
            cable->SetAreaMm2( areaFromOdInches( it->second.outer_diameter_in ) );
            continue;
        }

        // 2. Class-level spec — look up this net's class first
        wxString className = defaultClassName;
        if( netSettings )
        {
            const auto& assignments = netSettings->GetNetclassLabelAssignments();
            auto ait = assignments.find( netName );
            if( ait != assignments.end() && !ait->second.empty() )
                className = *ait->second.begin();
        }

        auto cit = proj.m_CableSpecs.find( className );
        if( cit != proj.m_CableSpecs.end() && cit->second.outer_diameter_in > 0.0 )
        {
            cable->SetAreaMm2( areaFromOdInches( cit->second.outer_diameter_in ) );
            continue;
        }

        // 3. No spec — leave m_areaKnown == false
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

    // netCode -> sorted set of component references
    std::unordered_map<int, std::set<wxString>> refsByNet;
    for( FOOTPRINT* fp : m_board->Footprints() )
    {
        const wxString ref = fp->GetReference();
        for( PAD* pad : fp->Pads() )
        {
            int code = pad->GetNetCode();
            if( code > 0 )
                refsByNet[ code ].insert( ref );
        }
    }

    // Build per-perspective assignment map: (netCode, fromRef) -> conduit names.
    // - For shared (<=2 component) cables in a conduit, BOTH perspective keys are
    //   marked assigned (so both rows show the conduit).
    // - For per-perspective cables, only the matching key is marked.
    std::map<std::pair<int, wxString>, wxString> assignments;

    auto markAssigned = [&]( int netCode, const wxString& persp, const wxString& conduitName )
    {
        wxString& a = assignments[ { netCode, persp } ];
        if( !a.IsEmpty() )
            a += wxT( ", " );
        a += conduitName;
    };

    for( const std::unique_ptr<CONDUIT>& c : m_conduits )
    {
        for( const CABLE* cable : c->GetCables() )
        {
            int code = cable->GetNetCode();
            auto rit = refsByNet.find( code );
            size_t refCount = ( rit != refsByNet.end() ) ? rit->second.size() : 0;

            if( refCount <= 2 )
            {
                // Shared cable: mark every perspective on this net.
                if( rit != refsByNet.end() )
                {
                    for( const wxString& r : rit->second )
                        markAssigned( code, r, c->GetName() );
                }
                // For nets with 0 board components but still assigned (orphan), use empty key.
                if( refCount == 0 )
                    markAssigned( code, wxEmptyString, c->GetName() );
            }
            else
            {
                // Per-perspective cable: only its own From perspective is marked.
                markAssigned( code, cable->GetFromRef(), c->GetName() );
            }
        }
    }

    // ------------------------------------------------------------------
    // Build one entry per (net, perspective). For a net touching components
    // [C5, R10], we emit:
    //     From=C5, To=R10
    //     From=R10, To=C5
    // For 3+ components, "To" becomes the other components comma-joined.
    // ------------------------------------------------------------------
    struct Entry
    {
        wxString fromRef;
        wxString toRef;
        wxString netName;
        int      netCode;
        wxString className;
        wxString assignedTo;
        bool     assigned;
    };

    std::vector<Entry> entries;
    int totalNets = 0;

    for( NETINFO_ITEM* net : m_board->GetNetInfo() )
    {
        if( !net || net->GetNetCode() <= 0 || net->GetNetname().IsEmpty() )
            continue;

        const wxString netName = net->GetNetname();
        const int      code    = net->GetNetCode();

        wxString className;
        if( net->GetNetClass() )
            className = net->GetNetClass()->GetName();

        auto rit = refsByNet.find( code );
        std::vector<wxString> refs;
        if( rit != refsByNet.end() )
            refs.assign( rit->second.begin(), rit->second.end() );

        // Skip nets that don't have both a From and a To — i.e. fewer than 2 components.
        if( refs.size() < 2 )
            continue;

        auto lookupAssign = [&]( const wxString& persp ) -> std::pair<wxString, bool>
        {
            auto it = assignments.find( { code, persp } );
            if( it == assignments.end() )
                return { wxString(), false };
            return { it->second, true };
        };

        {
            for( size_t i = 0; i < refs.size(); ++i )
            {
                wxString to;
                for( size_t j = 0; j < refs.size(); ++j )
                {
                    if( j == i )
                        continue;
                    if( !to.IsEmpty() )
                        to += wxT( ", " );
                    to += refs[ j ];
                }

                auto [assignedTo, isAssigned] = lookupAssign( refs[i] );
                Entry e{ refs[i], to, netName, code, className,
                         assignedTo, isAssigned };
                entries.push_back( std::move( e ) );
            }
        }

        totalNets++;
    }

    // Determine per-"From" group: is every entry with this From already assigned?
    std::map<wxString, bool> groupAllAssigned;
    for( const Entry& e : entries )
    {
        auto it = groupAllAssigned.find( e.fromRef );
        if( it == groupAllAssigned.end() )
            groupAllAssigned[ e.fromRef ] = e.assigned;
        else if( !e.assigned )
            it->second = false;
    }

    // Sort: groups with any unassigned cable first (alphabetical From within),
    // then fully-assigned groups at the bottom. Within a group, sort by net name.
    std::sort( entries.begin(), entries.end(),
            [&]( const Entry& a, const Entry& b )
            {
                bool aAll = groupAllAssigned[ a.fromRef ];
                bool bAll = groupAllAssigned[ b.fromRef ];
                if( aAll != bAll )
                    return !aAll;
                int c = a.fromRef.Cmp( b.fromRef );
                if( c != 0 )
                    return c < 0;
                return a.netName.Cmp( b.netName ) < 0;
            } );

    // Render flat list with blank separator rows between groups (by From).
    long     idx = 0;
    wxString prevFrom;
    bool     firstEntry = true;

    for( const Entry& e : entries )
    {
        if( !firstEntry && e.fromRef != prevFrom )
        {
            // Blank spacer row between groups
            m_cableListCtrl->InsertItem( idx, wxEmptyString );
            m_cableListCtrl->SetItemBackgroundColour( idx, wxColour( 240, 240, 235 ) );
            m_cableListCtrl->SetItemData( idx, 0 );    // 0 = not a cable row
            idx++;
        }

        m_cableListCtrl->InsertItem( idx, e.netName );
        m_cableListCtrl->SetItem( idx, 1, e.fromRef );
        m_cableListCtrl->SetItem( idx, 2, e.toRef );
        m_cableListCtrl->SetItem( idx, 3, wxString::Format( wxT( "%d" ), e.netCode ) );
        m_cableListCtrl->SetItem( idx, 4, e.className );
        m_cableListCtrl->SetItem( idx, 5, e.assignedTo );
        m_cableListCtrl->SetItemData( idx, static_cast<wxIntPtr>( e.netCode ) );

        if( e.assigned )
            m_cableListCtrl->SetItemTextColour( idx, wxColour( 120, 120, 120 ) );

        idx++;
        prevFrom   = e.fromRef;
        firstEntry = false;
    }

    SetStatusText( wxString::Format( _( "%d cable(s) from board  |  %zu conduit(s)" ),
                                     totalNets, m_conduits.size() ) );

    if( m_canvasPanel )
        m_canvasPanel->RefreshLayout();
}


int CONDUIT_SCHEMATIC_FRAME::countComponentsOnNet( int aNetCode ) const
{
    if( !m_board || aNetCode <= 0 )
        return 0;
    std::set<wxString> refs;
    for( FOOTPRINT* fp : m_board->Footprints() )
    {
        for( PAD* pad : fp->Pads() )
        {
            if( pad->GetNetCode() == aNetCode )
                refs.insert( fp->GetReference() );
        }
    }
    return static_cast<int>( refs.size() );
}


CABLE* CONDUIT_SCHEMATIC_FRAME::findOrCreateCable( int aNetCode, const wxString& aFromRef,
                                                   const wxString& aCurrentName,
                                                   const wxString& aToRef )
{
    const bool shared = ( countComponentsOnNet( aNetCode ) <= 2 );

    for( const std::unique_ptr<CABLE>& c : m_cables )
    {
        if( c->GetNetCode() != aNetCode || aNetCode <= 0 )
            continue;
        // For "shared" (point-to-point) nets, any cable for this net matches.
        // For "bus" nets, the perspective component must also match.
        if( !shared && c->GetFromRef() != aFromRef )
            continue;

        c->SetName( aCurrentName );
        if( !shared )
            c->SetToRef( aToRef );    // refresh the other-end list
        return c.get();
    }

    auto cable = std::make_unique<CABLE>( aCurrentName, aNetCode );
    if( !shared )
    {
        cable->SetFromRef( aFromRef );
        cable->SetToRef( aToRef );
    }
    // For shared cables, syncCablesFromBoard will populate fromRef/toRef alphabetically.

    m_cables.push_back( std::move( cable ) );
    return m_cables.back().get();
}


int CONDUIT_SCHEMATIC_FRAME::getSelectedNetCode() const
{
    long sel = m_cableListCtrl->GetNextItem( -1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED );
    if( sel < 0 )
        return -1;
    wxUIntPtr data = m_cableListCtrl->GetItemData( sel );
    if( data <= 0 )
        return -1;
    return static_cast<int>( data );
}


wxString CONDUIT_SCHEMATIC_FRAME::getSelectedNetName() const
{
    long sel = m_cableListCtrl->GetNextItem( -1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED );
    if( sel < 0 )
        return wxEmptyString;
    return m_cableListCtrl->GetItemText( sel, 0 );
}


wxString CONDUIT_SCHEMATIC_FRAME::getSelectedFromRef() const
{
    long sel = m_cableListCtrl->GetNextItem( -1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED );
    if( sel < 0 )
        return wxEmptyString;
    return m_cableListCtrl->GetItemText( sel, 1 );
}


wxString CONDUIT_SCHEMATIC_FRAME::getSelectedToRef() const
{
    long sel = m_cableListCtrl->GetNextItem( -1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED );
    if( sel < 0 )
        return wxEmptyString;
    return m_cableListCtrl->GetItemText( sel, 2 );
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
    wxString fromRef = getSelectedFromRef();
    wxString toRef   = getSelectedToRef();
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

    CABLE* cable = findOrCreateCable( netCode, fromRef, netName, toRef );

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
    setDirty( true );
}


void CONDUIT_SCHEMATIC_FRAME::onAddConduit( wxCommandEvent& aEvent )
{
    // Find the lowest unused "C-NNN" number, starting from 101.
    // This lets the engineer reclaim numbers freed by deletion.
    std::set<int> used;
    for( const std::unique_ptr<CONDUIT>& c : m_conduits )
    {
        const wxString& cname = c->GetName();
        if( !cname.StartsWith( wxT( "C-" ) ) )
            continue;
        long n;
        if( cname.Mid( 2 ).ToLong( &n ) && n > 0 )
            used.insert( static_cast<int>( n ) );
    }

    int chosen = 101;
    while( used.count( chosen ) )
        chosen++;

    // Keep m_nextConduitNumber loosely tracking what's been used so save/load works,
    // but the "lowest unused" search is what actually picks the name.
    m_nextConduitNumber = std::max( m_nextConduitNumber, chosen + 1 );

    wxString name = wxString::Format( wxT( "C-%d" ), chosen );
    auto newConduit = std::make_unique<CONDUIT>( name );

    // Cascade new conduits diagonally so successive Adds don't stack invisibly.
    int idx = static_cast<int>( m_conduits.size() );
    newConduit->SetPosition( 50 + ( idx % 6 ) * 30,
                             50 + ( idx % 6 ) * 30 );

    m_conduits.push_back( std::move( newConduit ) );

    refreshConduitList();
    refreshCableList();
    m_canvasPanel->RefreshLayout();

    // Auto-select the new conduit so subsequent cable assignments target it.
    m_canvasPanel->SetSelected( m_conduits.back().get() );

    setDirty( true );
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
        setDirty( true );
    }
}


// ============================================================================
// Delete conduit
// ============================================================================

void CONDUIT_SCHEMATIC_FRAME::deleteConduit( CONDUIT* aConduit )
{
    if( !aConduit )
        return;

    if( !m_skipDeleteConfirm )
    {
        wxRichMessageDialog dlg( this,
                wxString::Format(
                        _( "Delete conduit '%s'?\n\nIts %zu cable assignment(s) will be lost." ),
                        aConduit->GetName(), aConduit->GetCables().size() ),
                _( "Delete Conduit" ),
                wxYES_NO | wxICON_QUESTION );
        dlg.ShowCheckBox( _( "Don't ask again this session" ) );

        if( dlg.ShowModal() != wxID_YES )
            return;

        if( dlg.IsCheckBoxChecked() )
            m_skipDeleteConfirm = true;
    }

    auto it = std::find_if( m_conduits.begin(), m_conduits.end(),
            [aConduit]( const std::unique_ptr<CONDUIT>& p ) { return p.get() == aConduit; } );

    if( it == m_conduits.end() )
        return;

    m_conduits.erase( it );

    refreshConduitList();
    refreshCableList();
    setDirty( true );
}


// ============================================================================
// Dirty state + title
// ============================================================================

void CONDUIT_SCHEMATIC_FRAME::setDirty( bool aDirty )
{
    if( m_dirty == aDirty )
        return;
    m_dirty = aDirty;
    updateTitle();
}


void CONDUIT_SCHEMATIC_FRAME::updateTitle()
{
    wxString title = _( "Conduit Schematic Editor" );

    if( !m_filePath.IsEmpty() )
    {
        wxFileName fn( m_filePath );
        title += wxT( " - " ) + fn.GetFullName();
    }

    if( m_dirty )
        title += wxT( " *" );

    SetTitle( title );
}


// ============================================================================
// Persistence: file path derivation
// ============================================================================

wxString CONDUIT_SCHEMATIC_FRAME::deriveDefaultCndPath() const
{
    if( !m_board )
        return wxEmptyString;

    wxString boardPath = m_board->GetFileName();
    if( boardPath.IsEmpty() )
        return wxEmptyString;

    wxFileName fn( boardPath );
    fn.SetExt( wxT( "kicad_cnd" ) );
    return fn.GetFullPath();
}


void CONDUIT_SCHEMATIC_FRAME::tryAutoLoad()
{
    wxString path = deriveDefaultCndPath();
    if( path.IsEmpty() )
        return;

    if( !wxFileExists( path ) )
    {
        // No sidecar yet — treat the default path as our "save target" so the
        // user gets one-click save the first time.
        m_filePath = path;
        return;
    }

    loadFromFile( path );
}


// ============================================================================
// Persistence: serialize / deserialize
// ============================================================================

bool CONDUIT_SCHEMATIC_FRAME::saveToFile( const wxString& aPath )
{
    nlohmann::json j;
    j[ "version" ] = 1;
    j[ "next_conduit_number" ] = m_nextConduitNumber;
    j[ "conduits" ] = nlohmann::json::array();

    for( const std::unique_ptr<CONDUIT>& c : m_conduits )
    {
        nlohmann::json cj;
        cj[ "name" ]              = std::string( c->GetName().utf8_str() );
        cj[ "type" ]              = static_cast<int>( c->GetType() );
        cj[ "diameter_inches" ]   = c->GetDiameterInches();
        cj[ "max_fill_percent" ]  = c->GetMaxFillPercent();
        cj[ "pos_x" ]             = c->GetPosX();
        cj[ "pos_y" ]             = c->GetPosY();
        cj[ "cables" ]            = nlohmann::json::array();

        for( const CABLE* cable : c->GetCables() )
        {
            nlohmann::json kj;
            kj[ "name" ]      = std::string( cable->GetName().utf8_str() );
            kj[ "net_code" ]  = cable->GetNetCode();
            kj[ "from_ref" ]  = std::string( cable->GetFromRef().utf8_str() );
            kj[ "to_ref" ]    = std::string( cable->GetToRef().utf8_str() );
            kj[ "area_mm2" ]  = cable->GetAreaMm2();
            kj[ "pad_uuids" ] = nlohmann::json::array();
            for( const KIID& id : cable->GetPadIds() )
                kj[ "pad_uuids" ].push_back( id.AsStdString() );
            cj[ "cables" ].push_back( kj );
        }

        j[ "conduits" ].push_back( cj );
    }

    try
    {
        std::ofstream ofs( aPath.fn_str() );
        if( !ofs.is_open() )
        {
            wxMessageBox( wxString::Format( _( "Could not open '%s' for writing." ), aPath ),
                          _( "Save Failed" ), wxICON_ERROR, this );
            return false;
        }
        ofs << j.dump( 2 );
    }
    catch( const std::exception& e )
    {
        wxMessageBox( wxString::Format( _( "Save error: %s" ), e.what() ),
                      _( "Save Failed" ), wxICON_ERROR, this );
        return false;
    }

    m_filePath = aPath;
    setDirty( false );
    updateTitle();      // path may have changed (Save As)
    SetStatusText( wxString::Format( _( "Saved to %s" ), aPath ) );
    return true;
}


bool CONDUIT_SCHEMATIC_FRAME::loadFromFile( const wxString& aPath )
{
    nlohmann::json j;
    try
    {
        std::ifstream ifs( aPath.fn_str() );
        if( !ifs.is_open() )
        {
            wxMessageBox( wxString::Format( _( "Could not open '%s' for reading." ), aPath ),
                          _( "Load Failed" ), wxICON_ERROR, this );
            return false;
        }
        ifs >> j;
    }
    catch( const std::exception& e )
    {
        wxMessageBox( wxString::Format( _( "Load error: %s" ), e.what() ),
                      _( "Load Failed" ), wxICON_ERROR, this );
        return false;
    }

    // Replace current state
    m_conduits.clear();
    m_cables.clear();

    m_nextConduitNumber = j.value( "next_conduit_number", 101 );

    if( j.contains( "conduits" ) && j[ "conduits" ].is_array() )
    {
        for( const auto& cj : j[ "conduits" ] )
        {
            wxString name = wxString::FromUTF8( cj.value( "name", std::string() ).c_str() );
            auto conduit = std::make_unique<CONDUIT>( name );
            conduit->SetType( static_cast<CONDUIT_TYPE>( cj.value( "type", 0 ) ) );
            conduit->SetDiameterInches( cj.value( "diameter_inches", 2.0 ) );
            conduit->SetMaxFillPercent( cj.value( "max_fill_percent", 40.0 ) );
            conduit->SetPosition( cj.value( "pos_x", 50 ),
                                  cj.value( "pos_y", 50 ) );

            if( cj.contains( "cables" ) && cj[ "cables" ].is_array() )
            {
                for( const auto& kj : cj[ "cables" ] )
                {
                    wxString cableName = wxString::FromUTF8(
                            kj.value( "name", std::string() ).c_str() );
                    int      netCode   = kj.value( "net_code", -1 );

                    wxString fromRef;
                    wxString toRef;
                    if( kj.contains( "from_ref" ) )
                        fromRef = wxString::FromUTF8(
                                kj[ "from_ref" ].get<std::string>().c_str() );
                    if( kj.contains( "to_ref" ) )
                        toRef = wxString::FromUTF8(
                                kj[ "to_ref" ].get<std::string>().c_str() );

                    CABLE* cable = findOrCreateCable( netCode, fromRef, cableName, toRef );
                    cable->SetAreaMm2( kj.value( "area_mm2", 0.0 ) );
                    // Override what findOrCreateCable set, since the JSON values are authoritative
                    // until the next syncCablesFromBoard runs.
                    cable->SetFromRef( fromRef );
                    cable->SetToRef( toRef );

                    std::vector<KIID> padIds;
                    if( kj.contains( "pad_uuids" ) && kj[ "pad_uuids" ].is_array() )
                    {
                        for( const auto& pid : kj[ "pad_uuids" ] )
                        {
                            wxString uuidStr = wxString::FromUTF8(
                                    pid.get<std::string>().c_str() );
                            padIds.emplace_back( uuidStr );
                        }
                    }
                    cable->SetPadIds( padIds );

                    conduit->AddCable( cable );
                }
            }

            m_conduits.push_back( std::move( conduit ) );
        }
    }

    m_filePath = aPath;
    setDirty( false );
    refreshConduitList();
    refreshCableList();   // also triggers syncCablesFromBoard + canvas refresh
    updateTitle();
    SetStatusText( wxString::Format( _( "Loaded %s" ), aPath ) );
    return true;
}


bool CONDUIT_SCHEMATIC_FRAME::promptSaveIfDirty()
{
    if( !m_dirty )
        return true;

    int answer = wxMessageBox(
            _( "Conduit data has unsaved changes.\n\nSave before closing?" ),
            _( "Unsaved Changes" ),
            wxYES_NO | wxCANCEL | wxICON_QUESTION, this );

    if( answer == wxCANCEL )
        return false;
    if( answer == wxNO )
        return true;

    // YES — try to save
    if( m_filePath.IsEmpty() )
    {
        wxCommandEvent dummy;
        onSaveAs( dummy );
    }
    else
    {
        saveToFile( m_filePath );
    }

    // If still dirty, the save failed/cancelled — let the user decide again
    return !m_dirty;
}


// ============================================================================
// Save event handlers + close
// ============================================================================

void CONDUIT_SCHEMATIC_FRAME::onSave( wxCommandEvent& aEvent )
{
    if( m_filePath.IsEmpty() )
    {
        onSaveAs( aEvent );
        return;
    }
    saveToFile( m_filePath );
}


void CONDUIT_SCHEMATIC_FRAME::onSaveAs( wxCommandEvent& aEvent )
{
    wxString defaultPath = m_filePath.IsEmpty() ? deriveDefaultCndPath() : m_filePath;
    wxFileName fn( defaultPath );

    wxFileDialog dlg( this, _( "Save Conduit Data" ),
                      fn.GetPath(), fn.GetFullName(),
                      wxT( "KiCad Conduit Files (*.kicad_cnd)|*.kicad_cnd" ),
                      wxFD_SAVE | wxFD_OVERWRITE_PROMPT );

    if( dlg.ShowModal() != wxID_OK )
        return;

    saveToFile( dlg.GetPath() );
}


bool CONDUIT_SCHEMATIC_FRAME::canCloseWindow( wxCloseEvent& aCloseEvent )
{
    return promptSaveIfDirty();
}


void CONDUIT_SCHEMATIC_FRAME::onClose( wxCloseEvent& aEvent )
{
    // Unused now — EDA_BASE_FRAME drives the close via canCloseWindow().
    Destroy();
}
