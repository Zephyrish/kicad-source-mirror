/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Conduit canvas — Phase 3 starter. Custom-painted view of conduits as rectangles.
 * Intentionally NOT using GAL yet — wxDC keeps the scope small. Swap to GAL when we
 * need real schematic features (zoom/pan/snap/wires).
 */

#ifndef CONDUIT_CANVAS_PANEL_H
#define CONDUIT_CANVAS_PANEL_H

#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

#include <wx/scrolwin.h>

#include "conduit_data.h"


class CONDUIT_CANVAS_PANEL : public wxScrolledCanvas
{
public:
    using ConduitHandler   = std::function<void( CONDUIT* )>;
    using CableActionFn    = std::function<void( CONDUIT*, CABLE* )>;
    using RelinkProviderFn = std::function<wxString()>;   // returns "" if unavailable

    CONDUIT_CANVAS_PANEL( wxWindow* aParent,
                          std::vector<std::unique_ptr<CONDUIT>>* aConduits );

    /// Notify the canvas that the conduit list or any conduit's data has changed.
    void RefreshLayout();

    CONDUIT* GetSelected() const { return m_selected; }
    void     SetSelected( CONDUIT* aConduit );

    void SetOnSelectionChanged( ConduitHandler aHandler ) { m_onSelectionChanged = aHandler; }
    void SetOnActivated( ConduitHandler aHandler )        { m_onActivated = aHandler; }
    void SetOnConduitDelete( ConduitHandler aHandler )    { m_onConduitDelete = aHandler; }
    void SetOnConduitMoved( ConduitHandler aHandler )     { m_onConduitMoved = aHandler; }

    void SetOnCableRemove( CableActionFn aHandler )       { m_onCableRemove = aHandler; }
    void SetOnCableRelink( CableActionFn aHandler )       { m_onCableRelink = aHandler; }

    /// Called by the canvas to ask the frame what the currently-selected list cable
    /// name is. Returns empty when no list cable is selected. Used to label/enable
    /// the "Re-link to..." context menu item.
    void SetRelinkTargetProvider( RelinkProviderFn aFn )  { m_relinkTarget = aFn; }

private:
    struct CableHit
    {
        CONDUIT* conduit;
        CABLE*   cable;
    };

    void onPaint( wxPaintEvent& aEvent );
    void onLeftDown( wxMouseEvent& aEvent );
    void onLeftUp( wxMouseEvent& aEvent );
    void onMotion( wxMouseEvent& aEvent );
    void onCaptureLost( wxMouseCaptureLostEvent& aEvent );
    void onLeftDClick( wxMouseEvent& aEvent );
    void onRightDown( wxMouseEvent& aEvent );
    void onRightUp( wxMouseEvent& aEvent );
    void onMiddleDown( wxMouseEvent& aEvent );
    void onMiddleUp( wxMouseEvent& aEvent );
    void onMouseWheel( wxMouseEvent& aEvent );
    void onKeyDown( wxKeyEvent& aEvent );
    void onSize( wxSizeEvent& aEvent );

    void showContextMenuFor( const wxPoint& aClient );
    void beginPan( const wxPoint& aClient );
    void updatePan( const wxPoint& aClient );
    void endPan();

    void     recalcLayout();
    void     drawConduit( wxDC& aDC, const CONDUIT* aConduit, const wxRect& aRect );
    CONDUIT* hitTest( const wxPoint& aClient ) const;
    CableHit hitTestCable( const wxPoint& aClient ) const;
    wxPoint  toWorld( const wxPoint& aClient ) const;

    /// Set zoom, optionally anchored at a client-space point so the world point
    /// under the cursor stays under the cursor.
    void setZoom( double aZoom, const wxPoint* aAnchorClient = nullptr );

    static constexpr int CONDUIT_WIDTH       = 220;
    static constexpr int CONDUIT_HEADER_H    = 30;
    static constexpr int CONDUIT_FOOTER_H    = 36;   // 2 lines: length + fill
    static constexpr int CABLE_ROW_H         = 16;
    static constexpr int CONDUIT_MIN_BODY_H  = 30;
    static constexpr int OUTER_PADDING       = 16;
    static constexpr int INNER_PADDING       = 8;

    // Per-cable rect inside a conduit card, in virtual coords, captured during paint.
    struct CableRect
    {
        CONDUIT* conduit;
        CABLE*   cable;
        wxRect   rect;
    };

    std::vector<std::unique_ptr<CONDUIT>>* m_conduits;     // non-owning
    std::unordered_map<CONDUIT*, wxRect>   m_layout;       // virtual coords
    std::vector<CableRect>                 m_cableRects;
    CONDUIT*                               m_selected;

    ConduitHandler   m_onSelectionChanged;
    ConduitHandler   m_onActivated;
    ConduitHandler   m_onConduitDelete;
    ConduitHandler   m_onConduitMoved;
    CableActionFn    m_onCableRemove;
    CableActionFn    m_onCableRelink;
    RelinkProviderFn m_relinkTarget;

    // Drag-conduit state (left button on a conduit)
    CONDUIT* m_dragCandidate = nullptr;
    CONDUIT* m_dragging      = nullptr;
    wxPoint  m_clickStartWorld;
    wxPoint  m_dragOffset;

    // Pan state (middle drag or right drag)
    bool    m_panning        = false;
    wxPoint m_panStartClient;
    int     m_panStartScrollX = 0;
    int     m_panStartScrollY = 0;

    // Right-button click vs drag (right click → menu; right drag → pan)
    bool    m_rightPending    = false;     // right is down but no drag yet
    wxPoint m_rightDownClient;

    // Rubber-band selection (left drag on empty space)
    bool    m_rubbering         = false;
    bool    m_rubberCandidate   = false;   // left-down on empty, not yet rubbering
    wxPoint m_rubberStartWorld;
    wxPoint m_rubberEndWorld;

    // Multi-selection (in addition to m_selected, the click-target primary)
    std::unordered_set<CONDUIT*> m_multiSelected;

    // When dragging a group, store each member's offset from the drag-target's origin
    // so all selected conduits move together.
    std::unordered_map<CONDUIT*, wxPoint> m_dragGroupOffsets;

    // Zoom — 1.0 = 100%
    double m_zoom = 1.0;
};

#endif // CONDUIT_CANVAS_PANEL_H
