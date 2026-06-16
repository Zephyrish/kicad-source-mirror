/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Site Layout — site origin dialog.
 */

#include "dialog_site_origin.h"

#include <pcb_edit_frame.h>
#include <project/project_file.h>
#include <project.h>
#include <settings/settings_manager.h>

#include <wx/button.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>


DIALOG_SITE_ORIGIN::DIALOG_SITE_ORIGIN( PCB_EDIT_FRAME* aParent ) :
        wxDialog( aParent, wxID_ANY, _( "Site Origin" ),
                  wxDefaultPosition, wxSize( 400, 260 ),
                  wxDEFAULT_DIALOG_STYLE ),
        m_frame( aParent ),
        m_latCtrl( nullptr ),
        m_lonCtrl( nullptr ),
        m_rotCtrl( nullptr )
{
    wxBoxSizer* outer = new wxBoxSizer( wxVERTICAL );

    outer->Add( new wxStaticText( this, wxID_ANY,
            _( "Set the geographic origin for this site layout.\n"
               "The board's local (0, 0) corresponds to this point.\n"
               "Rotation is the clockwise bearing of the local +Y axis from True North.\n\n"
               "Set all values to 0 (or click Clear) to disable global coordinate display." ) ),
            0, wxALL, 12 );

    wxFlexGridSizer* grid = new wxFlexGridSizer( 3, 2, 8, 8 );
    grid->AddGrowableCol( 1, 1 );

    grid->Add( new wxStaticText( this, wxID_ANY, _( "Latitude (deg):" ) ),
               0, wxALIGN_CENTER_VERTICAL );
    m_latCtrl = new wxTextCtrl( this, wxID_ANY );
    grid->Add( m_latCtrl, 1, wxEXPAND );

    grid->Add( new wxStaticText( this, wxID_ANY, _( "Longitude (deg):" ) ),
               0, wxALIGN_CENTER_VERTICAL );
    m_lonCtrl = new wxTextCtrl( this, wxID_ANY );
    grid->Add( m_lonCtrl, 1, wxEXPAND );

    grid->Add( new wxStaticText( this, wxID_ANY, _( "Rotation (deg CW from N):" ) ),
               0, wxALIGN_CENTER_VERTICAL );
    m_rotCtrl = new wxTextCtrl( this, wxID_ANY );
    grid->Add( m_rotCtrl, 1, wxEXPAND );

    outer->Add( grid, 1, wxLEFT | wxRIGHT | wxEXPAND, 12 );

    // Button row: Clear on the left, OK/Cancel on the right
    wxBoxSizer* buttonRow = new wxBoxSizer( wxHORIZONTAL );
    wxButton* clearBtn = new wxButton( this, wxID_ANY, _( "Clear" ) );
    buttonRow->Add( clearBtn, 0, wxALIGN_CENTER_VERTICAL );
    buttonRow->AddStretchSpacer();
    buttonRow->Add( CreateButtonSizer( wxOK | wxCANCEL ), 0 );
    outer->Add( buttonRow, 0, wxEXPAND | wxALL, 12 );

    clearBtn->Bind( wxEVT_BUTTON,
            [this]( wxCommandEvent& )
            {
                m_latCtrl->SetValue( wxT( "0" ) );
                m_lonCtrl->SetValue( wxT( "0" ) );
                m_rotCtrl->SetValue( wxT( "0" ) );
            } );

    SetSizer( outer );
    Layout();

    TransferDataToWindow();
}


bool DIALOG_SITE_ORIGIN::TransferDataToWindow()
{
    PROJECT_FILE& proj = m_frame->Prj().GetProjectFile();
    m_latCtrl->SetValue( wxString::Format( wxT( "%.7f" ), proj.m_SiteOriginLat ) );
    m_lonCtrl->SetValue( wxString::Format( wxT( "%.7f" ), proj.m_SiteOriginLon ) );
    m_rotCtrl->SetValue( wxString::Format( wxT( "%.3f" ), proj.m_SiteOriginRotationDeg ) );
    return true;
}


bool DIALOG_SITE_ORIGIN::TransferDataFromWindow()
{
    PROJECT_FILE& proj = m_frame->Prj().GetProjectFile();

    double lat = proj.m_SiteOriginLat;
    double lon = proj.m_SiteOriginLon;
    double rot = proj.m_SiteOriginRotationDeg;

    m_latCtrl->GetValue().ToDouble( &lat );
    m_lonCtrl->GetValue().ToDouble( &lon );
    m_rotCtrl->GetValue().ToDouble( &rot );

    proj.m_SiteOriginLat = lat;
    proj.m_SiteOriginLon = lon;
    proj.m_SiteOriginRotationDeg = rot;

    // Persist via the settings manager (correct way — handles path resolution).
    if( SETTINGS_MANAGER* mgr = m_frame->GetSettingsManager() )
        mgr->SaveProject();

    return true;
}
