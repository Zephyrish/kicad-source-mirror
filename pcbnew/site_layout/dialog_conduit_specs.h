/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Site Layout — Conduit Specs dialog.
 *
 * Master-detail editor for project-level conduit definitions. Each entry is a
 * named conduit ("2-inch EMT") with the physical/routing rules: material,
 * inner diameter, clearance, bend radius, max bend angle.
 */

#ifndef DIALOG_CONDUIT_SPECS_H
#define DIALOG_CONDUIT_SPECS_H

#include <map>
#include <wx/dialog.h>
#include <wx/string.h>

#include <project/project_file.h>

class PCB_EDIT_FRAME;
class wxButton;
class wxListBox;
class wxTextCtrl;


class DIALOG_CONDUIT_SPECS : public wxDialog
{
public:
    DIALOG_CONDUIT_SPECS( PCB_EDIT_FRAME* aParent );

private:
    bool TransferDataToWindow() override;
    bool TransferDataFromWindow() override;

    void commitFormToCurrent();
    void loadFormFromSpec( const wxString& aName );
    void refreshListBox();

    void onSelectSpec( wxCommandEvent& aEvent );
    void onAddSpec( wxCommandEvent& aEvent );
    void onDeleteSpec( wxCommandEvent& aEvent );
    void onSave( wxCommandEvent& aEvent );
    void onPickFromLibrary( wxCommandEvent& aEvent );

    PCB_EDIT_FRAME* m_frame;

    // Working copy — committed on OK/Save
    std::map<wxString, PROJECT_FILE::CONDUIT_SPEC> m_specs;
    wxString m_currentName;

    // UI
    wxListBox*  m_specList;

    wxButton*   m_pickLibraryBtn;
    wxTextCtrl* m_supplier;
    wxTextCtrl* m_partNumber;
    wxTextCtrl* m_material;
    wxTextCtrl* m_innerDiameter;
    wxTextCtrl* m_clearance;
    wxTextCtrl* m_maxBendAngle;
    wxTextCtrl* m_bendRadius;
};

#endif // DIALOG_CONDUIT_SPECS_H
