/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Site Layout — dialog to edit the project's site origin (lat/lon/rotation).
 */

#ifndef DIALOG_SITE_ORIGIN_H
#define DIALOG_SITE_ORIGIN_H

#include <wx/dialog.h>

class PCB_EDIT_FRAME;
class wxTextCtrl;


class DIALOG_SITE_ORIGIN : public wxDialog
{
public:
    DIALOG_SITE_ORIGIN( PCB_EDIT_FRAME* aParent );

private:
    bool TransferDataToWindow() override;
    bool TransferDataFromWindow() override;

    PCB_EDIT_FRAME* m_frame;

    wxTextCtrl* m_latCtrl;
    wxTextCtrl* m_lonCtrl;
    wxTextCtrl* m_rotCtrl;
};

#endif // DIALOG_SITE_ORIGIN_H
