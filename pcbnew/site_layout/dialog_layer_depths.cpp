/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Site Layout — layer-depths dialog implementation.
 */

#include "dialog_layer_depths.h"

#include <board.h>
#include <board_design_settings.h>
#include <pcb_edit_frame.h>
#include <project.h>
#include <project/project_file.h>
#include <settings/settings_manager.h>
#include <widgets/appearance_controls.h>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/msgdlg.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>


enum { ID_ADD_LAYER = wxID_HIGHEST + 1 };


DIALOG_LAYER_DEPTHS::DIALOG_LAYER_DEPTHS( PCB_EDIT_FRAME* aParent ) :
        wxDialog( aParent, wxID_ANY, _( "Conduit Depths" ),
                  wxDefaultPosition, wxSize( 520, 480 ),
                  wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER ),
        m_frame( aParent ),
        m_scroll( nullptr ),
        m_grid( nullptr )
{
    wxBoxSizer* outer = new wxBoxSizer( wxVERTICAL );

    outer->Add( new wxStaticText( this, wxID_ANY,
            _( "Assign a display name and burial depth (inches) to each layer.\n"
               "Each layer represents a conduit depth stratum and is a real board copper\n"
               "layer (shown in the Layers panel). Depth 0 = surface. Names are saved to\n"
               "the board; depths to the project." ) ),
            0, wxALL, 12 );

    // Scrollable area for the rows
    m_scroll = new wxScrolledWindow( this, wxID_ANY,
            wxDefaultPosition, wxDefaultSize, wxVSCROLL );
    m_scroll->SetScrollRate( 0, 20 );

    m_grid = new wxFlexGridSizer( 4, 8, 8 );   // Del | Layer | Display name | Depth
    m_grid->AddGrowableCol( 2, 1 );
    m_grid->AddGrowableCol( 3, 0 );
    m_scroll->SetSizer( m_grid );

    outer->Add( m_scroll, 1, wxEXPAND | wxLEFT | wxRIGHT, 12 );

    // Add layers (in pairs); delete the layers checked in the "Del" column.
    wxBoxSizer* btnRow = new wxBoxSizer( wxHORIZONTAL );
    wxButton*   addBtn = new wxButton( this, ID_ADD_LAYER, _( "Add Layer" ) );
    wxButton*   delBtn = new wxButton( this, wxID_ANY, _( "Delete Selected" ) );
    btnRow->Add( addBtn, 0, wxRIGHT, 8 );
    btnRow->Add( delBtn, 0 );
    outer->Add( btnRow, 0, wxLEFT | wxRIGHT | wxTOP, 12 );

    outer->Add( CreateButtonSizer( wxOK | wxCANCEL ), 0, wxEXPAND | wxALL, 12 );

    SetSizer( outer );

    addBtn->Bind( wxEVT_BUTTON, &DIALOG_LAYER_DEPTHS::onAddLayer, this );
    delBtn->Bind( wxEVT_BUTTON, &DIALOG_LAYER_DEPTHS::onDeleteSelected, this );

    rebuildRows();
    Layout();
}


void DIALOG_LAYER_DEPTHS::rebuildRows()
{
    m_grid->Clear( true );    // delete existing header + row controls
    m_rows.clear();

    wxFont headerFont = m_scroll->GetFont();
    headerFont.MakeBold();
    auto hdr = [&]( const wxString& text )
    {
        wxStaticText* t = new wxStaticText( m_scroll, wxID_ANY, text );
        t->SetFont( headerFont );
        return t;
    };
    m_grid->Add( hdr( _( "Del" ) ),            0, wxALIGN_CENTER_VERTICAL );
    m_grid->Add( hdr( _( "Layer" ) ),          0, wxALIGN_CENTER_VERTICAL );
    m_grid->Add( hdr( _( "Display name" ) ),   0, wxALIGN_CENTER_VERTICAL );
    m_grid->Add( hdr( _( "Depth (inches)" ) ), 0, wxALIGN_CENTER_VERTICAL );

    BOARD*     board   = m_frame->GetBoard();
    const LSET enabled = board->GetEnabledLayers();

    for( PCB_LAYER_ID layer : enabled.CuStack() )
    {
        Row row;
        row.layer = layer;

        // Only inner copper layers can be deleted; F.Cu / B.Cu are mandatory.
        if( layer != F_Cu && layer != B_Cu )
        {
            row.delChk = new wxCheckBox( m_scroll, wxID_ANY, wxEmptyString );
            m_grid->Add( row.delChk, 0, wxALIGN_CENTER_VERTICAL );
        }
        else
        {
            m_grid->Add( new wxStaticText( m_scroll, wxID_ANY, wxEmptyString ),
                         0, wxALIGN_CENTER_VERTICAL );
        }

        row.layerLabel = new wxStaticText( m_scroll, wxID_ANY,
                                           BOARD::GetStandardLayerName( layer ) );
        m_grid->Add( row.layerLabel, 0, wxALIGN_CENTER_VERTICAL );

        row.nameCtrl  = new wxTextCtrl( m_scroll, wxID_ANY );
        row.depthCtrl = new wxTextCtrl( m_scroll, wxID_ANY );
        m_grid->Add( row.nameCtrl,  1, wxEXPAND );
        m_grid->Add( row.depthCtrl, 0, wxALIGN_CENTER_VERTICAL );

        // Keep the left "Layer" label in sync with the typed display name.
        wxStaticText* lbl = row.layerLabel;
        wxTextCtrl*   nc  = row.nameCtrl;
        PCB_LAYER_ID  ly  = layer;
        nc->Bind( wxEVT_TEXT,
                  [lbl, nc, ly]( wxCommandEvent& evt )
                  {
                      wxString v = nc->GetValue();
                      lbl->SetLabel( v.IsEmpty() ? BOARD::GetStandardLayerName( ly ) : v );
                      evt.Skip();
                  } );

        m_rows.push_back( row );
    }

    m_scroll->FitInside();
    m_scroll->Layout();

    TransferDataToWindow();
}


void DIALOG_LAYER_DEPTHS::onAddLayer( wxCommandEvent& aEvent )
{
    BOARD* board = m_frame->GetBoard();

    int count = board->GetCopperLayerCount();
    if( count >= MAX_CU_LAYERS )
    {
        wxMessageBox( _( "Maximum number of copper layers reached." ),
                      _( "Add Layer" ), wxICON_INFORMATION, this );
        return;
    }

    // KiCad copper layer counts must be even (>= 2), so layers are added in pairs.
    commitRows();
    board->SetCopperLayerCount( count + 2 );

    rebuildRows();
    Layout();

    // Reflect the new layer in the right-hand Layers (Appearance) panel.
    if( APPEARANCE_CONTROLS* panel = m_frame->GetAppearancePanel() )
        panel->OnBoardChanged();

    m_frame->OnModify();
}


void DIALOG_LAYER_DEPTHS::onDeleteSelected( wxCommandEvent& aEvent )
{
    BOARD* board = m_frame->GetBoard();

    // Collect the checked (inner) layers.
    std::vector<PCB_LAYER_ID> toDelete;
    for( const Row& row : m_rows )
    {
        if( row.delChk && row.delChk->IsChecked() )
            toDelete.push_back( row.layer );
    }

    if( toDelete.empty() )
    {
        wxMessageBox( _( "Check the layers to delete (in the 'Del' column) first." ),
                      _( "Delete Selected" ), wxICON_INFORMATION, this );
        return;
    }

    // KiCad requires an even copper-layer count, so an even number must be removed.
    if( ( toDelete.size() % 2 ) != 0 )
    {
        wxMessageBox(
                _( "KiCad requires an even number of copper layers, so layers must be "
                   "deleted in even quantities. Please check an even number of layers." ),
                _( "Delete Selected" ), wxICON_WARNING, this );
        return;
    }

    if( wxMessageBox(
                wxString::Format(
                    _( "Delete %zu copper layer(s)?\n\n"
                       "Any conduit routed on a deleted layer will be left without a "
                       "valid layer." ),
                    toDelete.size() ),
                _( "Delete Selected" ), wxYES_NO | wxICON_WARNING, this ) != wxYES )
    {
        return;
    }

    commitRows();

    // Disable the checked layers (keeps the remaining layers' IDs intact, so conduits
    // routed on them are undisturbed). Even removal from an even count stays even.
    LSET enabled = board->GetEnabledLayers();
    for( PCB_LAYER_ID l : toDelete )
        enabled.reset( l );

    board->SetEnabledLayers( enabled );

    PROJECT_FILE& proj = m_frame->Prj().GetProjectFile();
    for( PCB_LAYER_ID l : toDelete )
        proj.m_LayerDepthsInches.erase( static_cast<int>( l ) );

    rebuildRows();
    Layout();

    if( APPEARANCE_CONTROLS* panel = m_frame->GetAppearancePanel() )
        panel->OnBoardChanged();

    m_frame->OnModify();
}


void DIALOG_LAYER_DEPTHS::commitRows()
{
    BOARD*        board = m_frame->GetBoard();
    PROJECT_FILE& proj  = m_frame->Prj().GetProjectFile();

    for( const Row& row : m_rows )
    {
        wxString name = row.nameCtrl->GetValue();
        if( name == BOARD::GetStandardLayerName( row.layer ) )
            board->SetLayerName( row.layer, wxEmptyString );
        else
            board->SetLayerName( row.layer, name );

        double depth = 0.0;
        row.depthCtrl->GetValue().ToDouble( &depth );

        if( depth == 0.0 )
            proj.m_LayerDepthsInches.erase( static_cast<int>( row.layer ) );
        else
            proj.m_LayerDepthsInches[ static_cast<int>( row.layer ) ] = depth;
    }
}


bool DIALOG_LAYER_DEPTHS::TransferDataToWindow()
{
    BOARD*        board = m_frame->GetBoard();
    PROJECT_FILE& proj  = m_frame->Prj().GetProjectFile();

    for( Row& row : m_rows )
    {
        row.nameCtrl->SetValue( board->GetLayerName( row.layer ) );
        row.layerLabel->SetLabel( board->GetLayerName( row.layer ) );

        auto it = proj.m_LayerDepthsInches.find( static_cast<int>( row.layer ) );
        double depth = ( it != proj.m_LayerDepthsInches.end() ) ? it->second : 0.0;
        row.depthCtrl->SetValue( wxString::Format( wxT( "%.2f" ), depth ) );
    }
    return true;
}


bool DIALOG_LAYER_DEPTHS::TransferDataFromWindow()
{
    commitRows();

    if( SETTINGS_MANAGER* mgr = m_frame->GetSettingsManager() )
        mgr->SaveProject();

    // Force the appearance controls (right sidebar) to pick up the new names.
    if( APPEARANCE_CONTROLS* panel = m_frame->GetAppearancePanel() )
        panel->OnBoardChanged();

    m_frame->OnModify();
    return true;
}
