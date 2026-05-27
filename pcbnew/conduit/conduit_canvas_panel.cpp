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
    SetBackgroundColour( wxColour( 30, 30, 30 ) );
    SetScrollRate( 20, 20 );

    Bind( wxEVT_PAINT,         &CONDUIT_CANVAS_PANEL::onPaint,     this );
    Bind( wxEVT_LEFT_DOWN,     &CONDUIT_CANVAS_PANEL::onLeftDown,  this );
    Bind( wxEVT_LEFT_DCLICK,   &CONDUIT_CANVAS_PANEL::onLeftDClick,this );
    Bind( wxEVT_RIGHT_DOWN,    &CONDUIT_CANVAS_PANEL::onRightDown, this );
    Bind( wxEVT_SIZE,          &CONDUIT_CANVAS_PANEL::onSize,      this );
}


wxPoint CONDUIT_CANVAS_PANEL::toVirtual( const wxPoint& aClient ) const
{
    wxPoint v;
    CalcUnscrolledPosition( aClient.x, aClient.y, &v.x, &v.y );
    return v;
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

    int clientW = GetClientSize().GetWidth();
    int cols    = std::max( 1, ( clientW - OUTER_PADDING ) / ( CONDUIT_WIDTH + OUTER_PADDING ) );

    int x          = OUTER_PADDING;
    int y          = OUTER_PADDING;
    int col        = 0;
    int rowHeight  = 0;
    int maxRightX  = 0;

    for( const std::unique_ptr<CONDUIT>& c : *m_conduits )
    {
        int cableCount = static_cast<int>( c->GetCables().size() );
        int bodyH      = std::max( CONDUIT_MIN_BODY_H,
                                   cableCount * CABLE_ROW_H + INNER_PADDING * 2 );
        int totalH     = CONDUIT_HEADER_H + bodyH + CONDUIT_FOOTER_H;

        m_layout[ c.get() ] = wxRect( x, y, CONDUIT_WIDTH, totalH );

        rowHeight = std::max( rowHeight, totalH );
        maxRightX = std::max( maxRightX, x + CONDUIT_WIDTH );

        col++;
        if( col >= cols )
        {
            col = 0;
            x   = OUTER_PADDING;
            y  += rowHeight + OUTER_PADDING;
            rowHeight = 0;
        }
        else
        {
            x += CONDUIT_WIDTH + OUTER_PADDING;
        }
    }

    int virtualW = maxRightX + OUTER_PADDING;
    int virtualH = y + rowHeight + OUTER_PADDING;

    SetVirtualSize( virtualW, virtualH );
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

    dc.SetBackground( wxBrush( GetBackgroundColour() ) );
    dc.Clear();

    m_cableRects.clear();   // rebuilt during draw

    if( !m_conduits || m_conduits->empty() )
    {
        dc.SetTextForeground( wxColour( 160, 160, 160 ) );
        dc.SetFont( wxFont( wxFontInfo( 10 ) ) );
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
}


void CONDUIT_CANVAS_PANEL::drawConduit( wxDC& aDC, const CONDUIT* aConduit,
                                        const wxRect& aRect )
{
    const bool selected = ( aConduit == m_selected );

    // Body fill — color shifts with fill % (green → yellow → red).
    double fillPct = aConduit->ComputeFillPercent();
    double maxFill = std::max( 1.0, aConduit->GetMaxFillPercent() );
    double ratio   = std::min( 1.0, fillPct / maxFill );

    wxColour bodyColor;
    if( ratio < 0.5 )
        bodyColor = wxColour( 245, 250, 245 );    // near-white, slight green tint
    else if( ratio < 0.9 )
        bodyColor = wxColour( 252, 248, 220 );    // pale yellow
    else
        bodyColor = wxColour( 250, 225, 225 );    // pale red

    // Border
    wxColour borderColor = selected ? wxColour( 60, 140, 220 ) : wxColour( 80, 80, 80 );
    int      borderW     = selected ? 3 : 1;

    aDC.SetPen( wxPen( borderColor, borderW ) );
    aDC.SetBrush( wxBrush( bodyColor ) );
    aDC.DrawRoundedRectangle( aRect, 6 );

    // Header bar
    wxRect headerRect( aRect.x, aRect.y, aRect.width, CONDUIT_HEADER_H );
    aDC.SetPen( *wxTRANSPARENT_PEN );
    aDC.SetBrush( wxBrush( selected ? wxColour( 60, 140, 220 )
                                    : wxColour( 200, 200, 210 ) ) );
    aDC.DrawRectangle( headerRect.x + 1, headerRect.y + 1,
                       headerRect.width - 2, headerRect.height );

    // Header text
    aDC.SetFont( wxFont( wxFontInfo( 10 ).Bold() ) );
    aDC.SetTextForeground( selected ? *wxWHITE : *wxBLACK );
    aDC.DrawText( aConduit->GetName(),
                  aRect.x + INNER_PADDING, aRect.y + 7 );

    wxString typeStr = wxString::Format( wxT( "%s  %.2f\"" ),
                                         ConduitTypeToString( aConduit->GetType() ),
                                         aConduit->GetDiameterInches() );
    wxSize typeSize = aDC.GetTextExtent( typeStr );
    aDC.SetFont( wxFont( wxFontInfo( 9 ) ) );
    aDC.DrawText( typeStr,
                  aRect.GetRight() - typeSize.GetWidth() - INNER_PADDING,
                  aRect.y + 8 );

    // Body — list of cables
    int cableY = aRect.y + CONDUIT_HEADER_H + INNER_PADDING;
    aDC.SetFont( wxFont( wxFontInfo( 9 ) ) );
    aDC.SetTextForeground( wxColour( 40, 40, 40 ) );

    if( aConduit->GetCables().empty() )
    {
        aDC.SetTextForeground( wxColour( 130, 130, 130 ) );
        aDC.DrawText( _( "(no cables assigned)" ),
                      aRect.x + INNER_PADDING + 4, cableY );
    }
    else
    {
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

            // Bullet
            aDC.SetBrush( wxBrush( orphan ? wxColour( 180, 40, 40 )
                                          : wxColour( 60, 90, 160 ) ) );
            aDC.SetPen( *wxTRANSPARENT_PEN );
            aDC.DrawCircle( aRect.x + INNER_PADDING + 4, cableY + 7, 3 );

            // Name (gray + tag if orphan)
            aDC.SetTextForeground( orphan ? wxColour( 130, 50, 50 )
                                          : wxColour( 40, 40, 40 ) );
            wxString label = cable->GetName();
            if( orphan )
                label += _( "  (orphan)" );
            aDC.DrawText( label, aRect.x + INNER_PADDING + 14, cableY );
            cableY += CABLE_ROW_H;
        }
    }

    // Footer — fill %
    int footerY = aRect.GetBottom() - CONDUIT_FOOTER_H + 4;
    wxColour fillColor;
    if( ratio < 0.5 )
        fillColor = wxColour( 30, 130, 60 );
    else if( ratio < 0.9 )
        fillColor = wxColour( 170, 130, 0 );
    else
        fillColor = wxColour( 180, 40, 40 );

    aDC.SetFont( wxFont( wxFontInfo( 9 ) ) );
    aDC.SetTextForeground( fillColor );
    aDC.DrawText( wxString::Format( _( "Fill: %.1f%% / %.0f%%" ),
                                    fillPct, aConduit->GetMaxFillPercent() ),
                  aRect.x + INNER_PADDING, footerY );
}


CONDUIT* CONDUIT_CANVAS_PANEL::hitTest( const wxPoint& aClient ) const
{
    wxPoint virt = toVirtual( aClient );
    for( const std::unique_ptr<CONDUIT>& c : *m_conduits )
    {
        auto it = m_layout.find( c.get() );
        if( it != m_layout.end() && it->second.Contains( virt ) )
            return c.get();
    }
    return nullptr;
}


CONDUIT_CANVAS_PANEL::CableHit CONDUIT_CANVAS_PANEL::hitTestCable( const wxPoint& aClient ) const
{
    wxPoint virt = toVirtual( aClient );
    for( const CableRect& cr : m_cableRects )
    {
        if( cr.rect.Contains( virt ) )
            return CableHit{ cr.conduit, cr.cable };
    }
    return CableHit{ nullptr, nullptr };
}


void CONDUIT_CANVAS_PANEL::onLeftDown( wxMouseEvent& aEvent )
{
    SetFocus();
    CONDUIT* hit = hitTest( aEvent.GetPosition() );
    SetSelected( hit );    // nullptr if clicked empty space
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

    CableHit cableHit = hitTestCable( aEvent.GetPosition() );
    if( !cableHit.cable )
        return;     // right-clicked on conduit body / empty space — nothing to do yet

    // Select the parent conduit so the user has clear visual feedback.
    SetSelected( cableHit.conduit );

    enum
    {
        ID_REMOVE = wxID_HIGHEST + 1,
        ID_RELINK,
    };

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

    int sel = GetPopupMenuSelectionFromUser( menu, aEvent.GetPosition() );

    if( sel == ID_REMOVE && m_onCableRemove )
        m_onCableRemove( cableHit.conduit, cableHit.cable );
    else if( sel == ID_RELINK && m_onCableRelink )
        m_onCableRelink( cableHit.conduit, cableHit.cable );
}
