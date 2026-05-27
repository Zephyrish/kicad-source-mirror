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


class BOARD;
class CONDUIT_CANVAS_PANEL;
class wxListCtrl;
class wxListEvent;
class wxPanel;
class wxSplitterWindow;
class wxToolBar;


class CONDUIT_SCHEMATIC_FRAME : public KIWAY_PLAYER
{
public:
    CONDUIT_SCHEMATIC_FRAME( KIWAY* aKiway, wxWindow* aParent, BOARD* aBoard );
    ~CONDUIT_SCHEMATIC_FRAME() override;

    wxWindow* GetToolCanvas() const override;

    /// Re-read cables from the source BOARD's net list.
    void RefreshFromBoard();

private:
    void setupMenuBar();
    void setupToolBar();
    void setupBody();

    void refreshConduitList();
    void refreshCableList();
    void editConduit( CONDUIT* aConduit );

    /// Find a CABLE for the given board net code in m_cables, or create one.
    /// The returned CABLE's name is updated to aCurrentName.
    CABLE* findOrCreateCable( int aNetCode, const wxString& aCurrentName );

    /// Returns the BOARD net code currently selected in the cable list, or -1.
    int getSelectedNetCode() const;

    /// Returns the BOARD net name currently selected in the cable list, or empty.
    wxString getSelectedNetName() const;

    /// Walk owned CABLEs and refresh their names from the board (catches net renames).
    void syncCablesFromBoard();

    /// Returns the conduit currently selected (canvas selection wins).
    CONDUIT* getSelectedConduit() const;

    void assignSelectedCableToSelectedConduit();

    // Event handlers
    void onAddConduit( wxCommandEvent& aEvent );
    void onRefreshCables( wxCommandEvent& aEvent );
    void onAssignCable( wxCommandEvent& aEvent );
    void onConduitListActivated( wxListEvent& aEvent );
    void onConduitListSelected( wxListEvent& aEvent );
    void onCableActivated( wxListEvent& aEvent );
    void onClose( wxCloseEvent& aEvent );

    // Data
    BOARD*                                m_board;       // non-owning
    std::vector<std::unique_ptr<CONDUIT>> m_conduits;
    std::vector<std::unique_ptr<CABLE>>   m_cables;      // owned, keyed by net name
    int                                   m_nextConduitNumber;

    // UI
    wxToolBar*            m_toolBar;
    wxSplitterWindow*     m_mainSplitter;
    wxSplitterWindow*     m_leftSplitter;
    wxListCtrl*           m_conduitListCtrl;
    wxListCtrl*           m_cableListCtrl;
    CONDUIT_CANVAS_PANEL* m_canvasPanel;

    DECLARE_EVENT_TABLE()
};

#endif // CONDUIT_SCHEMATIC_FRAME_H
