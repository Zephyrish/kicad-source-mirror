/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Site Layout — cable spec dialog with classes + nets.
 */

#ifndef DIALOG_CABLE_SPECS_H
#define DIALOG_CABLE_SPECS_H

#include <map>
#include <wx/dialog.h>
#include <wx/string.h>

#include <project/project_file.h>

class SCH_EDIT_FRAME;
class wxButton;
class wxChoice;
class wxListCtrl;
class wxListEvent;
class wxSpinCtrl;
class wxTextCtrl;


class DIALOG_CABLE_SPECS : public wxDialog
{
public:
    /// Open the dialog. aInitialName pre-selects a class or net name if found.
    DIALOG_CABLE_SPECS( SCH_EDIT_FRAME* aParent,
                        const wxString& aInitialName = wxEmptyString );

private:
    enum class ROW_KIND { CLASS, NET };

    struct Row
    {
        ROW_KIND kind;
        wxString name;       // class name or net name
        wxString fromRef;    // empty for classes
        wxString toRef;
        int      netCode;    // -1 for classes
        wxString netClass;   // class assignment for nets (display); empty for class rows
        bool     hasSpec;
    };

    bool TransferDataToWindow() override;
    bool TransferDataFromWindow() override;

    void buildRows();
    void renderList();

    void commitFormToCurrent();
    void loadFormFromKey( ROW_KIND aKind, const wxString& aName );

    void readForm( PROJECT_FILE::CABLE_SPEC& aSpec ) const;   // form → spec
    void populateForm( const PROJECT_FILE::CABLE_SPEC& aSpec ); // spec → form

    void updateCableTypesPanel();

    bool currentSpecIsEmpty() const;

    void onRowSelected( wxListEvent& aEvent );
    void onTypeSelected( wxListEvent& aEvent );
    void onPickFromLibrary( wxCommandEvent& aEvent );

    SCH_EDIT_FRAME* m_frame;
    wxString        m_initialName;

    // Working copies — committed to the project on OK.
    std::map<wxString, PROJECT_FILE::CABLE_SPEC> m_classSpecs;
    std::map<wxString, PROJECT_FILE::CABLE_SPEC> m_netSpecs;

    std::vector<Row> m_rows;
    ROW_KIND m_currentKind = ROW_KIND::CLASS;
    wxString m_currentName;       // class name OR net name
    wxString m_currentFromRef;    // perspective component (for NET rows)

    // Cable Types panel editing: each displayed type maps to the assignments using it.
    struct TypeRow
    {
        PROJECT_FILE::CABLE_SPEC spec;
        std::vector<wxString>    classKeys;   // class names
        std::vector<wxString>    netKeys;     // net keys (netName||fromRef)
    };
    std::vector<TypeRow> m_typeRows;          // parallel to m_typesCtrl data rows
    int                  m_editingTypeIdx = -1;   // >=0 when editing a Cable Type

    void onSaveClicked( wxCommandEvent& aEvent );
    void onDeleteClicked( wxCommandEvent& aEvent );

    // UI
    wxListCtrl* m_listCtrl;

    wxTextCtrl* m_supplier;
    wxTextCtrl* m_partNumber;
    wxTextCtrl* m_outerDiameter;
    wxTextCtrl* m_bendRadius;
    wxTextCtrl* m_insulationType;
    wxTextCtrl* m_jacketType;

    wxSpinCtrl* m_primaryQty;
    wxSpinCtrl* m_primaryConductors;
    wxTextCtrl* m_primarySize;
    wxChoice*   m_primaryUnit;

    wxSpinCtrl* m_secondaryQty;
    wxSpinCtrl* m_secondaryConductors;
    wxTextCtrl* m_secondarySize;
    wxChoice*   m_secondaryUnit;

    wxButton*   m_pickLibraryBtn;

    wxListCtrl* m_typesCtrl;     // bottom panel: distinct cable types
};

#endif // DIALOG_CABLE_SPECS_H
