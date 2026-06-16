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

    /// Override EDA_BASE_FRAME's close hook to prompt for unsaved changes.
    bool canCloseWindow( wxCloseEvent& aCloseEvent ) override;

    /// Re-read cables from the source BOARD's net list.
    void RefreshFromBoard();

    /// Used by the PCB-side routing tool: list conduit names that the user can pick.
    std::vector<wxString> GetConduitNames() const;

    /// Like GetConduitNames(), but only conduits that have a Conduit Spec assigned.
    /// Routing requires a spec (it supplies bend radius / clearance / max bend angle).
    std::vector<wxString> GetRoutableConduitNames() const;

    /// Display label for a conduit including its connections + net class, e.g.
    /// "C-101 (MB1 - MB2 - MV)". Falls back to the bare name if unknown.
    wxString GetConduitConnectionLabel( const wxString& aConduitName ) const;

    /// Board positions of the pad terminations the conduit's cables attach to, so
    /// the routing tool can highlight them. Resolved from each cable's pad UUIDs.
    std::vector<wxPoint> GetConduitTerminationPoints( const wxString& aConduitName,
                                                      BOARD* aBoard ) const;

    /// The conduit's currently-assigned routing layer (PCB_LAYER_ID), or -1 if none.
    int GetConduitRouteLayer( const wxString& aConduitName ) const;

    /// The assigned (non-Default) net class of the conduit's circuits, or empty.
    /// Used to auto-pick the routing layer (a copper layer named to match the class).
    wxString GetConduitNetClassName( const wxString& aConduitName ) const;

    /// Conduits that already have a route (>= 2 points) — the editable set.
    std::vector<wxString> GetRoutedConduitNames() const;

    /// Fetch a conduit's current route layer + virtual-center points. Returns false
    /// if the conduit isn't found or has no route.
    bool GetConduitRoute( const wxString& aConduitName, int& aLayer,
                          std::vector<wxPoint>& aPoints ) const;

    /// Anchor one end of a conduit to a footprint. The endpoint keeps its current
    /// position; the stored offset = (endpoint − footprint origin). Saves + pushes
    /// the overlay. Returns false if the conduit isn't found / has no route.
    bool AnchorConduitEnd( const wxString& aConduitName, bool aAtFront,
                           const KIID& aFootprintUuid, const wxPoint& aFootprintOrigin );

    /// For every anchored conduit endpoint, re-derive its position from the current
    /// footprint origin (origin + stored offset) and recompute the faulty state.
    /// Updates the overlay if anything moved. Returns true if anything changed.
    bool UpdateAnchoredEndpoints( BOARD* aBoard );

    /// Report whether each end of a conduit currently has an anchor.
    void GetConduitAnchorFlags( const wxString& aConduitName, bool& aStart, bool& aEnd ) const;

    /// Remove the anchor on one end of a conduit (geometry unchanged). Saves + refreshes.
    void RemoveConduitAnchor( const wxString& aConduitName, bool aAtFront );

    /// Clear a conduit's entire route (points + both anchors). Saves + refreshes.
    void ClearConduitRoute( const wxString& aConduitName );

    /// Open the numeric "Edit Conduit Route Points" dialog for a conduit (callable
    /// from the Site Layout right-click menu). Saves + refreshes if changed.
    void EditRoutePointsDialog( const wxString& aConduitName );

    /// Collision data for the routing tool. One entry per conduit that already has
    /// a route. Clearance and half-width are pre-computed in board IU.
    struct ROUTE_FOR_COLLISION
    {
        wxString             conduitName;
        int                  layer;
        std::vector<wxPoint> points;
        int                  clearanceIu;   // required spacing from this route
        int                  halfWidthIu;   // half of this conduit's diameter
    };
    /// Returns existing routes, optionally excluding one (the conduit being re-routed).
    std::vector<ROUTE_FOR_COLLISION> GetAllRoutesForCollision(
            const wxString& aExcludeName = wxEmptyString ) const;

    /// Effective clearance for a conduit (looks up its spec, falls back to 0).
    /// Returned in board IU.
    int GetConduitClearanceIu( const wxString& aConduitName ) const;

    /// Effective half-width for a conduit (half of its inner diameter). Returns IU.
    int GetConduitHalfWidthIu( const wxString& aConduitName ) const;

    /// Minimum bend radius for a conduit, from its spec, in board IU. 0 if no spec
    /// or spec has no bend radius (fillets are then skipped / sharp corners).
    int GetConduitBendRadiusIu( const wxString& aConduitName ) const;

    /// Steepest single-corner bend angle (degrees of deflection) allowed for a
    /// conduit, from its spec. Returns the spec default (90) if no spec assigned.
    double GetConduitMaxBendAngleDeg( const wxString& aConduitName ) const;

    /// Used by the PCB-side routing tool: write a freshly-drawn polyline to the
    /// named conduit. Updates layer + points, recomputes lengths, refreshes UI,
    /// marks dirty, and persists to .kicad_cnd. No-op if name not found.
    void SetConduitRoute( const wxString& aConduitName, int aLayer,
                          const std::vector<wxPoint>& aPoints );

private:
    void setupMenuBar();
    void setupToolBar();
    void setupBody();

    void refreshConduitList();
    void refreshCableList();

    /// Build "Name (Equip - Equip - NetClass)" for a conduit from its cables.
    wxString buildConnectionSummary( const CONDUIT* aConduit ) const;
    void editConduit( CONDUIT* aConduit );
    /// Returns true if the user committed changes (so caller can refresh).
    bool editConduitRoutePoints( CONDUIT* aConduit );
    void deleteConduit( CONDUIT* aConduit );

    /// Find a CABLE for (netCode, fromRef) in m_cables, or create one.
    /// - For nets with <=2 connected components the cable is "shared": all perspectives
    ///   resolve to the same CABLE (so assigning either endpoint mirrors).
    /// - For nets with >=3 connected components ("buses" like GND), each perspective
    ///   gets its own CABLE so they can be routed through different conduits.
    CABLE* findOrCreateCable( int aNetCode, const wxString& aFromRef,
                              const wxString& aCurrentName, const wxString& aToRef );

    /// Returns the BOARD net code currently selected in the cable list, or -1.
    int getSelectedNetCode() const;

    /// Returns the BOARD net name currently selected in the cable list, or empty.
    wxString getSelectedNetName() const;

    /// Returns the From / To component shown in the currently-selected list row.
    wxString getSelectedFromRef() const;
    wxString getSelectedToRef() const;

    /// Number of distinct components connected to a given net code on the board.
    int countComponentsOnNet( int aNetCode ) const;

    /// Walk owned CABLEs and refresh their names from the board (catches net renames).
    void syncCablesFromBoard();

    /// Returns the conduit currently selected (canvas selection wins).
    CONDUIT* getSelectedConduit() const;

    void assignSelectedCableToSelectedConduit();

    // ---------- Persistence ----------
    /// Derive the default .kicad_cnd path from the active board's file path.
    /// Empty string if no board or board is unsaved.
    wxString deriveDefaultCndPath() const;

    /// Auto-load the .kicad_cnd file for the current board if it exists.
    void tryAutoLoad();

    bool saveToFile( const wxString& aPath );
    bool loadFromFile( const wxString& aPath );

    /// Returns true if the file was saved (or the user chose to discard).
    /// Returns false if the user cancelled.
    bool promptSaveIfDirty();

    void setDirty( bool aDirty );
    void updateTitle();

    // Event handlers
    void onAddConduit( wxCommandEvent& aEvent );
    void onRefreshCables( wxCommandEvent& aEvent );
    void onAssignCable( wxCommandEvent& aEvent );
    void onSave( wxCommandEvent& aEvent );
    void onSaveAs( wxCommandEvent& aEvent );
    void onExportCircuitList( wxCommandEvent& aEvent );
    void onExportRacewayList( wxCommandEvent& aEvent );
    void onConduitListActivated( wxListEvent& aEvent );
    void onConduitListSelected( wxListEvent& aEvent );
    void onCableActivated( wxListEvent& aEvent );
    void onClose( wxCloseEvent& aEvent );

    // Data
    BOARD*                                m_board;       // non-owning
    std::vector<std::unique_ptr<CONDUIT>> m_conduits;
    std::vector<std::unique_ptr<CABLE>>   m_cables;
    int                                   m_nextConduitNumber;

    // Persistence state
    wxString m_filePath;     // current .kicad_cnd path; empty if never saved
    bool     m_dirty;

    // Session-only "don't ask again" preferences
    bool m_skipDeleteConfirm = false;

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
