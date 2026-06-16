/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Conduit Schematic Editor — Phase 3 starter: visual canvas + cable assignment.
 */

#include "conduit_schematic_frame.h"
#include "conduit/circuit_list_exporter.h"

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
#include <pcb_edit_frame.h>

#include <project.h>
#include <project/project_file.h>
#include <project/net_settings.h>
#include <netclass.h>

#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/richmsgdlg.h>
#include <wx/tokenzr.h>

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
    ID_EXPORT_CIRCUIT_LIST,
    ID_EXPORT_RACEWAY_LIST,
};


BEGIN_EVENT_TABLE( CONDUIT_SCHEMATIC_FRAME, KIWAY_PLAYER )
    EVT_MENU( ID_CONDUIT_ADD, CONDUIT_SCHEMATIC_FRAME::onAddConduit )
    EVT_TOOL( ID_CONDUIT_ADD, CONDUIT_SCHEMATIC_FRAME::onAddConduit )
    EVT_TOOL( ID_CABLES_REFRESH, CONDUIT_SCHEMATIC_FRAME::onRefreshCables )
    EVT_TOOL( ID_ASSIGN_CABLE, CONDUIT_SCHEMATIC_FRAME::onAssignCable )
    EVT_MENU( wxID_SAVE,    CONDUIT_SCHEMATIC_FRAME::onSave )
    EVT_MENU( wxID_SAVEAS,  CONDUIT_SCHEMATIC_FRAME::onSaveAs )
    EVT_MENU( ID_EXPORT_CIRCUIT_LIST, CONDUIT_SCHEMATIC_FRAME::onExportCircuitList )
    EVT_MENU( ID_EXPORT_RACEWAY_LIST, CONDUIT_SCHEMATIC_FRAME::onExportRacewayList )
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
    fileMenu->Append( ID_EXPORT_CIRCUIT_LIST, _( "Export &Circuit List (CSV)..." ) );
    fileMenu->Append( ID_EXPORT_RACEWAY_LIST, _( "Export &Raceway List (CSV)..." ) );
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
    m_conduitListCtrl->AppendColumn( _( "Name" ),       wxLIST_FORMAT_LEFT,  100 );
    m_conduitListCtrl->AppendColumn( _( "Type" ),       wxLIST_FORMAT_LEFT,  60 );
    m_conduitListCtrl->AppendColumn( _( "Dia (in)" ),   wxLIST_FORMAT_RIGHT, 70 );
    m_conduitListCtrl->AppendColumn( _( "Cables" ),     wxLIST_FORMAT_RIGHT, 60 );
    m_conduitListCtrl->AppendColumn( _( "Fill %" ),     wxLIST_FORMAT_RIGHT, 60 );
    m_conduitListCtrl->AppendColumn( _( "Length (ft)" ), wxLIST_FORMAT_RIGHT, 80 );
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


// Natural-order compare for conduit names like "C-9" < "C-10" < "C-122" < "C-126":
// split each into a non-digit prefix + trailing number and compare by (prefix, number).
static bool conduitNameLess( const wxString& a, const wxString& b )
{
    auto split = []( const wxString& s, wxString& pre, long& num, bool& has )
    {
        int i = (int) s.length();
        while( i > 0 && wxIsdigit( s.GetChar( i - 1 ) ) )
            i--;
        pre = s.Left( i );
        has = ( i < (int) s.length() );
        num = 0;
        if( has )
            s.Mid( i ).ToLong( &num );
    };

    wxString pa, pb; long na = 0, nb = 0; bool ha = false, hb = false;
    split( a, pa, na, ha );
    split( b, pb, nb, hb );

    if( pa != pb )
        return pa < pb;
    if( ha && hb )
        return na < nb;
    return a < b;
}


std::vector<wxString> CONDUIT_SCHEMATIC_FRAME::GetConduitNames() const
{
    std::vector<wxString> out;
    out.reserve( m_conduits.size() );
    for( const std::unique_ptr<CONDUIT>& c : m_conduits )
        out.push_back( c->GetName() );
    std::sort( out.begin(), out.end(), conduitNameLess );
    return out;
}


// Conversion: 1 inch = 1,000,000 / 12 IU (project convention: 1 mm = 1 ft).
static constexpr double IU_PER_INCH = 1000000.0 / 12.0;


std::vector<wxString> CONDUIT_SCHEMATIC_FRAME::GetRoutableConduitNames() const
{
    std::vector<wxString> out;
    for( const std::unique_ptr<CONDUIT>& c : m_conduits )
    {
        if( !c->GetSpecName().IsEmpty() )
            out.push_back( c->GetName() );
    }
    std::sort( out.begin(), out.end(), conduitNameLess );
    return out;
}


// Resolve a net's *assigned* class name (highest-priority non-Default constituent of
// the effective netclass), for matching against m_CableSpecs (keyed by class name).
static wxString assignedClassName( NET_SETTINGS* aNetSettings, const wxString& aNetName,
                                   const wxString& aDefaultName )
{
    if( !aNetSettings )
        return aDefaultName;

    std::shared_ptr<NETCLASS> nc = aNetSettings->GetEffectiveNetClass( aNetName );
    if( !nc )
        return aDefaultName;

    const std::vector<NETCLASS*>& constituents = nc->GetConstituentNetclasses();
    if( constituents.empty() )
        return nc->GetName();

    for( NETCLASS* c : constituents )
        if( c->GetName() != NETCLASS::Default )
            return c->GetName();

    return aDefaultName;
}


static FOOTPRINT* findFootprintByUuid( BOARD* aBoard, const KIID& aUuid )
{
    if( !aBoard )
        return nullptr;
    for( FOOTPRINT* fp : aBoard->Footprints() )
        if( fp->m_Uuid == aUuid )
            return fp;
    return nullptr;
}


std::vector<wxString> CONDUIT_SCHEMATIC_FRAME::GetRoutedConduitNames() const
{
    std::vector<wxString> out;
    for( const std::unique_ptr<CONDUIT>& c : m_conduits )
    {
        if( !c->GetSpecName().IsEmpty() && c->GetRoutePoints().size() >= 2 )
            out.push_back( c->GetName() );
    }
    std::sort( out.begin(), out.end(), conduitNameLess );
    return out;
}


bool CONDUIT_SCHEMATIC_FRAME::GetConduitRoute( const wxString& aConduitName, int& aLayer,
                                               std::vector<wxPoint>& aPoints ) const
{
    for( const std::unique_ptr<CONDUIT>& c : m_conduits )
    {
        if( c->GetName() != aConduitName )
            continue;
        if( c->GetRoutePoints().size() < 2 )
            return false;

        aLayer  = c->GetRouteLayer();
        aPoints = c->GetRoutePoints();
        return true;
    }
    return false;
}


int CONDUIT_SCHEMATIC_FRAME::GetConduitBendRadiusIu( const wxString& aConduitName ) const
{
    const CONDUIT* conduit = nullptr;
    for( const std::unique_ptr<CONDUIT>& c : m_conduits )
    {
        if( c->GetName() == aConduitName ) { conduit = c.get(); break; }
    }
    if( !conduit || conduit->GetSpecName().IsEmpty() )
        return 0;

    const PROJECT_FILE& proj = const_cast<CONDUIT_SCHEMATIC_FRAME*>( this )
                                       ->Prj().GetProjectFile();
    auto it = proj.m_ConduitSpecs.find( conduit->GetSpecName() );
    if( it == proj.m_ConduitSpecs.end() )
        return 0;
    return static_cast<int>( it->second.bend_radius_in * IU_PER_INCH );
}


double CONDUIT_SCHEMATIC_FRAME::GetConduitMaxBendAngleDeg( const wxString& aConduitName ) const
{
    const CONDUIT* conduit = nullptr;
    for( const std::unique_ptr<CONDUIT>& c : m_conduits )
    {
        if( c->GetName() == aConduitName ) { conduit = c.get(); break; }
    }
    if( !conduit || conduit->GetSpecName().IsEmpty() )
        return 90.0;

    const PROJECT_FILE& proj = const_cast<CONDUIT_SCHEMATIC_FRAME*>( this )
                                       ->Prj().GetProjectFile();
    auto it = proj.m_ConduitSpecs.find( conduit->GetSpecName() );
    if( it == proj.m_ConduitSpecs.end() )
        return 90.0;
    return it->second.max_bend_angle_deg;
}


bool CONDUIT_SCHEMATIC_FRAME::AnchorConduitEnd( const wxString& aConduitName, bool aAtFront,
                                                const KIID&    aFootprintUuid,
                                                const wxPoint& aFootprintOrigin )
{
    for( const std::unique_ptr<CONDUIT>& c : m_conduits )
    {
        if( c->GetName() != aConduitName )
            continue;
        if( c->GetRoutePoints().size() < 2 )
            return false;

        const wxPoint& endpoint = aAtFront ? c->GetRoutePoints().front()
                                           : c->GetRoutePoints().back();
        wxPoint offset( endpoint.x - aFootprintOrigin.x, endpoint.y - aFootprintOrigin.y );

        if( aAtFront )
            c->SetStartAnchor( aFootprintUuid, offset );
        else
            c->SetEndAnchor( aFootprintUuid, offset );

        refreshConduitList();
        refreshCableList();   // pushes overlay
        setDirty( true );
        if( !m_filePath.IsEmpty() )
            saveToFile( m_filePath );
        return true;
    }
    return false;
}


void CONDUIT_SCHEMATIC_FRAME::GetConduitAnchorFlags( const wxString& aConduitName,
                                                     bool& aStart, bool& aEnd ) const
{
    aStart = aEnd = false;
    for( const std::unique_ptr<CONDUIT>& c : m_conduits )
    {
        if( c->GetName() == aConduitName )
        {
            aStart = c->HasStartAnchor();
            aEnd   = c->HasEndAnchor();
            return;
        }
    }
}


void CONDUIT_SCHEMATIC_FRAME::RemoveConduitAnchor( const wxString& aConduitName, bool aAtFront )
{
    for( const std::unique_ptr<CONDUIT>& c : m_conduits )
    {
        if( c->GetName() != aConduitName )
            continue;

        if( aAtFront )
            c->ClearStartAnchor();
        else
            c->ClearEndAnchor();

        refreshConduitList();
        refreshCableList();
        setDirty( true );
        if( !m_filePath.IsEmpty() )
            saveToFile( m_filePath );
        return;
    }
}


void CONDUIT_SCHEMATIC_FRAME::EditRoutePointsDialog( const wxString& aConduitName )
{
    for( const std::unique_ptr<CONDUIT>& c : m_conduits )
    {
        if( c->GetName() != aConduitName )
            continue;

        if( editConduitRoutePoints( c.get() ) )   // dialog already set route + faulty
        {
            refreshConduitList();
            refreshCableList();    // pushes overlay
            if( !m_filePath.IsEmpty() )
                saveToFile( m_filePath );
        }
        return;
    }
}


void CONDUIT_SCHEMATIC_FRAME::ClearConduitRoute( const wxString& aConduitName )
{
    for( const std::unique_ptr<CONDUIT>& c : m_conduits )
    {
        if( c->GetName() != aConduitName )
            continue;

        c->ClearRoutePoints();
        c->ClearStartAnchor();
        c->ClearEndAnchor();
        c->SetFaulty( false );

        refreshConduitList();
        refreshCableList();
        setDirty( true );
        if( !m_filePath.IsEmpty() )
            saveToFile( m_filePath );
        return;
    }
}


bool CONDUIT_SCHEMATIC_FRAME::UpdateAnchoredEndpoints( BOARD* aBoard )
{
    if( !aBoard )
        return false;

    bool anyChanged = false;

    for( const std::unique_ptr<CONDUIT>& c : m_conduits )
    {
        if( c->GetRoutePoints().size() < 2 )
            continue;
        if( !c->HasStartAnchor() && !c->HasEndAnchor() )
            continue;

        std::vector<wxPoint> pts = c->GetRoutePoints();
        bool changed = false;
        bool faulty  = false;

        // Move the anchored endpoint(s) to track the footprint using the SAME rule as
        // the route-points editor: move the endpoint, then slide only the nearest
        // neighbour to keep angles valid. If it can't be made valid → error state.
        // Snapping is in the local board frame (Site Origin rotation does NOT tilt it).
        double maxBend = GetConduitMaxBendAngleDeg( c->GetName() );

        if( c->HasStartAnchor() )
        {
            if( FOOTPRINT* fp = findFootprintByUuid( aBoard, c->GetStartAnchor() ) )
            {
                wxPoint np( fp->GetPosition().x + c->GetStartOffset().x,
                            fp->GetPosition().y + c->GetStartOffset().y );
                if( np != pts.front() )
                {
                    if( !EditRouteNode( pts, 0, np, maxBend, /*siteRotRad*/ 0.0 ) )
                        faulty = true;
                    changed = true;
                }
            }
        }
        if( c->HasEndAnchor() )
        {
            if( FOOTPRINT* fp = findFootprintByUuid( aBoard, c->GetEndAnchor() ) )
            {
                wxPoint np( fp->GetPosition().x + c->GetEndOffset().x,
                            fp->GetPosition().y + c->GetEndOffset().y );
                if( np != pts.back() )
                {
                    if( !EditRouteNode( pts, (int) pts.size() - 1, np, maxBend, 0.0 ) )
                        faulty = true;
                    changed = true;
                }
            }
        }

        if( !changed )
            continue;

        c->SetRoutePoints( std::move( pts ) );
        c->SetFaulty( faulty );
        anyChanged = true;
    }

    if( anyChanged )
    {
        refreshConduitList();
        refreshCableList();   // pushes overlay
        setDirty( true );     // recomputable from anchors, so don't write file here
    }

    return anyChanged;
}


int CONDUIT_SCHEMATIC_FRAME::GetConduitClearanceIu( const wxString& aConduitName ) const
{
    const CONDUIT* conduit = nullptr;
    for( const std::unique_ptr<CONDUIT>& c : m_conduits )
    {
        if( c->GetName() == aConduitName ) { conduit = c.get(); break; }
    }
    if( !conduit || conduit->GetSpecName().IsEmpty() )
        return 0;

    const PROJECT_FILE& proj = const_cast<CONDUIT_SCHEMATIC_FRAME*>( this )
                                       ->Prj().GetProjectFile();
    auto it = proj.m_ConduitSpecs.find( conduit->GetSpecName() );
    if( it == proj.m_ConduitSpecs.end() )
        return 0;
    return static_cast<int>( it->second.clearance_in * IU_PER_INCH );
}


int CONDUIT_SCHEMATIC_FRAME::GetConduitHalfWidthIu( const wxString& aConduitName ) const
{
    for( const std::unique_ptr<CONDUIT>& c : m_conduits )
    {
        if( c->GetName() == aConduitName )
            return static_cast<int>( c->GetDiameterInches() * IU_PER_INCH * 0.5 );
    }
    return 0;
}


std::vector<CONDUIT_SCHEMATIC_FRAME::ROUTE_FOR_COLLISION>
CONDUIT_SCHEMATIC_FRAME::GetAllRoutesForCollision( const wxString& aExcludeName ) const
{
    std::vector<ROUTE_FOR_COLLISION> out;

    for( const std::unique_ptr<CONDUIT>& c : m_conduits )
    {
        if( c->GetName() == aExcludeName )
            continue;
        if( c->GetRoutePoints().size() < 2 )
            continue;

        ROUTE_FOR_COLLISION r;
        r.conduitName = c->GetName();
        r.layer       = c->GetRouteLayer();
        r.points      = c->GetRoutePoints();
        r.clearanceIu = GetConduitClearanceIu( c->GetName() );
        r.halfWidthIu = GetConduitHalfWidthIu( c->GetName() );
        out.push_back( std::move( r ) );
    }
    return out;
}


void CONDUIT_SCHEMATIC_FRAME::SetConduitRoute( const wxString& aConduitName, int aLayer,
                                               const std::vector<wxPoint>& aPoints )
{
    for( const std::unique_ptr<CONDUIT>& c : m_conduits )
    {
        if( c->GetName() != aConduitName )
            continue;

        c->SetRouteLayer( aLayer );
        c->SetFilletRadiusIu( GetConduitBendRadiusIu( aConduitName ) );
        c->SetRoutePoints( aPoints );    // also recomputes filleted horizontal length

        refreshConduitList();
        refreshCableList();   // also refreshes canvas + cache + overlay push
        setDirty( true );

        // Persist immediately so the PCB editor's on-board-load read sees it.
        if( !m_filePath.IsEmpty() )
            saveToFile( m_filePath );
        return;
    }
}


wxString CONDUIT_SCHEMATIC_FRAME::buildConnectionSummary( const CONDUIT* aConduit ) const
{
    std::shared_ptr<NET_SETTINGS> netSettings =
            const_cast<CONDUIT_SCHEMATIC_FRAME*>( this )->Prj().GetProjectFile().NetSettings();

    wxString defaultClassName = wxT( "Default" );
    if( netSettings && netSettings->GetDefaultNetclass() )
        defaultClassName = netSettings->GetDefaultNetclass()->GetName();

    std::set<wxString> refs;
    std::set<wxString> classes;

    for( const CABLE* cab : aConduit->GetCables() )
    {
        auto addRefs = [&]( const wxString& aField )
        {
            wxStringTokenizer tok( aField, wxT( "," ) );
            while( tok.HasMoreTokens() )
            {
                wxString r = tok.GetNextToken().Trim().Trim( false );
                if( !r.IsEmpty() )
                    refs.insert( r );
            }
        };
        addRefs( cab->GetFromRef() );
        addRefs( cab->GetToRef() );

        wxString cls = assignedClassName( netSettings.get(), cab->GetName(), defaultClassName );
        if( !cls.IsEmpty() && cls != defaultClassName )
            classes.insert( cls );
    }

    std::vector<wxString> parts( refs.begin(), refs.end() );
    parts.insert( parts.end(), classes.begin(), classes.end() );

    if( parts.empty() )
        return aConduit->GetName();

    wxString joined;
    for( size_t k = 0; k < parts.size(); ++k )
        joined += ( k ? wxT( " - " ) : wxT( "" ) ) + parts[k];

    return aConduit->GetName() + wxT( " (" ) + joined + wxT( ")" );
}


wxString CONDUIT_SCHEMATIC_FRAME::GetConduitConnectionLabel( const wxString& aConduitName ) const
{
    for( const std::unique_ptr<CONDUIT>& c : m_conduits )
    {
        if( c->GetName() == aConduitName )
            return buildConnectionSummary( c.get() );
    }
    return aConduitName;
}


std::vector<wxPoint> CONDUIT_SCHEMATIC_FRAME::GetConduitTerminationPoints(
        const wxString& aConduitName, BOARD* aBoard ) const
{
    std::vector<wxPoint> out;
    if( !aBoard )
        return out;

    for( const std::unique_ptr<CONDUIT>& c : m_conduits )
    {
        if( c->GetName() != aConduitName )
            continue;

        for( const CABLE* cab : c->GetCables() )
        {
            for( const KIID& id : cab->GetPadIds() )
            {
                if( BOARD_ITEM* item = aBoard->ResolveItem( id, /*aAllowNullptr*/ true ) )
                {
                    if( item->Type() == PCB_PAD_T )
                    {
                        VECTOR2I p = static_cast<PAD*>( item )->GetPosition();
                        out.emplace_back( p.x, p.y );
                    }
                }
            }
        }
        break;
    }
    return out;
}


int CONDUIT_SCHEMATIC_FRAME::GetConduitRouteLayer( const wxString& aConduitName ) const
{
    for( const std::unique_ptr<CONDUIT>& c : m_conduits )
    {
        if( c->GetName() == aConduitName )
            return c->GetRouteLayer();
    }
    return -1;
}


wxString CONDUIT_SCHEMATIC_FRAME::GetConduitNetClassName( const wxString& aConduitName ) const
{
    std::shared_ptr<NET_SETTINGS> netSettings =
            const_cast<CONDUIT_SCHEMATIC_FRAME*>( this )->Prj().GetProjectFile().NetSettings();

    wxString defaultClassName = wxT( "Default" );
    if( netSettings && netSettings->GetDefaultNetclass() )
        defaultClassName = netSettings->GetDefaultNetclass()->GetName();

    for( const std::unique_ptr<CONDUIT>& c : m_conduits )
    {
        if( c->GetName() != aConduitName )
            continue;

        for( const CABLE* cab : c->GetCables() )
        {
            wxString cls = assignedClassName( netSettings.get(), cab->GetName(),
                                              defaultClassName );
            if( !cls.IsEmpty() && cls != defaultClassName )
                return cls;     // first assigned (non-Default) class wins
        }
        break;
    }
    return wxEmptyString;
}


void CONDUIT_SCHEMATIC_FRAME::refreshConduitList()
{
    long selected = m_conduitListCtrl->GetNextItem( -1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED );

    m_conduitListCtrl->DeleteAllItems();

    for( size_t i = 0; i < m_conduits.size(); ++i )
    {
        const CONDUIT* c = m_conduits[i].get();

        long idx = m_conduitListCtrl->InsertItem( static_cast<long>( i ),
                                                  buildConnectionSummary( c ) );
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

        double totalLen = c->GetCachedTotalLengthFt();
        if( totalLen > 0.0 )
        {
            m_conduitListCtrl->SetItem( idx, 5,
                wxString::Format( wxT( "%.2f" ), totalLen ) );
        }
        else
        {
            m_conduitListCtrl->SetItem( idx, 5, wxT( "—" ) );
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

        // 2. Class-level spec — resolve this net's assigned class (honours pattern
        //    assignments + labels; the composite's GetName() would not match the key).
        wxString className = assignedClassName( netSettings.get(), netName, defaultClassName );

        auto cit = proj.m_CableSpecs.find( className );
        if( cit != proj.m_CableSpecs.end() && cit->second.outer_diameter_in > 0.0 )
        {
            cable->SetAreaMm2( areaFromOdInches( cit->second.outer_diameter_in ) );
            continue;
        }

        // 3. No spec — leave m_areaKnown == false
    }

    // ---- Re-resolve fillet radius from each conduit's current spec ----
    // The radius is NOT baked: resolving it here means a later spec edit
    // propagates to the filleted length + overlay on the next refresh.
    for( const std::unique_ptr<CONDUIT>& c : m_conduits )
        c->SetFilletRadiusIu( GetConduitBendRadiusIu( c->GetName() ) );

    // ---- Cache per-conduit total lengths + total bend angle (uses LayerDepths) ----
    for( const std::unique_ptr<CONDUIT>& c : m_conduits )
    {
        c->SetCachedTotalLengthFt( c->GetTotalLengthFt( proj.m_LayerDepthsInches ) );
        c->SetCachedTotalBendDeg( c->GetTotalBendAngleDeg( proj.m_LayerDepthsInches ) );

        int n22 = 0, n45 = 0, n67 = 0, n90 = 0;
        c->GetBendCounts( proj.m_LayerDepthsInches, n22, n45, n67, n90 );
        c->SetCachedBendCounts( n22, n45, n67, n90 );
    }

    // ---- Push route geometry to the PCB editor's overlay, if reachable ----
    if( PCB_EDIT_FRAME* pcbFrame = dynamic_cast<PCB_EDIT_FRAME*>( GetParent() ) )
    {
        std::vector<PCB_EDIT_FRAME::CONDUIT_ROUTE_INFO> routes;
        for( const std::unique_ptr<CONDUIT>& c : m_conduits )
        {
            if( c->GetRoutePoints().size() < 2 )
                continue;
            PCB_EDIT_FRAME::CONDUIT_ROUTE_INFO info;
            info.layer        = c->GetRouteLayer();
            info.points       = c->GetRoutePoints();
            info.bendRadiusIu = c->GetFilletRadiusIu();
            info.widthIu      = GetConduitHalfWidthIu( c->GetName() ) * 2;
            info.faulty       = c->IsFaulty();
            info.label        = c->GetName();

            // Anchor lines: footprint origin → the anchored endpoint.
            if( m_board )
            {
                if( c->HasStartAnchor() )
                    if( FOOTPRINT* fp = findFootprintByUuid( m_board, c->GetStartAnchor() ) )
                        info.anchorLines.emplace_back(
                                wxPoint( fp->GetPosition().x, fp->GetPosition().y ),
                                c->GetRoutePoints().front() );
                if( c->HasEndAnchor() )
                    if( FOOTPRINT* fp = findFootprintByUuid( m_board, c->GetEndAnchor() ) )
                        info.anchorLines.emplace_back(
                                wxPoint( fp->GetPosition().x, fp->GetPosition().y ),
                                c->GetRoutePoints().back() );
            }

            routes.push_back( std::move( info ) );
        }
        pcbFrame->UpdateConduitOverlay( routes );
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


bool CONDUIT_SCHEMATIC_FRAME::editConduitRoutePoints( CONDUIT* aConduit )
{
    // Manual point-list editor. Each row = one polyline vertex (X, Y in board IU).
    // Phase 4.F.2.B will replace this with click-to-draw in the PCB editor; this
    // exists as the interim path to validate the data + overlay pipeline.

    wxDialog dlg( this, wxID_ANY, _( "Edit Conduit Route Points" ),
                  wxDefaultPosition, wxSize( 860, 560 ),
                  wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER );
    dlg.SetMinSize( wxSize( 860, 480 ) );

    wxBoxSizer* outer = new wxBoxSizer( wxVERTICAL );
    outer->Add( new wxStaticText( &dlg, wxID_ANY,
            _( "Polyline points in board internal units (nanometers).\n"
               "1 ft = 1,000,000 IU (project convention: mm treated as ft).\n"
               "Need at least 2 points to define a polyline." ) ),
            0, wxALL, 10 );

    wxListCtrl* list = new wxListCtrl( &dlg, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                       wxLC_REPORT | wxLC_SINGLE_SEL );
    list->AppendColumn( wxT( "#" ),  wxLIST_FORMAT_RIGHT, 40 );
    list->AppendColumn( wxT( "X (IU)" ), wxLIST_FORMAT_RIGHT, 140 );
    list->AppendColumn( wxT( "Y (IU)" ), wxLIST_FORMAT_RIGHT, 140 );
    outer->Add( list, 1, wxEXPAND | wxLEFT | wxRIGHT, 10 );

    auto fillList = [&]( const std::vector<wxPoint>& pts )
    {
        list->DeleteAllItems();
        for( size_t i = 0; i < pts.size(); ++i )
        {
            long idx = list->InsertItem( static_cast<long>( i ),
                                         wxString::Format( wxT( "%zu" ), i + 1 ) );
            list->SetItem( idx, 1, wxString::Format( wxT( "%d" ), pts[i].x ) );
            list->SetItem( idx, 2, wxString::Format( wxT( "%d" ), pts[i].y ) );
        }
    };

    // Working copy
    std::vector<wxPoint> pts = aConduit->GetRoutePoints();
    fillList( pts );

    // Constraints for in-place editing. Snap is in the LOCAL board frame, so the
    // Site Origin rotation is intentionally NOT applied (siteRotRad = 0).
    double maxBendDeg = GetConduitMaxBendAngleDeg( aConduit->GetName() );
    double siteRotRad = 0.0;
    bool   editError  = false;   // last in-place edit left the route in an error state

    // Input row + Add button
    wxBoxSizer* inputRow = new wxBoxSizer( wxHORIZONTAL );
    wxTextCtrl* xCtrl = new wxTextCtrl( &dlg, wxID_ANY, wxT( "0" ),
                                        wxDefaultPosition, wxSize( 120, -1 ) );
    wxTextCtrl* yCtrl = new wxTextCtrl( &dlg, wxID_ANY, wxT( "0" ),
                                        wxDefaultPosition, wxSize( 120, -1 ) );
    wxButton* addBtn    = new wxButton( &dlg, wxID_ANY, _( "Add Point" ) );
    wxButton* updateBtn = new wxButton( &dlg, wxID_ANY, _( "Update Selected" ) );
    wxButton* removeBtn = new wxButton( &dlg, wxID_ANY, _( "Remove Selected" ) );
    wxButton* clearBtn  = new wxButton( &dlg, wxID_ANY, _( "Clear All" ) );

    inputRow->Add( new wxStaticText( &dlg, wxID_ANY, _( "X:" ) ),
                   0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4 );
    inputRow->Add( xCtrl, 0, wxRIGHT, 8 );
    inputRow->Add( new wxStaticText( &dlg, wxID_ANY, _( "Y:" ) ),
                   0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4 );
    inputRow->Add( yCtrl, 0, wxRIGHT, 8 );
    inputRow->Add( addBtn, 0, wxRIGHT, 4 );
    inputRow->Add( updateBtn, 0, wxRIGHT, 4 );
    inputRow->Add( removeBtn, 0, wxRIGHT, 4 );
    inputRow->Add( clearBtn, 0 );
    outer->Add( inputRow, 0, wxEXPAND | wxALL, 10 );

    outer->Add( dlg.CreateButtonSizer( wxOK | wxCANCEL ), 0, wxEXPAND | wxALL, 10 );
    dlg.SetSizer( outer );
    dlg.Layout();

    addBtn->Bind( wxEVT_BUTTON,
            [&]( wxCommandEvent& )
            {
                long x = 0, y = 0;
                xCtrl->GetValue().ToLong( &x );
                yCtrl->GetValue().ToLong( &y );
                pts.emplace_back( static_cast<int>( x ), static_cast<int>( y ) );
                fillList( pts );
            } );

    // Selecting a row loads its coordinates into the X/Y fields for editing.
    list->Bind( wxEVT_LIST_ITEM_SELECTED,
            [&]( wxListEvent& evt )
            {
                long sel = evt.GetIndex();
                if( sel >= 0 && sel < static_cast<long>( pts.size() ) )
                {
                    xCtrl->SetValue( wxString::Format( wxT( "%d" ), pts[sel].x ) );
                    yCtrl->SetValue( wxString::Format( wxT( "%d" ), pts[sel].y ) );
                }
            } );

    // Update the selected point in place, then slide neighbours to keep angles valid.
    updateBtn->Bind( wxEVT_BUTTON,
            [&]( wxCommandEvent& )
            {
                long sel = list->GetNextItem( -1, wxLIST_NEXT_ALL,
                                              wxLIST_STATE_SELECTED );
                if( sel < 0 || sel >= static_cast<long>( pts.size() ) )
                    return;

                long x = 0, y = 0;
                xCtrl->GetValue().ToLong( &x );
                yCtrl->GetValue().ToLong( &y );

                bool valid = EditRouteNode( pts, static_cast<int>( sel ),
                                            wxPoint( static_cast<int>( x ),
                                                     static_cast<int>( y ) ),
                                            maxBendDeg, siteRotRad );
                editError = !valid;
                fillList( pts );

                if( !valid )
                {
                    dlg.SetTitle( _( "Edit Conduit Route Points  —  ERROR: angle "
                                     "could not be kept valid" ) );
                }
                else
                {
                    dlg.SetTitle( _( "Edit Conduit Route Points" ) );
                }
            } );

    removeBtn->Bind( wxEVT_BUTTON,
            [&]( wxCommandEvent& )
            {
                long sel = list->GetNextItem( -1, wxLIST_NEXT_ALL,
                                              wxLIST_STATE_SELECTED );
                if( sel >= 0 && sel < static_cast<long>( pts.size() ) )
                {
                    pts.erase( pts.begin() + sel );
                    fillList( pts );
                }
            } );

    clearBtn->Bind( wxEVT_BUTTON,
            [&]( wxCommandEvent& )
            {
                pts.clear();
                fillList( pts );
            } );

    if( dlg.ShowModal() != wxID_OK )
        return false;

    aConduit->SetRoutePoints( pts );    // also recomputes horizontal length
    aConduit->SetFaulty( editError );   // error-state if the last edit stayed invalid
    setDirty( true );
    return true;
}


void CONDUIT_SCHEMATIC_FRAME::editConduit( CONDUIT* aConduit )
{
    wxDialog dlg( this, wxID_ANY, _( "Conduit Properties" ),
                  wxDefaultPosition, wxSize( 380, 380 ) );

    wxBoxSizer* sizer = new wxBoxSizer( wxVERTICAL );
    // cols-only constructor: rows grow as needed (we add 7 of them now).
    wxFlexGridSizer* grid = new wxFlexGridSizer( 2, 8, 8 );
    grid->AddGrowableCol( 1, 1 );

    grid->Add( new wxStaticText( &dlg, wxID_ANY, _( "Name:" ) ),
               0, wxALIGN_CENTER_VERTICAL );
    wxTextCtrl* nameCtrl = new wxTextCtrl( &dlg, wxID_ANY, aConduit->GetName() );
    grid->Add( nameCtrl, 1, wxEXPAND );

    // ---- Conduit Spec dropdown (picks values from project's Conduit Specs) ----
    grid->Add( new wxStaticText( &dlg, wxID_ANY, _( "Conduit Spec:" ) ),
               0, wxALIGN_CENTER_VERTICAL );
    wxChoice* specCtrl = new wxChoice( &dlg, wxID_ANY );
    specCtrl->Append( _( "(none)" ) );
    PROJECT_FILE& proj = Prj().GetProjectFile();
    for( const auto& [specName, specData] : proj.m_ConduitSpecs )
        specCtrl->Append( specName );

    int initialSpecIdx = 0;
    if( !aConduit->GetSpecName().IsEmpty() )
    {
        int found = specCtrl->FindString( aConduit->GetSpecName() );
        if( found != wxNOT_FOUND )
            initialSpecIdx = found;
    }
    specCtrl->SetSelection( initialSpecIdx );
    grid->Add( specCtrl, 1, wxEXPAND );

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

    // When the engineer picks a Conduit Spec, auto-fill Material + Diameter from it.
    // They can still edit those fields manually after.
    specCtrl->Bind( wxEVT_CHOICE,
            [&proj, specCtrl, typeCtrl, diaCtrl]( wxCommandEvent& )
            {
                wxString picked = specCtrl->GetStringSelection();
                if( picked.IsEmpty() || picked == _( "(none)" ) )
                    return;

                auto it = proj.m_ConduitSpecs.find( picked );
                if( it == proj.m_ConduitSpecs.end() )
                    return;
                const PROJECT_FILE::CONDUIT_SPEC& s = it->second;

                CONDUIT_TYPE t;
                if( !s.material.IsEmpty() && ConduitTypeFromString( s.material, t ) )
                    typeCtrl->SetSelection( static_cast<int>( t ) );

                if( s.inner_diameter_in > 0.0 )
                    diaCtrl->SetValue( wxString::Format( wxT( "%.3f" ),
                            s.inner_diameter_in ) );
            } );

    grid->Add( new wxStaticText( &dlg, wxID_ANY, _( "Max Fill %:" ) ),
               0, wxALIGN_CENTER_VERTICAL );
    wxTextCtrl* fillCtrl = new wxTextCtrl( &dlg, wxID_ANY,
            wxString::Format( wxT( "%.1f" ), aConduit->GetMaxFillPercent() ) );
    grid->Add( fillCtrl, 1, wxEXPAND );

    // ---- Routing (4.F.1: manual entry, draw tool comes later) ----
    grid->Add( new wxStaticText( &dlg, wxID_ANY, _( "Route Layer:" ) ),
               0, wxALIGN_CENTER_VERTICAL );

    wxChoice* layerCtrl = new wxChoice( &dlg, wxID_ANY );
    layerCtrl->Append( _( "(none)" ), reinterpret_cast<void*>( -1 ) );
    int initialLayerIdx = 0;
    if( m_board )
    {
        const LSET enabled = m_board->GetEnabledLayers();
        for( PCB_LAYER_ID layer : enabled.CuStack() )
        {
            int idx = layerCtrl->Append( m_board->GetLayerName( layer ),
                                         reinterpret_cast<void*>(
                                                 static_cast<intptr_t>( layer ) ) );
            if( layer == aConduit->GetRouteLayer() )
                initialLayerIdx = idx;
        }
    }
    layerCtrl->SetSelection( initialLayerIdx );
    grid->Add( layerCtrl, 1, wxEXPAND );

    grid->Add( new wxStaticText( &dlg, wxID_ANY, _( "Horizontal Length (ft):" ) ),
               0, wxALIGN_CENTER_VERTICAL );
    wxTextCtrl* lenCtrl = new wxTextCtrl( &dlg, wxID_ANY,
            wxString::Format( wxT( "%.2f" ), aConduit->GetHorizontalLengthFt() ) );
    grid->Add( lenCtrl, 1, wxEXPAND );

    // Route points summary
    grid->Add( new wxStaticText( &dlg, wxID_ANY, _( "Route Points:" ) ),
               0, wxALIGN_CENTER_VERTICAL );
    wxBoxSizer* routeRow = new wxBoxSizer( wxHORIZONTAL );
    wxStaticText* pointCountLbl = new wxStaticText( &dlg, wxID_ANY,
            wxString::Format( _( "%zu point(s)" ), aConduit->GetRoutePoints().size() ) );
    routeRow->Add( pointCountLbl, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8 );
    wxButton* editRouteBtn = new wxButton( &dlg, wxID_ANY, _( "Edit..." ) );
    routeRow->Add( editRouteBtn, 0 );
    grid->Add( routeRow, 1, wxEXPAND );

    sizer->Add( grid, 1, wxALL | wxEXPAND, 12 );

    // Hint about the depth dive math
    sizer->Add( new wxStaticText( &dlg, wxID_ANY,
            _( "Total = Horizontal Length + 2 × Layer Depth (depth dive at each end).\n"
               "If Route Points are set, Horizontal Length is computed from the polyline." ) ),
            0, wxLEFT | wxRIGHT | wxBOTTOM, 12 );

    sizer->Add( dlg.CreateButtonSizer( wxOK | wxCANCEL ), 0, wxEXPAND | wxALL, 8 );
    dlg.SetSizer( sizer );

    // Capture-by-reference lambda to open the route-points editor and update controls.
    editRouteBtn->Bind( wxEVT_BUTTON,
            [this, aConduit, lenCtrl, pointCountLbl]( wxCommandEvent& )
            {
                if( editConduitRoutePoints( aConduit ) )
                {
                    lenCtrl->SetValue( wxString::Format( wxT( "%.2f" ),
                            aConduit->GetHorizontalLengthFt() ) );
                    pointCountLbl->SetLabel( wxString::Format( _( "%zu point(s)" ),
                            aConduit->GetRoutePoints().size() ) );
                }
            } );

    if( dlg.ShowModal() == wxID_OK )
    {
        aConduit->SetName( nameCtrl->GetValue() );

        wxString chosenSpec = specCtrl->GetStringSelection();
        if( chosenSpec == _( "(none)" ) )
            chosenSpec.Clear();
        aConduit->SetSpecName( chosenSpec );

        aConduit->SetType( static_cast<CONDUIT_TYPE>( typeCtrl->GetSelection() ) );

        double dia = aConduit->GetDiameterInches();
        diaCtrl->GetValue().ToDouble( &dia );
        aConduit->SetDiameterInches( dia );

        double fill = aConduit->GetMaxFillPercent();
        fillCtrl->GetValue().ToDouble( &fill );
        aConduit->SetMaxFillPercent( fill );

        // Route layer (encoded in wxChoice client data)
        int layerSel = layerCtrl->GetSelection();
        intptr_t encoded = reinterpret_cast<intptr_t>( layerCtrl->GetClientData( layerSel ) );
        aConduit->SetRouteLayer( static_cast<int>( encoded ) );

        double len = aConduit->GetHorizontalLengthFt();
        lenCtrl->GetValue().ToDouble( &len );
        aConduit->SetHorizontalLengthFt( len );

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
        cj[ "spec_name" ]         = std::string( c->GetSpecName().utf8_str() );
        cj[ "type" ]              = static_cast<int>( c->GetType() );
        cj[ "diameter_inches" ]   = c->GetDiameterInches();
        cj[ "max_fill_percent" ]  = c->GetMaxFillPercent();
        cj[ "pos_x" ]             = c->GetPosX();
        cj[ "pos_y" ]             = c->GetPosY();
        cj[ "route_layer" ]       = c->GetRouteLayer();
        cj[ "bend_radius_iu" ]    = c->GetFilletRadiusIu();
        cj[ "horizontal_length_ft" ] = c->GetHorizontalLengthFt();

        nlohmann::json points = nlohmann::json::array();
        for( const wxPoint& p : c->GetRoutePoints() )
            points.push_back( { p.x, p.y } );
        cj[ "route_points" ] = points;

        // Equipment anchors (Phase 4.F.4)
        cj[ "has_start_anchor" ] = c->HasStartAnchor();
        cj[ "start_anchor" ]     = c->GetStartAnchor().AsStdString();
        cj[ "start_offset" ]     = { c->GetStartOffset().x, c->GetStartOffset().y };
        cj[ "has_end_anchor" ]   = c->HasEndAnchor();
        cj[ "end_anchor" ]       = c->GetEndAnchor().AsStdString();
        cj[ "end_offset" ]       = { c->GetEndOffset().x, c->GetEndOffset().y };
        cj[ "faulty" ]           = c->IsFaulty();

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
            conduit->SetSpecName( wxString::FromUTF8(
                    cj.value( "spec_name", std::string() ).c_str() ) );
            conduit->SetPosition( cj.value( "pos_x", 50 ),
                                  cj.value( "pos_y", 50 ) );
            conduit->SetRouteLayer( cj.value( "route_layer", -1 ) );
            conduit->SetFilletRadiusIu( cj.value( "bend_radius_iu", 0.0 ) );
            conduit->SetHorizontalLengthFt( cj.value( "horizontal_length_ft", 0.0 ) );

            if( cj.contains( "route_points" ) && cj[ "route_points" ].is_array() )
            {
                std::vector<wxPoint> pts;
                for( const auto& pj : cj[ "route_points" ] )
                {
                    if( pj.is_array() && pj.size() == 2 )
                        pts.emplace_back( pj[ 0 ].get<int>(), pj[ 1 ].get<int>() );
                }
                if( !pts.empty() )
                    conduit->SetRoutePoints( std::move( pts ) );
            }

            // Equipment anchors (Phase 4.F.4)
            auto readOffset = [&]( const char* aKey ) -> wxPoint
            {
                if( cj.contains( aKey ) && cj[ aKey ].is_array() && cj[ aKey ].size() == 2 )
                    return wxPoint( cj[ aKey ][ 0 ].get<int>(), cj[ aKey ][ 1 ].get<int>() );
                return wxPoint( 0, 0 );
            };
            if( cj.value( "has_start_anchor", false ) )
                conduit->SetStartAnchor( KIID( wxString::FromUTF8(
                        cj.value( "start_anchor", std::string() ).c_str() ) ),
                        readOffset( "start_offset" ) );
            if( cj.value( "has_end_anchor", false ) )
                conduit->SetEndAnchor( KIID( wxString::FromUTF8(
                        cj.value( "end_anchor", std::string() ).c_str() ) ),
                        readOffset( "end_offset" ) );
            conduit->SetFaulty( cj.value( "faulty", false ) );

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


void CONDUIT_SCHEMATIC_FRAME::onExportRacewayList( wxCommandEvent& aEvent )
{
    wxString defaultDir;
    wxString defaultName = wxT( "raceways.csv" );

    if( !m_filePath.IsEmpty() )
    {
        wxFileName cnd( m_filePath );
        defaultDir = cnd.GetPath();
        defaultName = cnd.GetName() + wxT( "-raceways.csv" );
    }
    else if( m_board && !m_board->GetFileName().IsEmpty() )
    {
        wxFileName board( m_board->GetFileName() );
        defaultDir = board.GetPath();
        defaultName = board.GetName() + wxT( "-raceways.csv" );
    }

    wxFileDialog dlg( this, _( "Export Raceway List" ),
                      defaultDir, defaultName,
                      wxT( "CSV files (*.csv)|*.csv" ),
                      wxFD_SAVE | wxFD_OVERWRITE_PROMPT );

    if( dlg.ShowModal() != wxID_OK )
        return;

    bool ok = RACEWAY_LIST_EXPORTER::ExportCsv( dlg.GetPath(), m_conduits );

    if( ok )
        SetStatusText( wxString::Format( _( "Raceway List exported to %s" ),
                                         dlg.GetPath() ) );
    else
        wxMessageBox( wxString::Format( _( "Failed to write %s" ), dlg.GetPath() ),
                      _( "Export Failed" ), wxICON_ERROR, this );
}


void CONDUIT_SCHEMATIC_FRAME::onExportCircuitList( wxCommandEvent& aEvent )
{
    // Default filename: <project>-circuits.csv next to the .kicad_cnd
    wxString defaultDir;
    wxString defaultName = wxT( "circuits.csv" );

    if( !m_filePath.IsEmpty() )
    {
        wxFileName cnd( m_filePath );
        defaultDir = cnd.GetPath();
        defaultName = cnd.GetName() + wxT( "-circuits.csv" );
    }
    else if( m_board && !m_board->GetFileName().IsEmpty() )
    {
        wxFileName board( m_board->GetFileName() );
        defaultDir = board.GetPath();
        defaultName = board.GetName() + wxT( "-circuits.csv" );
    }

    wxFileDialog dlg( this, _( "Export Circuit List" ),
                      defaultDir, defaultName,
                      wxT( "CSV files (*.csv)|*.csv" ),
                      wxFD_SAVE | wxFD_OVERWRITE_PROMPT );

    if( dlg.ShowModal() != wxID_OK )
        return;

    PROJECT_FILE& proj = Prj().GetProjectFile();
    bool ok = CIRCUIT_LIST_EXPORTER::ExportCsv( dlg.GetPath(), m_conduits, proj, m_board );

    if( ok )
        SetStatusText( wxString::Format( _( "Circuit List exported to %s" ),
                                         dlg.GetPath() ) );
    else
        wxMessageBox( wxString::Format( _( "Failed to write %s" ), dlg.GetPath() ),
                      _( "Export Failed" ), wxICON_ERROR, this );
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
