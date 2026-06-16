/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Site Layout — dialog to edit per-copper-layer names and depths.
 */

#ifndef DIALOG_LAYER_DEPTHS_H
#define DIALOG_LAYER_DEPTHS_H

#include <vector>
#include <wx/dialog.h>

#include <layer_ids.h>

class PCB_EDIT_FRAME;
class wxTextCtrl;
class wxStaticText;
class wxCheckBox;
class wxScrolledWindow;
class wxFlexGridSizer;
class wxCommandEvent;


class DIALOG_LAYER_DEPTHS : public wxDialog
{
public:
    DIALOG_LAYER_DEPTHS( PCB_EDIT_FRAME* aParent );

private:
    bool TransferDataToWindow() override;
    bool TransferDataFromWindow() override;

    /// Rebuild the per-layer rows from the board's currently-enabled copper layers.
    void rebuildRows();

    /// Write the current row values (names + depths) back to the board/project,
    /// without saving the project file. Used before a structural rebuild so typed
    /// values aren't lost.
    void commitRows();

    /// "Add Layer" handler: enable one more copper layer on the board, then rebuild.
    void onAddLayer( wxCommandEvent& aEvent );

    /// "Delete Selected" handler: remove the checked (inner) layers. Requires an even
    /// number checked (KiCad needs an even copper count); confirmation; remaps nothing
    /// (kept layers keep their IDs so routed conduits aren't disturbed).
    void onDeleteSelected( wxCommandEvent& aEvent );

    PCB_EDIT_FRAME*   m_frame;
    wxScrolledWindow* m_scroll;
    wxFlexGridSizer*  m_grid;

    struct Row
    {
        PCB_LAYER_ID  layer;
        wxCheckBox*   delChk = nullptr;   // null for F.Cu/B.Cu (can't be deleted)
        wxStaticText* layerLabel;         // left "Layer" column; mirrors display name
        wxTextCtrl*   nameCtrl;
        wxTextCtrl*   depthCtrl;
    };
    std::vector<Row> m_rows;
};

#endif // DIALOG_LAYER_DEPTHS_H
