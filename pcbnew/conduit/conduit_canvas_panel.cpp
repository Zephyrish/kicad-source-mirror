/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Conduit canvas implementation.
 */

#include "conduit_canvas_panel.h"

#include <algorithm>

#include <wx/dcbuffer.h>
#include <wx/menu.h>
#include <wx/settings.h>


CONDUIT_CANVAS_PANEL::CONDUIT_CANVAS_PANEL( wxWindow* aParent,
                                            std::vector<std::unique_ptr<CONDUIT>>* aConduits ) :
    wxScrolledCanvas( aParent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                      wxFULL_REPAINT_ON_RESIZE ),
    m_conduits( aConduits ),
    m_selected( nullptr )
{
    SetBackgroundStyle( wxBG_STYLE_PAINT );      // for wxAutoBufferedPaintDC
    // Off-white "paper" background like a schematic sheet.
    SetBackgroundColour( wxColour( 250, 250, 245 ) );
    SetScrollRate( 20, 20 );

    Bind( wxEVT_PAINT,                 &CONDUIT_CANVAS_PANEL::onPaint,        this );
    Bind( wxEVT_LEFT_DOWN,             &CONDUIT_CANVAS_PANEL::onLeftDown,     this );
    Bind( wxEVT_LEFT_UP,               &CONDUIT_CANVAS_PANEL::onLeftUp,       this );
    Bind( wxEVT_MOTION,                &CONDUIT_CANVAS_PANEL::onMotion,       this );
    Bind( wxEVT_MOUSE_CAPTURE_LOST,    &CONDUIT_CANVAS_PANEL::onCaptureLost,  this );
    Bind( wxEVT_LEFT_DCLICK,           &CONDUIT_CANVAS_PANEL::onLeftDClick,   this );
    Bind( wxEVT_RIGHT_DOWN,            &CONDUIT_CANVAS_PANEL::onRightDown,    this );
    Bind( wxEVT_RIGHT_UP,              &CONDUIT_CANVAS_PANEL::onRightUp,      this );
    Bind( wxEVT_MIDDLE_DOWN,           &CONDUIT_CANVAS_PANEL::onMiddleDown,   this );
    Bind( wxEVT_MIDDLE_UP,             &CONDUIT_CANVAS_PANEL::onMiddleUp,     this );
    Bind( wxEVT_MOUSEWHEEL,            &CONDUIT_CANVAS_PANEL::onMouseWheel,   this );
    Bind( wxEVT_CHAR_HOOK,             &CONDUIT_CANVAS_PANEL::onKeyDown,      this );
    Bind( wxEVT_SIZE,                  &CONDUIT_CANVAS_PANEL::onSize,         this );
}


wxPoint CONDUIT_CANVAS_PANEL::toWorld( const wxPoint& aClient ) const
{
    wxPoint v;
    CalcUnscrolledPosition( aClient.x, aClient.y, &v.x, &v.y );
    return wxPoint( static_cast<int>( v.x / m_zoom ),
                    static_cast<int>( v.y / m_zoom ) );
}


void CONDUIT_CANVAS_PANEL::setZoom( double aZoom, const wxPoint* aAnchorClient )
{
    aZoom = std::clamp( aZoom, 0.25, 4.0 );
    if( aZoom == m_zoom )
        return;

    wxPoint worldBefore;
    if( aAnchorClient )
        worldBefore = toWorld( *aAnchorClient );

    m_zoom = aZoom;
    recalcLayout();

    if( aAnchorClient )
    {
        // Adjust scroll so the world point under the anchor stays under the anchor.
        int newVirtX = static_cast<int>( worldBefore.x * m_zoom );
        int newVirtY = static_cast<int>( worldBefore.y * m_zoom );

        int scrollPxX = newVirtX - aAnchorClient->x;
        int scrollPxY = newVirtY - aAnchorClient->y;

        int xRate = 1, yRate = 1;
        GetScrollPixelsPerUnit( &xRate, &yRate );

        Scroll( std::max( 0, scrollPxX / std::max( 1, xRate ) ),
                std::max( 0, scrollPxY / std::max( 1, yRate ) ) );
    }

    Refresh();
}


void CONDUIT_CANVAS_PANEL::RefreshLayout()
{
    // If the selected conduit was removed, drop the selection.
    if( m_selected )
    {
        bool stillExists = std::any_of( m_conduits->begin(), m_conduits->end(),
                [this]( const std::unique_ptr<CONDUIT>& c ) { return c.get() == m_selected; } );
        if( !stillExists )
            m_selected = nullptr;
    }

    recalcLayout();
    Refresh();
}


void CONDUIT_CANVAS_PANEL::SetSelected( CONDUIT* aConduit )
{
    if( aConduit == m_selected )
        return;
    m_selected = aConduit;
    Refresh();
    if( m_onSelectionChanged )
        m_onSelectionChanged( m_selected );
}


void CONDUIT_CANVAS_PANEL::recalcLayout()
{
    m_layout.clear();

    int maxRight  = 0;
    int maxBottom = 0;

    for( const std::unique_ptr<CONDUIT>& c : *m_conduits )
    {
        int cableCount = static_cast<int>( c->GetCables().size() );
        int bodyH      = std::max( CONDUIT_MIN_BODY_H,
                                   cableCount * CABLE_ROW_H + INNER_PADDING * 2 );
        int totalH     = CONDUIT_HEADER_H + bodyH + CONDUIT_FOOTER_H;

        wxRect r( c->GetPosX(), c->GetPosY(), CONDUIT_WIDTH, totalH );
        m_layout[ c.get() ] = r;

        maxRight  = std::max( maxRight,  r.GetRight() );
        maxBottom = std::max( maxBottom, r.GetBottom() );
    }

    // Virtual size accounts for current zoom — drawing happens in world coords
    // and is scaled by m_zoom via SetUserScale in onPaint.
    int virtW = static_cast<int>( ( maxRight + 400 ) * m_zoom );
    int virtH = static_cast<int>( ( maxBottom + 400 ) * m_zoom );
    SetVirtualSize( virtW, virtH );
}


void CONDUIT_CANVAS_PANEL::onSize( wxSizeEvent& aEvent )
{
    recalcLayout();
    Refresh();
    aEvent.Skip();
}


void CONDUIT_CANVAS_PANEL::onPaint( wxPaintEvent& aEvent )
{
    wxAutoBufferedPaintDC dc( this );
    DoPrepareDC( dc );

    // Apply zoom — all subsequent drawing is in world coords, scaled by m_zoom.
    dc.SetUserScale( m_zoom, m_zoom );

    dc.SetBackground( wxBrush( GetBackgroundColour() ) );
    dc.Clear();

    // ---- Draw schematic-style dot grid (world coords, GRID_STEP world units) ----
    constexpr int GRID_STEP = 25;
    wxSize  clientSize = GetClientSize();
    wxPoint topLeftW   = toWorld( wxPoint( 0, 0 ) );
    wxPoint botRightW  = toWorld( wxPoint( clientSize.GetWidth(), clientSize.GetHeight() ) );

    int startX = ( topLeftW.x  / GRID_STEP ) * GRID_STEP;
    int startY = ( topLeftW.y  / GRID_STEP ) * GRID_STEP;

    dc.SetPen( *wxTRANSPARENT_PEN );
    dc.SetBrush( wxBrush( wxColour( 200, 200, 195 ) ) );
    for( int gx = startX; gx <= botRightW.x + GRID_STEP; gx += GRID_STEP )
    {
        for( int gy = startY; gy <= botRightW.y + GRID_STEP; gy += GRID_STEP )
            dc.DrawRectangle( gx, gy, 1, 1 );
    }

    m_cableRects.clear();   // rebuilt during draw

    if( !m_conduits || m_conduits->empty() )
    {
        dc.SetTextForeground( wxColour( 130, 130, 125 ) );
        dc.SetFont( wxFont( wxFontInfo( 10 ).Italic() ) );
        dc.DrawText( _( "No conduits yet. Click \"Add Conduit\" in the toolbar." ),
                     OUTER_PADDING, OUTER_PADDING );
        return;
    }

    for( const std::unique_ptr<CONDUIT>& c : *m_conduits )
    {
        auto it = m_layout.find( c.get() );
        if( it != m_layout.end() )
            drawConduit( dc, c.get(), it->second );
    }

    // ---- Rubber-band selection rect ----
    if( m_rubbering )
    {
        int x = std::min( m_rubberStartWorld.x, m_rubberEndWorld.x );
        int y = std::min( m_rubberStartWorld.y, m_rubberEndWorld.y );
        int w = std::abs( m_rubberEndWorld.x - m_rubberStartWorld.x );
        int h = std::abs( m_rubberEndWorld.y - m_rubberStartWorld.y );
        dc.SetPen( wxPen( wxColour( 80, 130, 230 ), 1, wxPENSTYLE_LONG_DASH ) );
        dc.SetBrush( *wxTRANSPARENT_BRUSH );
        dc.DrawRectangle( x, y, w, h );
    }
}


void CONDUIT_CANVAS_PANEL::drawConduit( wxDC& aDC, const CONDUIT* aConduit,
                                        const wxRect& aRect )
{
    const bool selected = ( aConduit == m_selected )
                       || ( m_multiSelected.count( const_cast<CONDUIT*>( aConduit ) ) > 0 );

    // ---- Schematic colors ----
    const wxColour SHEET_FG      ( 40, 40, 40 );        // schematic line color
    const wxColour SHEET_BG      ( 255, 255, 255 );     // pure white "body"
    const wxColour SHEET_SELECTED( 80, 130, 230 );      // KiCad-ish blue
    const wxColour TEXT_LABEL    ( 30, 30, 30 );
    const wxColour TEXT_MUTED    ( 120, 120, 120 );
    const wxColour PIN_COLOR     ( 130, 80, 30 );       // brownish pin (eeschema)
    const wxColour ORPHAN_COLOR  ( 200, 50, 50 );

    // ---- Body: pure white rectangle, thin square corners ----
    aDC.SetPen( wxPen( selected ? SHEET_SELECTED : SHEET_FG, selected ? 2 : 1 ) );
    aDC.SetBrush( wxBrush( SHEET_BG ) );
    aDC.DrawRectangle( aRect );

    // ---- Header separator (line under header area) ----
    aDC.SetPen( wxPen( SHEET_FG, 1 ) );
    int headerLineY = aRect.y + CONDUIT_HEADER_H;
    aDC.DrawLine( aRect.x, headerLineY, aRect.GetRight(), headerLineY );

    // ---- Header text: name (bold left), type (right) ----
    aDC.SetFont( wxFont( wxFontInfo( 10 ).Bold() ) );
    aDC.SetTextForeground( TEXT_LABEL );
    aDC.DrawText( aConduit->GetName(),
                  aRect.x + INNER_PADDING, aRect.y + 7 );

    wxString typeStr = wxString::Format( wxT( "%s  %.2f\"" ),
                                         ConduitTypeToString( aConduit->GetType() ),
                                         aConduit->GetDiameterInches() );
    aDC.SetFont( wxFont( wxFontInfo( 9 ) ) );
    aDC.SetTextForeground( TEXT_MUTED );
    wxSize typeSize = aDC.GetTextExtent( typeStr );
    aDC.DrawText( typeStr,
                  aRect.GetRight() - typeSize.GetWidth() - INNER_PADDING,
                  aRect.y + 9 );

    // ---- Body: cables as labeled rows with pin terminals on each side ----
    int cableY = headerLineY + INNER_PADDING;
    aDC.SetFont( wxFont( wxFontInfo( 9 ) ) );

    if( aConduit->GetCables().empty() )
    {
        aDC.SetTextForeground( TEXT_MUTED );
        aDC.SetFont( wxFont( wxFontInfo( 9 ).Italic() ) );
        aDC.DrawText( _( "(no cables assigned)" ),
                      aRect.x + INNER_PADDING + 8, cableY + 4 );
        aDC.SetFont( wxFont( wxFontInfo( 9 ) ) );
    }
    else
    {
        // Pin terminal length (extends inward from the rectangle edge)
        constexpr int PIN_LEN     = 10;
        constexpr int REF_FONT_PT = 8;

        wxFont refFont{ wxFontInfo( REF_FONT_PT ) };
        wxFont nameFont{ wxFontInfo( 9 ) };

        for( CABLE* cable : aConduit->GetCables() )
        {
            const bool orphan = cable->IsOrphan();

            // Record the row rect for right-click hit-testing
            CableRect cr;
            cr.conduit = const_cast<CONDUIT*>( aConduit );
            cr.cable   = cable;
            cr.rect    = wxRect( aRect.x + INNER_PADDING, cableY,
                                 aRect.width - 2 * INNER_PADDING, CABLE_ROW_H );
            m_cableRects.push_back( cr );

            int rowMidY = cableY + CABLE_ROW_H / 2;

            // Pin terminals: short horizontal lines from edge inward
            aDC.SetPen( wxPen( orphan ? ORPHAN_COLOR : PIN_COLOR, 1 ) );
            aDC.DrawLine( aRect.x, rowMidY, aRect.x + PIN_LEN, rowMidY );
            aDC.DrawLine( aRect.GetRight() - PIN_LEN, rowMidY,
                          aRect.GetRight(), rowMidY );

            // Pin endpoint dots
            aDC.SetBrush( wxBrush( orphan ? ORPHAN_COLOR : PIN_COLOR ) );
            aDC.SetPen( *wxTRANSPARENT_PEN );
            aDC.DrawCircle( aRect.x + PIN_LEN, rowMidY, 2 );
            aDC.DrawCircle( aRect.GetRight() - PIN_LEN, rowMidY, 2 );

            // From/To component refs in small font, just inside the pin endpoints.
            aDC.SetFont( refFont );
            aDC.SetTextForeground( orphan ? ORPHAN_COLOR : TEXT_MUTED );

            const wxString& fromRef = cable->GetFromRef();
            const wxString& toRef   = cable->GetToRef();

            int leftRefX  = aRect.x + PIN_LEN + 4;
            int leftRefEnd = leftRefX;
            if( !fromRef.IsEmpty() )
            {
                aDC.DrawText( fromRef, leftRefX, cableY + 1 );
                leftRefEnd = leftRefX + aDC.GetTextExtent( fromRef ).GetWidth();
            }

            int rightRefRightEdge = aRect.GetRight() - PIN_LEN - 4;
            int rightRefStart     = rightRefRightEdge;
            if( !toRef.IsEmpty() )
            {
                wxSize ts = aDC.GetTextExtent( toRef );
                rightRefStart = rightRefRightEdge - ts.GetWidth();
                aDC.DrawText( toRef, rightRefStart, cableY + 1 );
            }

            // Cable name centered between From and To.
            aDC.SetFont( nameFont );
            aDC.SetTextForeground( orphan ? ORPHAN_COLOR : TEXT_LABEL );
            wxString label = cable->GetName();
            if( orphan )
                label += _( "  (orphan)" );

            wxSize  ns        = aDC.GetTextExtent( label );
            int     midSpaceL = leftRefEnd + 6;
            int     midSpaceR = rightRefStart - 6;
            int     labelX;
            if( midSpaceR - midSpaceL >= ns.GetWidth() )
                labelX = midSpaceL + ( midSpaceR - midSpaceL - ns.GetWidth() ) / 2;
            else
                labelX = midSpaceL;     // not enough room; left-align after From

            aDC.DrawText( label, labelX, cableY );

            cableY += CABLE_ROW_H;
        }
    }

    // ---- Footer separator + fill text ----
    int footerLineY = aRect.GetBottom() - CONDUIT_FOOTER_H;
    aDC.SetPen( wxPen( SHEET_FG, 1 ) );
    aDC.DrawLine( aRect.x, footerLineY, aRect.GetRight(), footerLineY );

    aDC.SetFont( wxFont( wxFontInfo( 9 ) ) );

    if( !aConduit->HasAllCableSizesKnown() )
    {
        aDC.SetTextForeground( wxColour( 180, 40, 40 ) );
        aDC.DrawText( _( "Error: cable size not assigned" ),
                      aRect.x + INNER_PADDING, footerLineY + 4 );
        return;
    }

    double fillPct = aConduit->ComputeFillPercent();
    double maxFill = std::max( 1.0, aConduit->GetMaxFillPercent() );
    double ratio   = std::min( 1.0, fillPct / maxFill );

    wxColour fillColor;
    if( ratio < 0.5 )
        fillColor = wxColour( 30, 110, 50 );
    else if( ratio < 0.9 )
        fillColor = wxColour( 160, 120, 0 );
    else
        fillColor = wxColour( 180, 40, 40 );

    aDC.SetTextForeground( fillColor );
    aDC.DrawText( wxString::Format( _( "Fill: %.1f%% / %.0f%%" ),
                                    fillPct, aConduit->GetMaxFillPercent() ),
                  aRect.x + INNER_PADDING, footerLineY + 4 );
}


CONDUIT* CONDUIT_CANVAS_PANEL::hitTest( const wxPoint& aClient ) const
{
    wxPoint w = toWorld( aClient );
    for( const std::unique_ptr<CONDUIT>& c : *m_conduits )
    {
        auto it = m_layout.find( c.get() );
        if( it != m_layout.end() && it->second.Contains( w ) )
            return c.get();
    }
    return nullptr;
}


CONDUIT_CANVAS_PANEL::CableHit CONDUIT_CANVAS_PANEL::hitTestCable( const wxPoint& aClient ) const
{
    wxPoint w = toWorld( aClient );
    for( const CableRect& cr : m_cableRects )
    {
        if( cr.rect.Contains( w ) )
            return CableHit{ cr.conduit, cr.cable };
    }
    return CableHit{ nullptr, nullptr };
}


void CONDUIT_CANVAS_PANEL::onLeftDown( wxMouseEvent& aEvent )
{
    SetFocus();
    CONDUIT* hit = hitTest( aEvent.GetPosition() );

    if( hit )
    {
        // If the user clicked an already-multi-selected conduit, keep the group
        // so they can drag all of them together. Otherwise start a fresh selection.
        bool clickedSelectedMember = ( m_multiSelected.count( hit ) > 0 );

        if( !clickedSelectedMember )
            m_multiSelected.clear();

        SetSelected( hit );

        m_dragCandidate   = hit;
        m_clickStartWorld = toWorld( aEvent.GetPosition() );
        m_dragOffset      = wxPoint( m_clickStartWorld.x - hit->GetPosX(),
                                     m_clickStartWorld.y - hit->GetPosY() );

        // Capture offsets so all group members can be moved as one.
        m_dragGroupOffsets.clear();
        if( clickedSelectedMember && m_multiSelected.size() > 1 )
        {
            for( CONDUIT* c : m_multiSelected )
            {
                if( c == hit )
                    continue;
                m_dragGroupOffsets[ c ] = wxPoint( c->GetPosX() - hit->GetPosX(),
                                                   c->GetPosY() - hit->GetPosY() );
            }
        }
    }
    else
    {
        // Click on empty space: clear selection and arm for rubber-band drag.
        SetSelected( nullptr );
        m_multiSelected.clear();
        Refresh();

        m_dragCandidate    = nullptr;
        m_rubberCandidate  = true;
        m_rubberStartWorld = toWorld( aEvent.GetPosition() );
        m_rubberEndWorld   = m_rubberStartWorld;
    }
}


void CONDUIT_CANVAS_PANEL::onLeftUp( wxMouseEvent& aEvent )
{
    bool wasDragging = ( m_dragging != nullptr );
    CONDUIT* moved   = m_dragging;

    if( m_rubbering )
    {
        // Finalize rubber-band: select all conduits whose rect intersects.
        int rx = std::min( m_rubberStartWorld.x, m_rubberEndWorld.x );
        int ry = std::min( m_rubberStartWorld.y, m_rubberEndWorld.y );
        int rw = std::abs( m_rubberEndWorld.x - m_rubberStartWorld.x );
        int rh = std::abs( m_rubberEndWorld.y - m_rubberStartWorld.y );
        wxRect rb( rx, ry, rw, rh );

        m_multiSelected.clear();
        for( const std::unique_ptr<CONDUIT>& c : *m_conduits )
        {
            auto it = m_layout.find( c.get() );
            if( it != m_layout.end() && it->second.Intersects( rb ) )
                m_multiSelected.insert( c.get() );
        }

        // Promote first hit to primary selection so frame ops have a target.
        if( !m_multiSelected.empty() )
            SetSelected( *m_multiSelected.begin() );
    }

    m_dragCandidate    = nullptr;
    m_dragging         = nullptr;
    m_rubberCandidate  = false;
    m_rubbering        = false;
    m_dragGroupOffsets.clear();

    if( HasCapture() )
        ReleaseMouse();

    SetCursor( wxNullCursor );
    Refresh();

    if( wasDragging && moved && m_onConduitMoved )
        m_onConduitMoved( moved );
}


void CONDUIT_CANVAS_PANEL::onMotion( wxMouseEvent& aEvent )
{
    wxPoint client = aEvent.GetPosition();

    // ---- Active panning (middle or right drag) ----
    if( m_panning )
    {
        updatePan( client );
        return;
    }

    // ---- Right-button click → drag detection promotes to pan ----
    if( m_rightPending && aEvent.RightIsDown() )
    {
        constexpr int PAN_THRESHOLD = 4;
        int dx = std::abs( client.x - m_rightDownClient.x );
        int dy = std::abs( client.y - m_rightDownClient.y );
        if( dx + dy >= PAN_THRESHOLD )
        {
            m_rightPending = false;
            beginPan( m_rightDownClient );
            updatePan( client );
        }
        return;
    }

    // ---- Left-button states: drag conduit or rubber-band ----
    if( !aEvent.LeftIsDown() )
    {
        // Left released somewhere; clear any lingering left-state.
        if( m_dragCandidate || m_dragging || m_rubberCandidate || m_rubbering )
        {
            m_dragCandidate   = nullptr;
            m_dragging        = nullptr;
            m_rubberCandidate = false;
            m_rubbering       = false;
            if( HasCapture() )
                ReleaseMouse();
            SetCursor( wxNullCursor );
            Refresh();
        }
        return;
    }

    wxPoint curWorld = toWorld( client );

    // Conduit drag
    if( !m_dragging && m_dragCandidate )
    {
        constexpr int DRAG_THRESHOLD = 4;
        int dx = std::abs( curWorld.x - m_clickStartWorld.x );
        int dy = std::abs( curWorld.y - m_clickStartWorld.y );
        if( dx + dy >= DRAG_THRESHOLD )
        {
            m_dragging = m_dragCandidate;
            CaptureMouse();
            SetCursor( wxCursor( wxCURSOR_SIZING ) );
        }
    }
    if( m_dragging )
    {
        int newX = std::max( 0, curWorld.x - m_dragOffset.x );
        int newY = std::max( 0, curWorld.y - m_dragOffset.y );
        m_dragging->SetPosition( newX, newY );

        // Move every other group member by the same delta (preserves relative layout).
        for( const auto& [c, offset] : m_dragGroupOffsets )
        {
            if( c == m_dragging )
                continue;
            int gx = std::max( 0, newX + offset.x );
            int gy = std::max( 0, newY + offset.y );
            c->SetPosition( gx, gy );
        }

        recalcLayout();
        Refresh();
        return;
    }

    // Rubber-band promotion
    if( !m_rubbering && m_rubberCandidate )
    {
        constexpr int RUBBER_THRESHOLD = 4;
        int dx = std::abs( curWorld.x - m_rubberStartWorld.x );
        int dy = std::abs( curWorld.y - m_rubberStartWorld.y );
        if( dx + dy >= RUBBER_THRESHOLD )
        {
            m_rubbering = true;
            CaptureMouse();
        }
    }
    if( m_rubbering )
    {
        m_rubberEndWorld = curWorld;
        Refresh();
    }
}


void CONDUIT_CANVAS_PANEL::onCaptureLost( wxMouseCaptureLostEvent& aEvent )
{
    m_dragCandidate   = nullptr;
    m_dragging        = nullptr;
    m_rubberCandidate = false;
    m_rubbering       = false;
    m_panning         = false;
    m_rightPending    = false;
    m_dragGroupOffsets.clear();
    SetCursor( wxNullCursor );
}


void CONDUIT_CANVAS_PANEL::onLeftDClick( wxMouseEvent& aEvent )
{
    CONDUIT* hit = hitTest( aEvent.GetPosition() );
    if( hit )
    {
        SetSelected( hit );
        if( m_onActivated )
            m_onActivated( hit );
    }
}


void CONDUIT_CANVAS_PANEL::onRightDown( wxMouseEvent& aEvent )
{
    SetFocus();
    // Don't show context menu yet — wait until release to distinguish click vs drag-pan.
    m_rightPending    = true;
    m_rightDownClient = aEvent.GetPosition();
}


void CONDUIT_CANVAS_PANEL::onRightUp( wxMouseEvent& aEvent )
{
    if( m_panning )
    {
        endPan();
        m_rightPending = false;
        return;
    }

    if( m_rightPending )
    {
        m_rightPending = false;
        showContextMenuFor( aEvent.GetPosition() );
    }
}


void CONDUIT_CANVAS_PANEL::onMiddleDown( wxMouseEvent& aEvent )
{
    SetFocus();
    beginPan( aEvent.GetPosition() );
}


void CONDUIT_CANVAS_PANEL::onMiddleUp( wxMouseEvent& aEvent )
{
    if( m_panning )
        endPan();
}


void CONDUIT_CANVAS_PANEL::beginPan( const wxPoint& aClient )
{
    if( m_panning )
        return;
    m_panning        = true;
    m_panStartClient = aClient;
    m_panStartScrollX = GetScrollPos( wxHORIZONTAL );
    m_panStartScrollY = GetScrollPos( wxVERTICAL );
    if( !HasCapture() )
        CaptureMouse();
    SetCursor( wxCursor( wxCURSOR_HAND ) );
}


void CONDUIT_CANVAS_PANEL::updatePan( const wxPoint& aClient )
{
    int dx = aClient.x - m_panStartClient.x;
    int dy = aClient.y - m_panStartClient.y;

    int xRate = 1, yRate = 1;
    GetScrollPixelsPerUnit( &xRate, &yRate );
    xRate = std::max( 1, xRate );
    yRate = std::max( 1, yRate );

    int newX = m_panStartScrollX - ( dx / xRate );
    int newY = m_panStartScrollY - ( dy / yRate );

    Scroll( std::max( 0, newX ), std::max( 0, newY ) );
}


void CONDUIT_CANVAS_PANEL::endPan()
{
    m_panning = false;
    if( HasCapture() )
        ReleaseMouse();
    SetCursor( wxNullCursor );
}


void CONDUIT_CANVAS_PANEL::showContextMenuFor( const wxPoint& aClient )
{
    enum
    {
        ID_REMOVE = wxID_HIGHEST + 1,
        ID_RELINK,
        ID_DELETE_CONDUIT,
    };

    CableHit cableHit = hitTestCable( aClient );

    if( cableHit.cable )
    {
        SetSelected( cableHit.conduit );

        wxMenu menu;
        menu.Append( ID_REMOVE,
                     wxString::Format( _( "Remove '%s' from '%s'" ),
                                       cableHit.cable->GetName(),
                                       cableHit.conduit->GetName() ) );

        wxString relinkTargetName = m_relinkTarget ? m_relinkTarget() : wxString();
        bool     showRelink       = cableHit.cable->IsOrphan() && !relinkTargetName.IsEmpty();

        if( showRelink )
        {
            menu.AppendSeparator();
            menu.Append( ID_RELINK,
                         wxString::Format( _( "Re-link to selected cable: '%s'" ),
                                           relinkTargetName ) );
        }

        int sel = GetPopupMenuSelectionFromUser( menu, aClient );

        if( sel == ID_REMOVE && m_onCableRemove )
            m_onCableRemove( cableHit.conduit, cableHit.cable );
        else if( sel == ID_RELINK && m_onCableRelink )
            m_onCableRelink( cableHit.conduit, cableHit.cable );

        return;
    }

    CONDUIT* conduitHit = hitTest( aClient );
    if( !conduitHit )
        return;

    SetSelected( conduitHit );

    wxMenu menu;
    menu.Append( ID_DELETE_CONDUIT,
                 wxString::Format( _( "Delete '%s'..." ), conduitHit->GetName() ) );

    int sel = GetPopupMenuSelectionFromUser( menu, aClient );

    if( sel == ID_DELETE_CONDUIT && m_onConduitDelete )
        m_onConduitDelete( conduitHit );
}


void CONDUIT_CANVAS_PANEL::onMouseWheel( wxMouseEvent& aEvent )
{
    // Eeschema-style: plain wheel = zoom, centered on cursor.
    constexpr double STEP = 1.15;
    double newZoom = ( aEvent.GetWheelRotation() > 0 ) ? m_zoom * STEP : m_zoom / STEP;
    wxPoint anchor = aEvent.GetPosition();
    setZoom( newZoom, &anchor );
}


void CONDUIT_CANVAS_PANEL::onKeyDown( wxKeyEvent& aEvent )
{
    int key = aEvent.GetKeyCode();
    switch( key )
    {
    case '+':
    case WXK_NUMPAD_ADD:
        setZoom( m_zoom * 1.25 );
        break;
    case '-':
    case WXK_NUMPAD_SUBTRACT:
        setZoom( m_zoom / 1.25 );
        break;
    case '0':
    case WXK_NUMPAD0:
        setZoom( 1.0 );
        break;
    default:
        aEvent.Skip();
        return;
    }
}
