/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Site Layout — cable spec dialog (classes + per-net entries).
 */

#include "dialog_cable_specs.h"

#include <algorithm>
#include <set>

#include <sch_edit_frame.h>
#include <schematic.h>
#include <sch_screen.h>
#include <sch_sheet_path.h>
#include <sch_symbol.h>
#include <sch_pin.h>
#include <connection_graph.h>
#include <sch_connection.h>
#include <project.h>
#include <project/net_settings.h>
#include <netclass.h>
#include <settings/settings_manager.h>

#include <wx/button.h>
#include <wx/choice.h>
#include <wx/listctrl.h>
#include <wx/msgdlg.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>


// Per-perspective storage key for nets. Two cables on the same physical net
// are still different cables in real life (e.g., each "GND" branch is its own wire).
static wxString netSpecKey( const wxString& aNetName, const wxString& aFromRef )
{
    return aNetName + wxT( "||" ) + aFromRef;
}


// Defined later; used by onSaveClicked to re-find an edited cable type.
static wxString makeFingerprint( const PROJECT_FILE::CABLE_SPEC& s );


// Resolve the net's *assigned* class name for cable-spec keying. GetEffectiveNetClass
// returns a COMPOSITE (the assigned class layered over Default) for an assigned net, so
// we pull the highest-priority non-Default constituent. Falls back to the default name.
static wxString assignedClassName( NET_SETTINGS* aNetSettings, const wxString& aNetName,
                                   const wxString& aDefaultName )
{
    if( !aNetSettings )
        return aDefaultName;

    std::shared_ptr<NETCLASS> nc = aNetSettings->GetEffectiveNetClass( aNetName );
    if( !nc )
        return aDefaultName;

    const std::vector<NETCLASS*>& constituents = nc->GetConstituentNetclasses();

    if( constituents.empty() )         // single (non-composite) class
        return nc->GetName();

    for( NETCLASS* c : constituents )  // sorted highest-priority first
    {
        if( c->GetName() != NETCLASS::Default )
            return c->GetName();
    }
    return aDefaultName;
}


DIALOG_CABLE_SPECS::DIALOG_CABLE_SPECS( SCH_EDIT_FRAME* aParent,
                                        const wxString& aInitialName ) :
        wxDialog( aParent, wxID_ANY, _( "Cable Specs" ),
                  wxDefaultPosition, wxSize( 980, 620 ),
                  wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER ),
        m_frame( aParent ),
        m_initialName( aInitialName ),
        m_listCtrl( nullptr ),
        m_supplier( nullptr ),
        m_partNumber( nullptr ),
        m_outerDiameter( nullptr ),
        m_bendRadius( nullptr ),
        m_insulationType( nullptr ),
        m_jacketType( nullptr ),
        m_primaryQty( nullptr ),
        m_primaryConductors( nullptr ),
        m_primarySize( nullptr ),
        m_primaryUnit( nullptr ),
        m_secondaryQty( nullptr ),
        m_secondaryConductors( nullptr ),
        m_secondarySize( nullptr ),
        m_secondaryUnit( nullptr ),
        m_pickLibraryBtn( nullptr ),
        m_typesCtrl( nullptr )
{
    wxBoxSizer* outer = new wxBoxSizer( wxVERTICAL );

    outer->Add( new wxStaticText( this, wxID_ANY,
            _( "Physical cable properties per net class or per individual net.\n"
               "Net classes appear at the top for bulk assignment. Individual nets follow.\n"
               "Items with specs defined drop to the bottom of their group." ) ),
            0, wxALL, 12 );

    // Master-detail
    wxBoxSizer* split = new wxBoxSizer( wxHORIZONTAL );

    m_listCtrl = new wxListCtrl( this, wxID_ANY, wxDefaultPosition, wxSize( 440, -1 ),
                                 wxLC_REPORT | wxLC_SINGLE_SEL );
    m_listCtrl->AppendColumn( _( "Name" ),  wxLIST_FORMAT_LEFT,  170 );
    m_listCtrl->AppendColumn( _( "From" ),  wxLIST_FORMAT_LEFT,  60 );
    m_listCtrl->AppendColumn( _( "To" ),    wxLIST_FORMAT_LEFT,  60 );
    m_listCtrl->AppendColumn( _( "Code" ),  wxLIST_FORMAT_RIGHT, 50 );
    m_listCtrl->AppendColumn( _( "Class" ), wxLIST_FORMAT_LEFT,  90 );

    split->Add( m_listCtrl, 0, wxEXPAND | wxRIGHT, 12 );

    // Form panel (scrollable)
    wxScrolledWindow* formScroll = new wxScrolledWindow( this, wxID_ANY,
            wxDefaultPosition, wxDefaultSize, wxVSCROLL );
    formScroll->SetScrollRate( 0, 20 );

    wxStaticBoxSizer* form = new wxStaticBoxSizer( wxVERTICAL, formScroll,
                                                   _( "Cable Properties" ) );
    wxWindow* formParent = form->GetStaticBox();

    // Library picker
    wxBoxSizer* libRow = new wxBoxSizer( wxHORIZONTAL );
    libRow->Add( new wxStaticText( formParent, wxID_ANY, _( "From library:" ) ),
                 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8 );
    m_pickLibraryBtn = new wxButton( formParent, wxID_ANY, _( "Pick from Library..." ) );
    m_pickLibraryBtn->Disable();
    m_pickLibraryBtn->SetToolTip( _( "Cable library not yet implemented" ) );
    libRow->Add( m_pickLibraryBtn, 1, wxEXPAND );
    form->Add( libRow, 0, wxEXPAND | wxALL, 8 );

    // Physical
    wxFlexGridSizer* phys = new wxFlexGridSizer( 6, 2, 6, 8 );
    phys->AddGrowableCol( 1, 1 );

    auto addRow = [&]( wxFlexGridSizer* g, wxWindow* parent, const wxString& label, wxWindow* ctrl )
    {
        g->Add( new wxStaticText( parent, wxID_ANY, label ),
                0, wxALIGN_CENTER_VERTICAL );
        g->Add( ctrl, 1, wxEXPAND );
    };

    m_supplier       = new wxTextCtrl( formParent, wxID_ANY );
    m_partNumber     = new wxTextCtrl( formParent, wxID_ANY );
    m_outerDiameter  = new wxTextCtrl( formParent, wxID_ANY );
    m_bendRadius     = new wxTextCtrl( formParent, wxID_ANY );
    m_insulationType = new wxTextCtrl( formParent, wxID_ANY );
    m_jacketType     = new wxTextCtrl( formParent, wxID_ANY );

    addRow( phys, formParent, _( "Supplier:" ),                m_supplier );
    addRow( phys, formParent, _( "Part Number:" ),             m_partNumber );
    addRow( phys, formParent, _( "Outer Diameter (in):" ),      m_outerDiameter );
    addRow( phys, formParent, _( "Min Bend Radius (in):" ),     m_bendRadius );
    addRow( phys, formParent, _( "Insulation Type (TYPE):" ),   m_insulationType );
    addRow( phys, formParent, _( "Jacket Type:" ),              m_jacketType );

    form->Add( phys, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8 );

    wxArrayString sizeUnits;
    sizeUnits.Add( wxT( "AWG" ) );
    sizeUnits.Add( wxT( "KCMIL" ) );

    auto buildConductorBox = [&]( const wxString& title,
                                  wxSpinCtrl*&  qtyOut,
                                  wxSpinCtrl*&  condOut,
                                  wxTextCtrl*&  sizeOut,
                                  wxChoice*&    unitOut )
    {
        wxStaticBoxSizer* box = new wxStaticBoxSizer( wxVERTICAL, formParent, title );
        wxWindow*         bp  = box->GetStaticBox();

        wxFlexGridSizer* g = new wxFlexGridSizer( 3, 2, 6, 8 );
        g->AddGrowableCol( 1, 1 );

        qtyOut  = new wxSpinCtrl( bp, wxID_ANY );
        qtyOut->SetRange( 0, 100 );
        condOut = new wxSpinCtrl( bp, wxID_ANY );
        condOut->SetRange( 0, 100 );

        addRow( g, bp, _( "QTY:" ),        qtyOut );
        addRow( g, bp, _( "Conductors:" ), condOut );

        // Size: value + unit dropdown on same row
        wxBoxSizer* sizeRow = new wxBoxSizer( wxHORIZONTAL );
        sizeOut = new wxTextCtrl( bp, wxID_ANY );
        unitOut = new wxChoice( bp, wxID_ANY, wxDefaultPosition, wxDefaultSize, sizeUnits );
        unitOut->SetSelection( 0 );
        sizeRow->Add( sizeOut, 1, wxEXPAND | wxRIGHT, 4 );
        sizeRow->Add( unitOut, 0 );

        g->Add( new wxStaticText( bp, wxID_ANY, _( "Size:" ) ),
                0, wxALIGN_CENTER_VERTICAL );
        g->Add( sizeRow, 1, wxEXPAND );

        box->Add( g, 0, wxEXPAND | wxALL, 6 );
        return box;
    };

    form->Add( buildConductorBox( _( "Primary" ),
                                  m_primaryQty, m_primaryConductors,
                                  m_primarySize, m_primaryUnit ),
               0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8 );

    form->Add( buildConductorBox( _( "Secondary (optional)" ),
                                  m_secondaryQty, m_secondaryConductors,
                                  m_secondarySize, m_secondaryUnit ),
               0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8 );

    wxBoxSizer* scrollSizer = new wxBoxSizer( wxVERTICAL );
    scrollSizer->Add( form, 0, wxEXPAND | wxALL, 4 );
    formScroll->SetSizer( scrollSizer );
    formScroll->FitInside();

    split->Add( formScroll, 1, wxEXPAND );
    outer->Add( split, 1, wxEXPAND | wxLEFT | wxRIGHT, 12 );

    // ----- Cable Types panel -----
    wxStaticBoxSizer* typesBox = new wxStaticBoxSizer( wxVERTICAL, this,
                                                       _( "Cable Types Defined" ) );

    m_typesCtrl = new wxListCtrl( typesBox->GetStaticBox(), wxID_ANY,
                                  wxDefaultPosition, wxSize( -1, 110 ),
                                  wxLC_REPORT | wxLC_SINGLE_SEL );
    m_typesCtrl->AppendColumn( _( "Display Name" ), wxLIST_FORMAT_LEFT, 280 );
    m_typesCtrl->AppendColumn( _( "Source" ),       wxLIST_FORMAT_LEFT, 80 );
    m_typesCtrl->AppendColumn( _( "Used By" ),      wxLIST_FORMAT_LEFT, 380 );
    typesBox->Add( m_typesCtrl, 1, wxEXPAND | wxALL, 4 );

    outer->Add( typesBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12 );

    // Button row: Save and Delete on the left, OK/Cancel on the right
    wxBoxSizer* buttonRow = new wxBoxSizer( wxHORIZONTAL );
    wxButton* saveBtn = new wxButton( this, wxID_ANY, _( "Save" ) );
    saveBtn->SetToolTip( _( "Commit the current entry without closing the dialog" ) );
    buttonRow->Add( saveBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4 );

    wxButton* deleteBtn = new wxButton( this, wxID_ANY, _( "Delete Spec" ) );
    deleteBtn->SetToolTip( _( "Remove the cable spec assigned to this row" ) );
    buttonRow->Add( deleteBtn, 0, wxALIGN_CENTER_VERTICAL );

    buttonRow->AddStretchSpacer();
    buttonRow->Add( CreateButtonSizer( wxOK | wxCANCEL ), 0 );
    outer->Add( buttonRow, 0, wxEXPAND | wxALL, 12 );

    saveBtn->Bind( wxEVT_BUTTON,
            [this]( wxCommandEvent& e ) { onSaveClicked( e ); } );
    deleteBtn->Bind( wxEVT_BUTTON,
            [this]( wxCommandEvent& e ) { onDeleteClicked( e ); } );

    SetSizer( outer );
    Layout();

    m_listCtrl->Bind( wxEVT_LIST_ITEM_SELECTED,
            [this]( wxListEvent& e ) { onRowSelected( e ); } );
    m_typesCtrl->Bind( wxEVT_LIST_ITEM_SELECTED,
            [this]( wxListEvent& e ) { onTypeSelected( e ); } );
    m_pickLibraryBtn->Bind( wxEVT_BUTTON,
            [this]( wxCommandEvent& e ) { onPickFromLibrary( e ); } );

    TransferDataToWindow();
}


// --- Build the row list ---------------------------------------------------

void DIALOG_CABLE_SPECS::buildRows()
{
    m_rows.clear();

    // -- Net classes (always first) --
    std::vector<wxString> classNames;

    {
        wxString defaultName = wxT( "Default" );
        if( auto netSettings = m_frame->Prj().GetProjectFile().NetSettings() )
        {
            if( auto def = netSettings->GetDefaultNetclass() )
            {
                wxString nm = def->GetName();
                if( !nm.IsEmpty() )
                    defaultName = nm;
            }
            classNames.push_back( defaultName );

            for( const auto& [name, nc] : netSettings->GetNetclasses() )
            {
                if( std::find( classNames.begin(), classNames.end(), name )
                    == classNames.end() )
                    classNames.push_back( name );
            }
        }
        else
        {
            classNames.push_back( defaultName );
        }
    }

    // Sort: unspecced classes alphabetical first, then specced classes alphabetical.
    auto classHasSpec = [&]( const wxString& n )
    {
        return m_classSpecs.find( n ) != m_classSpecs.end();
    };
    std::sort( classNames.begin(), classNames.end(),
            [&]( const wxString& a, const wxString& b )
            {
                bool aS = classHasSpec( a );
                bool bS = classHasSpec( b );
                if( aS != bS ) return !aS;     // unspecced first
                return a.Cmp( b ) < 0;
            } );

    for( const wxString& n : classNames )
    {
        Row r;
        r.kind     = ROW_KIND::CLASS;
        r.name     = n;
        r.netCode  = -1;
        r.hasSpec  = classHasSpec( n );
        m_rows.push_back( r );
    }

    // -- Individual nets from the schematic --
    SCHEMATIC* schematic = &m_frame->Schematic();
    if( !schematic )
        return;

    CONNECTION_GRAPH* graph = schematic->ConnectionGraph();
    if( !graph )
        return;

    // Build {netName -> {component refs}}  by walking all symbol pins
    std::map<wxString, std::set<wxString>> netToRefs;
    std::map<wxString, int>                netToCode;
    std::map<wxString, wxString>           netToClass;

    SCH_SHEET_LIST sheetList = schematic->Hierarchy();
    for( const SCH_SHEET_PATH& path : sheetList )
    {
        SCH_SCREEN* screen = path.LastScreen();
        if( !screen )
            continue;

        for( SCH_ITEM* item : screen->Items() )
        {
            if( item->Type() != SCH_SYMBOL_T )
                continue;
            SCH_SYMBOL* sym = static_cast<SCH_SYMBOL*>( item );
            wxString ref = sym->GetRef( &path, true );

            for( SCH_PIN* pin : sym->GetPins( &path ) )
            {
                if( SCH_CONNECTION* conn = pin->Connection( &path ) )
                {
                    wxString n = conn->Name();
                    if( n.IsEmpty() )
                        continue;
                    netToRefs[ n ].insert( ref );
                    netToCode[ n ] = conn->NetCode();
                }
            }
        }
    }

    // Map each net to its EFFECTIVE class. GetEffectiveNetClass() honours pattern
    // assignments, label assignments, and the default class — whereas the label
    // assignments alone miss nets put into a class via membership patterns, which
    // is how class-level cable specs failed to reach their nets.
    wxString defaultClassName = wxT( "Default" );
    if( auto netSettings = m_frame->Prj().GetProjectFile().NetSettings() )
    {
        if( auto def = netSettings->GetDefaultNetclass() )
            defaultClassName = def->GetName();

        for( const auto& [netName, refs] : netToRefs )
            netToClass[ netName ] = assignedClassName( netSettings.get(), netName,
                                                       defaultClassName );
    }

    // Generate per-perspective net rows; each perspective gets its own spec.
    auto netHasSpec = [&]( const wxString& n, const wxString& f )
    {
        return m_netSpecs.find( netSpecKey( n, f ) ) != m_netSpecs.end();
    };

    std::vector<Row> netRows;
    for( const auto& [netName, refs] : netToRefs )
    {
        std::vector<wxString> refsVec( refs.begin(), refs.end() );
        // refs is std::set => already sorted

        if( refsVec.size() < 2 )
            continue;   // need both From and To to be meaningful

        for( size_t i = 0; i < refsVec.size(); ++i )
        {
            wxString to;
            for( size_t j = 0; j < refsVec.size(); ++j )
            {
                if( j == i ) continue;
                if( !to.IsEmpty() ) to += wxT( ", " );
                to += refsVec[j];
            }

            Row r;
            r.kind     = ROW_KIND::NET;
            r.name     = netName;
            r.fromRef  = refsVec[i];
            r.toRef    = to;
            r.netCode  = netToCode[ netName ];
            r.netClass = netToClass[ netName ];
            // "Assigned" (gray) if it has a per-net spec OR inherits one from its class.
            r.hasSpec  = netHasSpec( netName, refsVec[i] )
                         || ( m_classSpecs.find( r.netClass ) != m_classSpecs.end() );
            netRows.push_back( r );
        }
    }

    // Sort net rows: unspecced first (by From, then by netName), specced last.
    std::sort( netRows.begin(), netRows.end(),
            [&]( const Row& a, const Row& b )
            {
                if( a.hasSpec != b.hasSpec )
                    return !a.hasSpec;
                int c = a.fromRef.Cmp( b.fromRef );
                if( c != 0 )
                    return c < 0;
                return a.name.Cmp( b.name ) < 0;
            } );

    for( Row& r : netRows )
        m_rows.push_back( std::move( r ) );
}


// --- Render to wxListCtrl -------------------------------------------------

void DIALOG_CABLE_SPECS::renderList()
{
    m_listCtrl->DeleteAllItems();

    wxString prevSeparatorMarker;
    long     idx = 0;
    bool     inNetSection = false;

    for( size_t i = 0; i < m_rows.size(); ++i )
    {
        const Row& r = m_rows[i];

        // Visual separator between classes and nets
        if( !inNetSection && r.kind == ROW_KIND::NET )
        {
            m_listCtrl->InsertItem( idx, wxT( "──── Individual Nets ────" ) );
            m_listCtrl->SetItemBackgroundColour( idx, wxColour( 220, 220, 220 ) );
            m_listCtrl->SetItemData( idx, -1 );   // sentinel
            idx++;
            inNetSection = true;
        }

        if( r.kind == ROW_KIND::CLASS )
        {
            m_listCtrl->InsertItem( idx, wxT( "[Class]  " ) + r.name );
            m_listCtrl->SetItem( idx, 4, r.name );
        }
        else
        {
            m_listCtrl->InsertItem( idx, r.name );
            m_listCtrl->SetItem( idx, 1, r.fromRef );
            m_listCtrl->SetItem( idx, 2, r.toRef );
            m_listCtrl->SetItem( idx, 3, wxString::Format( wxT( "%d" ), r.netCode ) );
            m_listCtrl->SetItem( idx, 4, r.netClass );
        }

        if( r.hasSpec )
            m_listCtrl->SetItemTextColour( idx, wxColour( 120, 120, 120 ) );

        m_listCtrl->SetItemData( idx, static_cast<long>( i ) );
        idx++;
    }
}


// --- Transfer in/out ------------------------------------------------------

bool DIALOG_CABLE_SPECS::TransferDataToWindow()
{
    PROJECT_FILE& proj = m_frame->Prj().GetProjectFile();
    m_classSpecs = proj.m_CableSpecs;
    m_netSpecs   = proj.m_CableSpecsByNet;

    buildRows();
    renderList();

    // Pick the initial row
    if( !m_rows.empty() )
    {
        long pick = 0;
        if( !m_initialName.IsEmpty() )
        {
            for( size_t i = 0; i < m_rows.size(); ++i )
            {
                if( m_rows[i].name == m_initialName )
                {
                    pick = static_cast<long>( i );
                    break;
                }
            }
        }

        // Skip the separator if it lands on us; advance to next data row.
        long listIdx = 0;
        for( long i = 0; i < m_listCtrl->GetItemCount(); ++i )
        {
            if( static_cast<long>( m_listCtrl->GetItemData( i ) ) == pick )
            {
                listIdx = i;
                break;
            }
        }

        m_listCtrl->SetItemState( listIdx, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED );
        m_currentKind    = m_rows[ pick ].kind;
        m_currentName    = m_rows[ pick ].name;
        m_currentFromRef = m_rows[ pick ].fromRef;
        loadFormFromKey( m_currentKind, m_currentName );
    }

    updateCableTypesPanel();
    return true;
}


bool DIALOG_CABLE_SPECS::TransferDataFromWindow()
{
    commitFormToCurrent();

    PROJECT_FILE& proj = m_frame->Prj().GetProjectFile();

    auto isEmpty = []( const PROJECT_FILE::CABLE_SPEC& s )
    {
        return s.supplier.IsEmpty() && s.part_number.IsEmpty()
            && s.outer_diameter_in == 0.0 && s.bend_radius_in == 0.0
            && s.insulation_type.IsEmpty() && s.jacket_type.IsEmpty()
            && s.primary_qty == 0 && s.primary_conductors == 0
            && s.primary_size_value == 0.0
            && s.secondary_qty == 0 && s.secondary_conductors == 0
            && s.secondary_size_value == 0.0;
    };

    proj.m_CableSpecs.clear();
    for( const auto& [name, spec] : m_classSpecs )
    {
        if( !isEmpty( spec ) )
            proj.m_CableSpecs[ name ] = spec;
    }

    proj.m_CableSpecsByNet.clear();
    for( const auto& [name, spec] : m_netSpecs )
    {
        if( !isEmpty( spec ) )
            proj.m_CableSpecsByNet[ name ] = spec;
    }

    if( SETTINGS_MANAGER* mgr = m_frame->GetSettingsManager() )
        mgr->SaveProject();

    return true;
}


// --- Form load / commit ---------------------------------------------------

void DIALOG_CABLE_SPECS::commitFormToCurrent()
{
    if( m_currentName.IsEmpty() )
        return;

    wxString key;
    if( m_currentKind == ROW_KIND::CLASS )
        key = m_currentName;
    else
        key = netSpecKey( m_currentName, m_currentFromRef );

    auto& target = ( m_currentKind == ROW_KIND::CLASS ) ? m_classSpecs : m_netSpecs;
    readForm( target[ key ] );
}


void DIALOG_CABLE_SPECS::readForm( PROJECT_FILE::CABLE_SPEC& spec ) const
{
    spec.supplier         = m_supplier->GetValue();
    spec.part_number      = m_partNumber->GetValue();
    m_outerDiameter->GetValue().ToDouble( &spec.outer_diameter_in );
    m_bendRadius->GetValue().ToDouble( &spec.bend_radius_in );
    spec.insulation_type  = m_insulationType->GetValue();
    spec.jacket_type      = m_jacketType->GetValue();

    spec.primary_qty         = m_primaryQty->GetValue();
    spec.primary_conductors  = m_primaryConductors->GetValue();
    m_primarySize->GetValue().ToDouble( &spec.primary_size_value );
    spec.primary_size_unit   = static_cast<PROJECT_FILE::CABLE_SIZE_UNIT>(
                                   m_primaryUnit->GetSelection() );

    spec.secondary_qty        = m_secondaryQty->GetValue();
    spec.secondary_conductors = m_secondaryConductors->GetValue();
    m_secondarySize->GetValue().ToDouble( &spec.secondary_size_value );
    spec.secondary_size_unit  = static_cast<PROJECT_FILE::CABLE_SIZE_UNIT>(
                                    m_secondaryUnit->GetSelection() );
}


void DIALOG_CABLE_SPECS::populateForm( const PROJECT_FILE::CABLE_SPEC& spec )
{
    m_supplier->SetValue( spec.supplier );
    m_partNumber->SetValue( spec.part_number );
    m_outerDiameter->SetValue( wxString::Format( wxT( "%.3f" ), spec.outer_diameter_in ) );
    m_bendRadius->SetValue( wxString::Format( wxT( "%.2f" ), spec.bend_radius_in ) );
    m_insulationType->SetValue( spec.insulation_type );
    m_jacketType->SetValue( spec.jacket_type );

    m_primaryQty->SetValue( spec.primary_qty );
    m_primaryConductors->SetValue( spec.primary_conductors );
    m_primarySize->SetValue( wxString::Format( wxT( "%g" ), spec.primary_size_value ) );
    m_primaryUnit->SetSelection( static_cast<int>( spec.primary_size_unit ) );

    m_secondaryQty->SetValue( spec.secondary_qty );
    m_secondaryConductors->SetValue( spec.secondary_conductors );
    m_secondarySize->SetValue( wxString::Format( wxT( "%g" ), spec.secondary_size_value ) );
    m_secondaryUnit->SetSelection( static_cast<int>( spec.secondary_size_unit ) );
}


void DIALOG_CABLE_SPECS::loadFormFromKey( ROW_KIND aKind, const wxString& aName )
{
    wxString key;
    if( aKind == ROW_KIND::CLASS )
        key = aName;
    else
        key = netSpecKey( aName, m_currentFromRef );

    auto& source = ( aKind == ROW_KIND::CLASS ) ? m_classSpecs : m_netSpecs;
    auto  it = source.find( key );

    PROJECT_FILE::CABLE_SPEC spec = ( it != source.end() ) ? it->second
                                                           : PROJECT_FILE::CABLE_SPEC{};
    populateForm( spec );
}


bool DIALOG_CABLE_SPECS::currentSpecIsEmpty() const
{
    return false;       // unused, retained for future use
}


void DIALOG_CABLE_SPECS::onRowSelected( wxListEvent& aEvent )
{
    long rowIdx = aEvent.GetData();
    if( rowIdx < 0 || rowIdx >= static_cast<long>( m_rows.size() ) )
        return;   // separator

    // Save current form before switching
    commitFormToCurrent();

    m_editingTypeIdx = -1;          // back to per-row (class/net) editing

    const Row& r = m_rows[ rowIdx ];
    m_currentKind    = r.kind;
    m_currentName    = r.name;
    m_currentFromRef = r.fromRef;
    loadFormFromKey( m_currentKind, m_currentName );

    // Refresh the Cable Types panel so newly-entered values appear there immediately.
    updateCableTypesPanel();
}


void DIALOG_CABLE_SPECS::onSaveClicked( wxCommandEvent& )
{
    // Editing a Cable Type: apply the edited spec to EVERY class/net that uses it.
    if( m_editingTypeIdx >= 0 && m_editingTypeIdx < (int) m_typeRows.size() )
    {
        PROJECT_FILE::CABLE_SPEC edited;
        readForm( edited );

        TypeRow& tr = m_typeRows[ m_editingTypeIdx ];
        for( const wxString& ck : tr.classKeys )
            m_classSpecs[ ck ] = edited;
        for( const wxString& nk : tr.netKeys )
            m_netSpecs[ nk ] = edited;

        TransferDataFromWindow();   // persist to project
        buildRows();
        renderList();
        updateCableTypesPanel();    // rebuilds m_typeRows (fingerprint changed)

        // Re-select the edited type by matching its new fingerprint.
        wxString fp = makeFingerprint( edited );
        for( int i = 0; i < (int) m_typeRows.size(); ++i )
        {
            if( makeFingerprint( m_typeRows[i].spec ) == fp )
            {
                m_editingTypeIdx = i;
                m_typesCtrl->SetItemState( i, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED );
                break;
            }
        }
        return;
    }

    // Save the current form, push working state to project, persist, and refresh the
    // list so the just-edited item moves to the bottom of its group.
    commitFormToCurrent();
    TransferDataFromWindow();

    // Remember the currently selected row to restore selection after rebuilding.
    wxString savedName    = m_currentName;
    wxString savedFromRef = m_currentFromRef;
    ROW_KIND savedKind    = m_currentKind;

    buildRows();
    renderList();

    // Try to restore selection
    for( size_t i = 0; i < m_rows.size(); ++i )
    {
        if( m_rows[i].kind == savedKind && m_rows[i].name == savedName
            && m_rows[i].fromRef == savedFromRef )
        {
            for( long li = 0; li < m_listCtrl->GetItemCount(); ++li )
            {
                if( static_cast<long>( m_listCtrl->GetItemData( li ) ) == static_cast<long>( i ) )
                {
                    m_listCtrl->SetItemState( li, wxLIST_STATE_SELECTED,
                                              wxLIST_STATE_SELECTED );
                    m_listCtrl->EnsureVisible( li );
                    break;
                }
            }
            break;
        }
    }

    updateCableTypesPanel();
}


void DIALOG_CABLE_SPECS::onDeleteClicked( wxCommandEvent& )
{
    if( m_currentName.IsEmpty() )
        return;

    wxString key;
    if( m_currentKind == ROW_KIND::CLASS )
        key = m_currentName;
    else
        key = netSpecKey( m_currentName, m_currentFromRef );

    auto& target = ( m_currentKind == ROW_KIND::CLASS ) ? m_classSpecs : m_netSpecs;
    auto it = target.find( key );
    if( it == target.end() )
        return;       // nothing assigned, nothing to delete

    int answer = wxMessageBox(
            wxString::Format( _( "Remove the cable spec for '%s'?" ),
                              m_currentKind == ROW_KIND::CLASS
                              ? m_currentName
                              : ( m_currentName + wxT( " (from " ) + m_currentFromRef
                                  + wxT( ")" ) ) ),
            _( "Delete Cable Spec" ),
            wxYES_NO | wxICON_QUESTION, this );

    if( answer != wxYES )
        return;

    target.erase( it );

    // Push to project + persist immediately
    TransferDataFromWindow();

    // Refresh the row list (the item should now appear in the no-spec section)
    wxString savedName    = m_currentName;
    wxString savedFromRef = m_currentFromRef;
    ROW_KIND savedKind    = m_currentKind;

    buildRows();
    renderList();

    // Restore selection + reload an empty form for the same row
    for( size_t i = 0; i < m_rows.size(); ++i )
    {
        if( m_rows[i].kind == savedKind && m_rows[i].name == savedName
            && m_rows[i].fromRef == savedFromRef )
        {
            for( long li = 0; li < m_listCtrl->GetItemCount(); ++li )
            {
                if( static_cast<long>( m_listCtrl->GetItemData( li ) )
                    == static_cast<long>( i ) )
                {
                    m_listCtrl->SetItemState( li, wxLIST_STATE_SELECTED,
                                              wxLIST_STATE_SELECTED );
                    m_listCtrl->EnsureVisible( li );
                    break;
                }
            }
            break;
        }
    }

    loadFormFromKey( m_currentKind, m_currentName );
    updateCableTypesPanel();
}


void DIALOG_CABLE_SPECS::onPickFromLibrary( wxCommandEvent& )
{
    wxMessageBox( _( "Cable library not yet implemented.\n\n"
                     "For now, enter values manually." ),
                  _( "Cable Library" ), wxICON_INFORMATION, this );
}


// --- Cable Types panel ---------------------------------------------------

static wxString sizeStr( double aValue, PROJECT_FILE::CABLE_SIZE_UNIT aUnit )
{
    if( aValue == 0.0 )
        return wxEmptyString;
    const wxChar* u = ( aUnit == PROJECT_FILE::CABLE_SIZE_UNIT::AWG ) ? wxT( "AWG" )
                                                                      : wxT( "KCMIL" );
    return wxString::Format( wxT( "%g %s" ), aValue, u );
}


static wxString makeDisplayName( const PROJECT_FILE::CABLE_SPEC& s )
{
    // Library-style name (placeholder logic): prefer Supplier + Part Number
    if( !s.supplier.IsEmpty() && !s.part_number.IsEmpty() )
        return s.supplier + wxT( " " ) + s.part_number;

    // Manual style: "Supplier  Insulation  Size"
    wxArrayString parts;
    if( !s.supplier.IsEmpty() )       parts.Add( s.supplier );
    if( !s.insulation_type.IsEmpty() ) parts.Add( s.insulation_type );
    wxString sz = sizeStr( s.primary_size_value, s.primary_size_unit );
    if( !sz.IsEmpty() )               parts.Add( sz );

    if( parts.IsEmpty() )
        return _( "(unnamed cable)" );

    wxString out;
    for( size_t i = 0; i < parts.GetCount(); ++i )
    {
        if( i > 0 ) out += wxT( "  " );
        out += parts[i];
    }
    return out;
}


static wxString makeFingerprint( const PROJECT_FILE::CABLE_SPEC& s )
{
    // Identifies a "type". Same fingerprint → same row in the panel.
    return wxString::Format(
            wxT( "%s|%s|%s|%g|%s|%g|%d|%d|%d|%g|%d" ),
            s.supplier, s.part_number, s.insulation_type,
            s.outer_diameter_in, s.jacket_type,
            s.bend_radius_in,
            s.primary_qty, s.primary_conductors,
            static_cast<int>( s.primary_size_unit ), s.primary_size_value,
            static_cast<int>( s.secondary_qty ) );
}


void DIALOG_CABLE_SPECS::updateCableTypesPanel()
{
    if( !m_typesCtrl )
        return;

    auto specIsEmpty = []( const PROJECT_FILE::CABLE_SPEC& s )
    {
        return s.supplier.IsEmpty() && s.part_number.IsEmpty()
            && s.insulation_type.IsEmpty() && s.primary_size_value == 0.0;
    };

    // Group all specs (classes + nets) by fingerprint into m_typeRows (insertion
    // order), tracking the assignment keys so a type edit can update every use.
    m_typeRows.clear();
    std::map<wxString, int>            fpToIdx;
    std::vector<std::vector<wxString>> labels;   // parallel to m_typeRows

    auto addAssignment = [&]( const PROJECT_FILE::CABLE_SPEC& spec, const wxString& label,
                              bool isClass, const wxString& key )
    {
        if( specIsEmpty( spec ) )
            return;

        wxString fp = makeFingerprint( spec );
        auto     it = fpToIdx.find( fp );
        int      idx;
        if( it == fpToIdx.end() )
        {
            idx = (int) m_typeRows.size();
            fpToIdx[ fp ] = idx;
            m_typeRows.push_back( TypeRow{ spec, {}, {} } );
            labels.emplace_back();
        }
        else
        {
            idx = it->second;
        }

        if( isClass )
            m_typeRows[ idx ].classKeys.push_back( key );
        else
            m_typeRows[ idx ].netKeys.push_back( key );

        labels[ idx ].push_back( label );
    };

    for( const auto& [name, spec] : m_classSpecs )
        addAssignment( spec, wxT( "[Class] " ) + name, true, name );

    for( const auto& [key, spec] : m_netSpecs )
    {
        // Decode "netName||fromRef" → "netName (from FROM)" for display.
        int sep = key.Find( wxT( "||" ) );
        if( sep != wxNOT_FOUND )
        {
            wxString net  = key.Left( sep );
            wxString from = key.Mid( sep + 2 );
            addAssignment( spec, net + wxT( " (from " ) + from + wxT( ")" ), false, key );
        }
        else
        {
            addAssignment( spec, key, false, key );
        }
    }

    m_typesCtrl->DeleteAllItems();

    for( int idx = 0; idx < (int) m_typeRows.size(); ++idx )
    {
        wxString usedJoined;
        for( size_t i = 0; i < labels[ idx ].size(); ++i )
        {
            if( i > 0 ) usedJoined += wxT( ", " );
            usedJoined += labels[ idx ][ i ];
        }

        m_typesCtrl->InsertItem( idx, makeDisplayName( m_typeRows[ idx ].spec ) );
        m_typesCtrl->SetItem( idx, 1, _( "Manual" ) );    // library support: future
        m_typesCtrl->SetItem( idx, 2, usedJoined );
        m_typesCtrl->SetItemData( idx, idx );             // → m_typeRows index
    }

    if( m_typeRows.empty() )
    {
        m_typesCtrl->InsertItem( 0, _( "(no cable types defined yet)" ) );
        m_typesCtrl->SetItemData( 0, -1 );
        m_typesCtrl->SetItemTextColour( 0, wxColour( 130, 130, 130 ) );
    }
}


void DIALOG_CABLE_SPECS::onTypeSelected( wxListEvent& aEvent )
{
    long idx = aEvent.GetData();
    if( idx < 0 || idx >= (long) m_typeRows.size() )
        return;

    // Switch to "edit this cable type" mode: load its spec, and on Save apply the
    // edits to every class/net that uses this type.
    commitFormToCurrent();          // no-op in type mode (m_currentName empty)
    m_currentName.Clear();
    m_currentFromRef.Clear();
    m_editingTypeIdx = (int) idx;
    populateForm( m_typeRows[ idx ].spec );
}
