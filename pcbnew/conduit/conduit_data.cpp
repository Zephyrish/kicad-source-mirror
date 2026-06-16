/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Conduit system — in-memory data model for Phase 1.
 */

#include "conduit_data.h"

#include <algorithm>
#include <cmath>


wxString ConduitTypeToString( CONDUIT_TYPE aType )
{
    switch( aType )
    {
    case CONDUIT_TYPE::EMT:  return wxS( "EMT" );
    case CONDUIT_TYPE::PVC:  return wxS( "PVC" );
    case CONDUIT_TYPE::RGS:  return wxS( "RGS" );
    case CONDUIT_TYPE::IMC:  return wxS( "IMC" );
    case CONDUIT_TYPE::FMC:  return wxS( "FMC" );
    case CONDUIT_TYPE::LFNC: return wxS( "LFNC" );
    }
    return wxS( "?" );
}


bool ConduitTypeFromString( const wxString& aText, CONDUIT_TYPE& aOut )
{
    wxString t = aText.Upper().Trim().Trim( false );
    if( t == wxT( "EMT" ) )  { aOut = CONDUIT_TYPE::EMT;  return true; }
    if( t == wxT( "PVC" ) )  { aOut = CONDUIT_TYPE::PVC;  return true; }
    if( t == wxT( "RGS" ) )  { aOut = CONDUIT_TYPE::RGS;  return true; }
    if( t == wxT( "IMC" ) )  { aOut = CONDUIT_TYPE::IMC;  return true; }
    if( t == wxT( "FMC" ) )  { aOut = CONDUIT_TYPE::FMC;  return true; }
    if( t == wxT( "LFNC" ) ) { aOut = CONDUIT_TYPE::LFNC; return true; }
    return false;
}


void CONDUIT::RemoveCable( CABLE* aCable )
{
    m_cables.erase( std::remove( m_cables.begin(), m_cables.end(), aCable ),
                    m_cables.end() );
}


std::vector<VECTOR2I> BuildFilletedPolyline( const std::vector<VECTOR2I>& aCenters,
                                             double aRadiusIu, bool* aFitsOk )
{
    if( aFitsOk )
        *aFitsOk = true;

    if( aCenters.size() < 3 || aRadiusIu <= 0.0 )
        return aCenters;

    constexpr double EPS = 1.0;
    const double     arcStep = 5.0 * M_PI / 180.0;   // ~5° per arc facet

    std::vector<VECTOR2I> out;
    out.push_back( aCenters.front() );

    for( size_t i = 1; i + 1 < aCenters.size(); ++i )
    {
        VECTOR2D A( aCenters[i - 1].x, aCenters[i - 1].y );
        VECTOR2D V( aCenters[i].x,     aCenters[i].y );
        VECTOR2D B( aCenters[i + 1].x, aCenters[i + 1].y );

        VECTOR2D va = A - V;
        VECTOR2D vb = B - V;
        double   la = va.EuclideanNorm();
        double   lb = vb.EuclideanNorm();

        if( la < EPS || lb < EPS )
            continue;                       // degenerate; drop this vertex

        VECTOR2D ua = va / la;
        VECTOR2D ub = vb / lb;

        double cosang = std::clamp( ua.x * ub.x + ua.y * ub.y, -1.0, 1.0 );
        double phi    = std::acos( cosang );   // interior angle between segments

        // Nearly straight: keep the vertex, no arc.
        if( phi > M_PI - 1e-3 )
        {
            out.push_back( aCenters[i] );
            continue;
        }

        double tanHalf = std::tan( phi / 2.0 );
        double d       = aRadiusIu / tanHalf;             // tangent dist from V
        double maxD    = 0.5 * std::min( la, lb );
        double rEff    = aRadiusIu;

        if( d > maxD )
        {
            // Not enough room for the full-radius fillet: leave the corner sharp and
            // flag it. (We never shrink the fillet below spec.)
            if( aFitsOk )
                *aFitsOk = false;
            out.push_back( aCenters[i] );
            continue;
        }

        VECTOR2D p1 = V + ua * d;        // tangent point toward A
        VECTOR2D p2 = V + ub * d;        // tangent point toward B

        VECTOR2D bis = ua + ub;
        double   bl  = bis.EuclideanNorm();

        if( bl < EPS )                   // 180° fold-back; can't fillet
        {
            out.push_back( aCenters[i] );
            continue;
        }

        bis = bis / bl;
        VECTOR2D center = V + bis * ( rEff / std::sin( phi / 2.0 ) );

        double a1    = std::atan2( p1.y - center.y, p1.x - center.x );
        double a2    = std::atan2( p2.y - center.y, p2.x - center.x );
        double sweep = a2 - a1;

        while( sweep >  M_PI ) sweep -= 2.0 * M_PI;
        while( sweep < -M_PI ) sweep += 2.0 * M_PI;

        int steps = std::max( 2, (int) std::ceil( std::abs( sweep ) / arcStep ) );

        out.emplace_back( KiROUND( p1.x ), KiROUND( p1.y ) );
        for( int k = 1; k < steps; ++k )
        {
            double a = a1 + sweep * ( (double) k / steps );
            out.emplace_back( KiROUND( center.x + rEff * std::cos( a ) ),
                              KiROUND( center.y + rEff * std::sin( a ) ) );
        }
        out.emplace_back( KiROUND( p2.x ), KiROUND( p2.y ) );
    }

    out.push_back( aCenters.back() );
    return out;
}


bool SolveRoutePreserveAngles( std::vector<wxPoint>& aPts, int aMovedIdx,
                               const wxPoint& aTarget, bool aFixedIsLast )
{
    const int n = (int) aPts.size();
    if( aMovedIdx < 0 || aMovedIdx >= n || n < 2 )
        return false;

    const int fixedIdx = aFixedIsLast ? n - 1 : 0;
    if( aMovedIdx == fixedIdx )
        return false;

    // Ordered list of segment indices between the moved node and the fixed end.
    // Each segment i runs pts[i] → pts[i+1] with a fixed unit direction; we solve
    // for new lengths so the moved node reaches the target with the fixed end pinned.
    std::vector<int> segs;
    if( aFixedIsLast )
        for( int i = aMovedIdx; i <= n - 2; ++i ) segs.push_back( i );
    else
        for( int i = 0; i <= aMovedIdx - 1; ++i ) segs.push_back( i );

    const int M = (int) segs.size();
    if( M == 0 )
        return false;

    std::vector<VECTOR2D> u( M );
    std::vector<double>   t0( M );
    for( int j = 0; j < M; ++j )
    {
        int      i = segs[j];
        VECTOR2D d( aPts[i + 1].x - aPts[i].x, aPts[i + 1].y - aPts[i].y );
        double   l = d.EuclideanNorm();
        if( l < 1.0 )
            return false;                 // degenerate segment, can't define angle
        u[j]  = VECTOR2D( d.x / l, d.y / l );
        t0[j] = l;
    }

    // Constraint: sum( t_j * u_j ) = b, where b points from the moving end to the
    // fixed end (the chain must still span that gap with fixed directions).
    VECTOR2D fixedPt( aPts[fixedIdx].x, aPts[fixedIdx].y );
    VECTOR2D movedTo( aTarget.x, aTarget.y );
    VECTOR2D b = aFixedIsLast ? ( fixedPt - movedTo ) : ( movedTo - fixedPt );

    // Minimise ||t - t0|| subject to A t = b  (A = [u_0 … u_{M-1}], 2×M).
    double axx = 0, axy = 0, ayy = 0;
    VECTOR2D At0( 0, 0 );
    for( int j = 0; j < M; ++j )
    {
        axx += u[j].x * u[j].x;
        axy += u[j].x * u[j].y;
        ayy += u[j].y * u[j].y;
        At0 += u[j] * t0[j];
    }
    VECTOR2D r = b - At0;
    double   det = axx * ayy - axy * axy;

    std::vector<double> t( M );

    if( std::abs( det ) > 1e-3 )
    {
        // t = t0 + Aᵀ (A Aᵀ)⁻¹ r
        double lx = (  ayy * r.x - axy * r.y ) / det;
        double ly = ( -axy * r.x + axx * r.y ) / det;
        for( int j = 0; j < M; ++j )
            t[j] = t0[j] + ( u[j].x * lx + u[j].y * ly );
    }
    else
    {
        // All directions parallel: only motion along that axis is achievable.
        VECTOR2D ref( 0, 0 );
        for( int j = 0; j < M; ++j ) if( u[j].EuclideanNorm() > 0.5 ) { ref = u[j]; break; }

        double perp  = r.x * ( -ref.y ) + r.y * ref.x;     // unreachable component
        double bnorm = b.EuclideanNorm();
        if( std::abs( perp ) > std::max( 1000.0, 0.002 * bnorm ) )
            return false;                                  // can't reach: error

        double along = r.x * ref.x + r.y * ref.y;
        for( int j = 0; j < M; ++j )
            t[j] = t0[j] + ( along / M ) * ( u[j].x * ref.x + u[j].y * ref.y );
    }

    // A negative length means the target is "behind" a segment — stretched too far.
    for( int j = 0; j < M; ++j )
    {
        if( t[j] < -1.0 )
            return false;                 // error → caller flags the conduit faulty
        if( t[j] < 0.0 )
            t[j] = 0.0;
    }

    // Reconstruct. Walk from the moved node to the fixed end along the fixed
    // directions with the solved lengths; rigidly translate the near side.
    std::vector<wxPoint> out = aPts;
    VECTOR2D             delta = movedTo - VECTOR2D( aPts[aMovedIdx].x, aPts[aMovedIdx].y );
    out[aMovedIdx] = aTarget;

    if( aFixedIsLast )
    {
        VECTOR2D cur = movedTo;
        for( int j = 0; j < M; ++j )
        {
            cur += u[j] * t[j];
            out[segs[j] + 1] = wxPoint( KiROUND( cur.x ), KiROUND( cur.y ) );
        }
        for( int i = 0; i < aMovedIdx; ++i )       // near side translates
            out[i] = wxPoint( KiROUND( aPts[i].x + delta.x ), KiROUND( aPts[i].y + delta.y ) );
    }
    else
    {
        VECTOR2D cur = fixedPt;
        for( int j = 0; j < M; ++j )
        {
            cur += u[j] * t[j];
            out[segs[j] + 1] = wxPoint( KiROUND( cur.x ), KiROUND( cur.y ) );
        }
        for( int i = aMovedIdx + 1; i < n; ++i )   // far side translates
            out[i] = wxPoint( KiROUND( aPts[i].x + delta.x ), KiROUND( aPts[i].y + delta.y ) );
    }

    aPts = std::move( out );
    return true;
}


bool EditRouteNode( std::vector<wxPoint>& aPts, int k, const wxPoint& aNewPos,
                    double aMaxBendDeg, double aSiteRotRad )
{
    const int n = (int) aPts.size();
    if( k < 0 || k >= n || n < 2 )
        return false;

    aPts[k] = aNewPos;

    const double gridStep = M_PI / 8.0;   // 22.5°

    auto localAngle = [&]( double dx, double dy ) { return std::atan2( dy, dx ) - aSiteRotRad; };

    auto angleDiff = [&]( double a, double b )
    {
        double d = a - b;
        while( d >  M_PI ) d -= 2.0 * M_PI;
        while( d < -M_PI ) d += 2.0 * M_PI;
        return std::abs( d );
    };

    auto deflectionDeg = [&]( double ax, double ay, double bx, double by )
    {
        double la = std::hypot( ax, ay ), lb = std::hypot( bx, by );
        if( la < 1.0 || lb < 1.0 )
            return 0.0;
        double c = std::clamp( ( ax * bx + ay * by ) / ( la * lb ), -1.0, 1.0 );
        return std::acos( c ) * 180.0 / M_PI;
    };

    bool ok = true;

    // Slide neighbour 'm' along the line through 'anchor' in its existing direction
    // (anchor→m) so segment (m→toward) lands on the grid with bend(at m) ≤ max.
    auto slideNeighbor = [&]( int m, int anchorIdx, int towardIdx ) -> bool
    {
        double Ax = aPts[anchorIdx].x, Ay = aPts[anchorIdx].y;
        double dx = aPts[m].x - Ax,    dy = aPts[m].y - Ay;
        double dl = std::hypot( dx, dy );
        if( dl < 1.0 )
            return false;
        dx /= dl; dy /= dl;

        double Kx = aPts[towardIdx].x, Ky = aPts[towardIdx].y;
        double wx = Kx - Ax,           wy = Ky - Ay;
        double curAng = localAngle( Kx - aPts[m].x, Ky - aPts[m].y );

        bool   found = false;
        double bestClose = 1e9, bestX = 0, bestY = 0;

        for( int i = 0; i < 16; ++i )
        {
            double g  = i * gridStep + aSiteRotRad;
            double vx = std::cos( g ), vy = std::sin( g );
            double det = dx * vy - dy * vx;
            if( std::abs( det ) < 1e-9 )
                continue;                           // line parallel to this grid dir

            double t = ( wx * vy - wy * vx ) / det;
            if( t < 1.0 )
                continue;                           // keep anchor→m direction (no flip)

            double mx = Ax + dx * t, my = Ay + dy * t;
            double sx = Kx - mx,     sy = Ky - my;
            if( std::hypot( sx, sy ) < 1.0 )
                continue;

            if( deflectionDeg( dx, dy, sx, sy ) > aMaxBendDeg + 1e-6 )
                continue;                           // bend at m too sharp

            double close = angleDiff( localAngle( sx, sy ), curAng );
            if( close < bestClose )
            {
                bestClose = close; bestX = mx; bestY = my; found = true;
            }
        }

        if( found )
            aPts[m] = wxPoint( KiROUND( bestX ), KiROUND( bestY ) );
        return found;
    };

    auto segOnGrid = [&]( int a, int b ) -> bool
    {
        double ang = localAngle( aPts[b].x - aPts[a].x, aPts[b].y - aPts[a].y );
        double snapped = std::round( ang / gridStep ) * gridStep;
        return angleDiff( ang, snapped ) < ( 0.5 * M_PI / 180.0 );   // within 0.5°
    };

    // Left neighbour
    if( k >= 1 )
    {
        int m = k - 1;
        if( m >= 1 )
            ok &= slideNeighbor( m, m - 1, k );
        else
            ok &= segOnGrid( 0, k );                // m is the start end node
    }

    // Right neighbour
    if( k <= n - 2 )
    {
        int m = k + 1;
        if( m <= n - 2 )
            ok &= slideNeighbor( m, m + 1, k );
        else
            ok &= segOnGrid( k, n - 1 );            // m is the last end node
    }

    // Edited node's own bend must also be within max.
    if( k >= 1 && k <= n - 2 )
    {
        double a1x = aPts[k].x - aPts[k - 1].x, a1y = aPts[k].y - aPts[k - 1].y;
        double a2x = aPts[k + 1].x - aPts[k].x, a2y = aPts[k + 1].y - aPts[k].y;
        if( deflectionDeg( a1x, a1y, a2x, a2y ) > aMaxBendDeg + 1e-6 )
            ok = false;
    }

    return ok;
}


void CONDUIT::recomputeHorizontalFromRoute()
{
    if( m_routePoints.size() < 2 )
        return;     // can't measure with fewer than 2 points

    std::vector<VECTOR2I> centers;
    centers.reserve( m_routePoints.size() );
    for( const wxPoint& p : m_routePoints )
        centers.emplace_back( p.x, p.y );

    std::vector<VECTOR2I> flat = BuildFilletedPolyline( centers, m_filletRadiusIu );

    double totalIu = 0.0;
    for( size_t i = 1; i < flat.size(); ++i )
    {
        double dx = flat[i].x - flat[i - 1].x;
        double dy = flat[i].y - flat[i - 1].y;
        totalIu += std::sqrt( dx * dx + dy * dy );
    }

    // KiCad IU = 1 nm; 1 mm = 1e6 IU; project convention treats mm as feet, so
    // 1 ft = 1,000,000 IU here.
    m_horizontalLengthFt = totalIu / 1000000.0;
}


double CONDUIT::GetTotalLengthFt( const std::map<int, double>& aLayerDepthsInches ) const
{
    double total = m_horizontalLengthFt;

    if( m_routeLayer >= 0 )
    {
        auto it = aLayerDepthsInches.find( m_routeLayer );
        if( it != aLayerDepthsInches.end() && it->second > 0.0 )
            total += 2.0 * ( it->second / 12.0 );   // surface → depth → surface
    }

    return total;
}


double CONDUIT::GetTotalBendAngleDeg( const std::map<int, double>& aLayerDepthsInches ) const
{
    double total = 0.0;

    // Sum the deflection at every interior route vertex.
    for( size_t i = 1; i + 1 < m_routePoints.size(); ++i )
    {
        double d1x = m_routePoints[i].x - m_routePoints[i - 1].x;
        double d1y = m_routePoints[i].y - m_routePoints[i - 1].y;
        double d2x = m_routePoints[i + 1].x - m_routePoints[i].x;
        double d2y = m_routePoints[i + 1].y - m_routePoints[i].y;

        double l1 = std::hypot( d1x, d1y ), l2 = std::hypot( d2x, d2y );
        if( l1 < 1.0 || l2 < 1.0 )
            continue;

        double c = std::clamp( ( d1x * d2x + d1y * d2y ) / ( l1 * l2 ), -1.0, 1.0 );
        total += std::acos( c ) * 180.0 / M_PI;     // deflection at this vertex
    }

    // Two 90° dives (depth → surface) at the ends, only if the layer has depth.
    if( m_routeLayer >= 0 )
    {
        auto it = aLayerDepthsInches.find( m_routeLayer );
        if( it != aLayerDepthsInches.end() && it->second > 0.0 )
            total += 180.0;
    }

    return total;
}


void CONDUIT::GetBendCounts( const std::map<int, double>& aLayerDepthsInches,
                             int& aN22, int& aN45, int& aN67, int& aN90 ) const
{
    aN22 = aN45 = aN67 = aN90 = 0;

    for( size_t i = 1; i + 1 < m_routePoints.size(); ++i )
    {
        double d1x = m_routePoints[i].x - m_routePoints[i - 1].x;
        double d1y = m_routePoints[i].y - m_routePoints[i - 1].y;
        double d2x = m_routePoints[i + 1].x - m_routePoints[i].x;
        double d2y = m_routePoints[i + 1].y - m_routePoints[i].y;

        double l1 = std::hypot( d1x, d1y ), l2 = std::hypot( d2x, d2y );
        if( l1 < 1.0 || l2 < 1.0 )
            continue;

        double c = std::clamp( ( d1x * d2x + d1y * d2y ) / ( l1 * l2 ), -1.0, 1.0 );
        double deg = std::acos( c ) * 180.0 / M_PI;

        // Bucket to the nearest 22.5° increment (0 = straight, ignored).
        int step = (int) std::lround( deg / 22.5 );
        switch( step )
        {
        case 1: aN22++; break;
        case 2: aN45++; break;
        case 3: aN67++; break;
        case 4: aN90++; break;
        default: break;     // 0 (straight) or >90 (shouldn't happen with snapping)
        }
    }

    // The two 90° depth dives count as 90° bends when the layer has a depth.
    if( m_routeLayer >= 0 )
    {
        auto it = aLayerDepthsInches.find( m_routeLayer );
        if( it != aLayerDepthsInches.end() && it->second > 0.0 )
            aN90 += 2;
    }
}


bool CONDUIT::HasAllCableSizesKnown() const
{
    if( m_cables.empty() )
        return true;     // an empty conduit isn't an "error" case
    for( const CABLE* c : m_cables )
        if( !c->IsAreaKnown() )
            return false;
    return true;
}


double CONDUIT::ComputeFillPercent() const
{
    if( m_diameterInches <= 0.0 )
        return 0.0;

    // Inner area in mm^2 — assumes nominal ID == OD for Phase 1 (correct enough for sizing).
    const double inchToMm = 25.4;
    double radiusMm = ( m_diameterInches * inchToMm ) / 2.0;
    double conduitAreaMm2 = M_PI * radiusMm * radiusMm;

    if( conduitAreaMm2 <= 0.0 )
        return 0.0;

    double cableAreaTotalMm2 = 0.0;
    for( const CABLE* cable : m_cables )
        cableAreaTotalMm2 += cable->GetAreaMm2();

    return ( cableAreaTotalMm2 / conduitAreaMm2 ) * 100.0;
}
