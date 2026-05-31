/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Site Layout — Conduit Specs dialog implementation.
 */

#include "dialog_conduit_specs.h"

#include <pcb_edit_frame.h>
#include <project.h>
#include <project/project_file.h>
#include <settings/settings_manager.h>

#include <wx/button.h>
#include <wx/listbox.h>
#include <wx/msgdlg.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/textdlg.h>


DIALOG_CONDUIT_SPECS::DIALOG_CONDUIT_SPECS( PCB_EDIT_FRAME* aParent ) :
        wxDialog( aParent, wxID_ANY, _( "Conduit Specs" ),
                  wxDefaultPosition, wxSize( 720, 560 ),
                  wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER ),
        m_frame( aParent ),
        m_specList( nullptr ),
        m_pickLibraryBtn( nullptr ),
        m_supplier( nullptr ),
        m_partNumber( nullptr ),
        m_material( nullptr ),
        m_innerDiameter( nullptr ),
        m_clearance( nullptr ),
        m_maxBendAngle( nullptr ),
        m_bendRadius( nullptr )
{
    wxBoxSizer* outer = new wxBoxSizer( wxVERTICAL );

    outer->Add( new wxStaticText( this, wxID_ANY,
            _( "Project-level conduit definitions used for routing rules and DRC.\n"
               "Define each conduit you stock for this project; assign one to each conduit\n"
               "in the Conduit Schematic." ) ),
            0, wxALL, 12 );

    // Master-detail
    wxBoxSizer* split = new wxBoxSizer( wxHORIZONTAL );

    // ---- Left: spec list + Add/Delete ----
    wxBoxSizer* leftCol = new wxBoxSizer( wxVERTICAL );

    m_specList = new wxListBox( this, wxID_ANY, wxDefaultPosition, wxSize( 180, -1 ) );
    leftCol->Add( m_specList, 1, wxEXPAND | wxBOTTOM, 6 );

    wxBoxSizer* addDelRow = new wxBoxSizer( wxHORIZONTAL );
    wxButton* addBtn    = new wxButton( this, wxID_ANY, _( "Add..." ) );
    wxButton* deleteBtn = new wxButton( this, wxID_ANY, _( "Delete" ) );
    addDelRow->Add( addBtn,    1, wxEXPAND | wxRIGHT, 3 );
    addDelRow->Add( deleteBtn, 1, wxEXPAND );
    leftCol->Add( addDelRow, 0, wxEXPAND );

    split->Add( leftCol, 0, wxEXPAND | wxRIGHT, 12 );

    // ---- Right: form ----
    wxScrolledWindow* formScroll = new wxScrolledWindow( this, wxID_ANY,
            wxDefaultPosition, wxDefaultSize, wxVSCROLL );
    formScroll->SetScrollRate( 0, 20 );

    wxStaticBoxSizer* form = new wxStaticBoxSizer( wxVERTICAL, formScroll,
                                                   _( "Conduit Properties" ) );
    wxWindow* fp = form->GetStaticBox();

    // Library picker
    wxBoxSizer* libRow = new wxBoxSizer( wxHORIZONTAL );
    libRow->Add( new wxStaticText( fp, wxID_ANY, _( "From library:" ) ),
                 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8 );
    m_pickLibraryBtn = new wxButton( fp, wxID_ANY, _( "Pick from Library..." ) );
    m_pickLibraryBtn->Disable();
    m_pickLibraryBtn->SetToolTip( _( "Conduit library not yet implemented" ) );
    libRow->Add( m_pickLibraryBtn, 1, wxEXPAND );
    form->Add( libRow, 0, wxEXPAND | wxALL, 8 );

    auto addRow = [&]( const wxString& label, wxTextCtrl*& ctrl, wxFlexGridSizer* g )
    {
        g->Add( new wxStaticText( fp, wxID_ANY, label ), 0, wxALIGN_CENTER_VERTICAL );
        ctrl = new wxTextCtrl( fp, wxID_ANY );
        g->Add( ctrl, 1, wxEXPAND );
    };

    wxFlexGridSizer* grid = new wxFlexGridSizer( 2, 6, 8 );
    grid->AddGrowableCol( 1, 1 );

    addRow( _( "Supplier:" ),               m_supplier,      grid );
    addRow( _( "Part Number:" ),            m_partNumber,    grid );
    addRow( _( "Material:" ),               m_material,      grid );
    addRow( _( "Inner Diameter (in):" ),    m_innerDiameter, grid );
    addRow( _( "Clearance (in):" ),         m_clearance,     grid );
    addRow( _( "Max Bend Angle (deg):" ),   m_maxBendAngle,  grid );
    addRow( _( "Min Bend Radius (in):" ),   m_bendRadius,    grid );

    form->Add( grid, 0, wxEXPAND | wxALL, 8 );

    wxBoxSizer* scrollSizer = new wxBoxSizer( wxVERTICAL );
    scrollSizer->Add( form, 0, wxEXPAND | wxALL, 4 );
    formScroll->SetSizer( scrollSizer );
    formScroll->FitInside();

    split->Add( formScroll, 1, wxEXPAND );
    outer->Add( split, 1, wxEXPAND | wxLEFT | wxRIGHT, 12 );

    // Button row
    wxBoxSizer* buttonRow = new wxBoxSizer( wxHORIZONTAL );
    wxButton* saveBtn = new wxButton( this, wxID_ANY, _( "Save" ) );
    saveBtn->SetToolTip( _( "Commit current entry without closing" ) );
    buttonRow->Add( saveBtn, 0, wxALIGN_CENTER_VERTICAL );
    buttonRow->AddStretchSpacer();
    buttonRow->Add( CreateButtonSizer( wxOK | wxCANCEL ), 0 );
    outer->Add( buttonRow, 0, wxEXPAND | wxALL, 12 );

    SetSizer( outer );
    Layout();

    m_specList->Bind( wxEVT_LISTBOX,
            [this]( wxCommandEvent& e ) { onSelectSpec( e ); } );
    addBtn->Bind( wxEVT_BUTTON,
            [this]( wxCommandEvent& e ) { onAddSpec( e ); } );
    deleteBtn->Bind( wxEVT_BUTTON,
            [this]( wxCommandEvent& e ) { onDeleteSpec( e ); } );
    saveBtn->Bind( wxEVT_BUTTON,
            [this]( wxCommandEvent& e ) { onSave( e ); } );
    m_pickLibraryBtn->Bind( wxEVT_BUTTON,
            [this]( wxCommandEvent& e ) { onPickFromLibrary( e ); } );

    TransferDataToWindow();
}


// ---- List rendering / selection -----------------------------------------

void DIALOG_CONDUIT_SPECS::refreshListBox()
{
    m_specList->Clear();
    for( const auto& [name, spec] : m_specs )
        m_specList->Append( name );
}


bool DIALOG_CONDUIT_SPECS::TransferDataToWindow()
{
    m_specs = m_frame->Prj().GetProjectFile().m_ConduitSpecs;
    refreshListBox();

    if( m_specList->GetCount() > 0 )
    {
        m_specList->SetSelection( 0 );
        m_currentName = m_specList->GetString( 0 );
        loadFormFromSpec( m_currentName );
    }
    return true;
}


bool DIALOG_CONDUIT_SPECS::TransferDataFromWindow()
{
    commitFormToCurrent();

    PROJECT_FILE& proj = m_frame->Prj().GetProjectFile();
    proj.m_ConduitSpecs = m_specs;

    if( SETTINGS_MANAGER* mgr = m_frame->GetSettingsManager() )
        mgr->SaveProject();

    return true;
}


void DIALOG_CONDUIT_SPECS::commitFormToCurrent()
{
    if( m_currentName.IsEmpty() )
        return;

    PROJECT_FILE::CONDUIT_SPEC& s = m_specs[ m_currentName ];

    s.supplier    = m_supplier->GetValue();
    s.part_number = m_partNumber->GetValue();
    s.material    = m_material->GetValue();
    m_innerDiameter->GetValue().ToDouble( &s.inner_diameter_in );
    m_clearance->GetValue().ToDouble( &s.clearance_in );
    m_maxBendAngle->GetValue().ToDouble( &s.max_bend_angle_deg );
    m_bendRadius->GetValue().ToDouble( &s.bend_radius_in );
}


void DIALOG_CONDUIT_SPECS::loadFormFromSpec( const wxString& aName )
{
    auto it = m_specs.find( aName );
    PROJECT_FILE::CONDUIT_SPEC s = ( it != m_specs.end() ) ? it->second
                                                           : PROJECT_FILE::CONDUIT_SPEC{};

    m_supplier->SetValue( s.supplier );
    m_partNumber->SetValue( s.part_number );
    m_material->SetValue( s.material );
    m_innerDiameter->SetValue( wxString::Format( wxT( "%.3f" ), s.inner_diameter_in ) );
    m_clearance->SetValue( wxString::Format( wxT( "%.3f" ), s.clearance_in ) );
    m_maxBendAngle->SetValue( wxString::Format( wxT( "%.1f" ), s.max_bend_angle_deg ) );
    m_bendRadius->SetValue( wxString::Format( wxT( "%.3f" ), s.bend_radius_in ) );
}


void DIALOG_CONDUIT_SPECS::onSelectSpec( wxCommandEvent& aEvent )
{
    commitFormToCurrent();

    int sel = m_specList->GetSelection();
    if( sel == wxNOT_FOUND )
    {
        m_currentName.Clear();
        return;
    }
    m_currentName = m_specList->GetString( sel );
    loadFormFromSpec( m_currentName );
}


void DIALOG_CONDUIT_SPECS::onAddSpec( wxCommandEvent& )
{
    wxTextEntryDialog askName( this,
            _( "Name for the new conduit spec (e.g., '2-inch EMT'):" ),
            _( "Add Conduit Spec" ) );
    if( askName.ShowModal() != wxID_OK )
        return;

    wxString name = askName.GetValue().Trim();
    if( name.IsEmpty() )
        return;
    if( m_specs.find( name ) != m_specs.end() )
    {
        wxMessageBox( wxString::Format( _( "A spec named '%s' already exists." ), name ),
                      _( "Add Conduit Spec" ), wxICON_INFORMATION, this );
        return;
    }

    // Commit the current entry before adding
    commitFormToCurrent();

    m_specs[ name ] = PROJECT_FILE::CONDUIT_SPEC{};
    refreshListBox();

    int newIdx = m_specList->FindString( name );
    if( newIdx != wxNOT_FOUND )
    {
        m_specList->SetSelection( newIdx );
        m_currentName = name;
        loadFormFromSpec( name );
    }
}


void DIALOG_CONDUIT_SPECS::onDeleteSpec( wxCommandEvent& )
{
    if( m_currentName.IsEmpty() )
        return;

    int answer = wxMessageBox(
            wxString::Format( _( "Delete conduit spec '%s'?" ), m_currentName ),
            _( "Delete Conduit Spec" ),
            wxYES_NO | wxICON_QUESTION, this );
    if( answer != wxYES )
        return;

    m_specs.erase( m_currentName );
    m_currentName.Clear();
    refreshListBox();

    if( m_specList->GetCount() > 0 )
    {
        m_specList->SetSelection( 0 );
        m_currentName = m_specList->GetString( 0 );
        loadFormFromSpec( m_currentName );
    }
    else
    {
        loadFormFromSpec( wxEmptyString );
    }
}


void DIALOG_CONDUIT_SPECS::onSave( wxCommandEvent& )
{
    commitFormToCurrent();
    TransferDataFromWindow();
}


void DIALOG_CONDUIT_SPECS::onPickFromLibrary( wxCommandEvent& )
{
    wxMessageBox( _( "Conduit library not yet implemented.\n\n"
                     "For now, enter values manually." ),
                  _( "Conduit Library" ), wxICON_INFORMATION, this );
}
