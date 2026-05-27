/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Conduit Schematic Editor — experimental editor for grouping cables into conduits.
 */

#ifndef CONDUIT_SCHEMATIC_FRAME_H
#define CONDUIT_SCHEMATIC_FRAME_H

#include <memory>
#include <vector>

#include <kiway_player.h>

#include "conduit/conduit_data.h"


// Unique window name so wxWindow::FindWindowByName can locate it
#define CONDUIT_SCHEMATIC_FRAME_NAME wxT( "ConduitSchematicFrame" )


class wxListCtrl;
class wxListEvent;
class wxPanel;
class wxSplitterWindow;
class wxToolBar;


class CONDUIT_SCHEMATIC_FRAME : public KIWAY_PLAYER
{
public:
    CONDUIT_SCHEMATIC_FRAME( KIWAY* aKiway, wxWindow* aParent );
    ~CONDUIT_SCHEMATIC_FRAME() override;

    wxWindow* GetToolCanvas() const override { return m_canvasPanel; }

private:
    void setupMenuBar();
    void setupToolBar();
    void setupBody();

    void refreshConduitList();
    void editConduit( CONDUIT* aConduit );

    // Event handlers
    void onAddConduit( wxCommandEvent& aEvent );
    void onConduitActivated( wxListEvent& aEvent );
    void onClose( wxCloseEvent& aEvent );

    // Returns the currently-selected conduit, or nullptr.
    CONDUIT* getSelectedConduit() const;

    // Data
    std::vector<std::unique_ptr<CONDUIT>> m_conduits;
    int                                   m_nextConduitNumber;

    // UI
    wxToolBar*        m_toolBar;
    wxSplitterWindow* m_splitter;
    wxListCtrl*       m_listCtrl;
    wxPanel*          m_canvasPanel;

    DECLARE_EVENT_TABLE()
};

#endif // CONDUIT_SCHEMATIC_FRAME_H
