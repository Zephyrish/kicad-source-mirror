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
