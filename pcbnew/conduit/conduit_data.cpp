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


void CONDUIT::recomputeHorizontalFromRoute()
{
    if( m_routePoints.size() < 2 )
        return;     // can't measure with fewer than 2 points

    double totalIu = 0.0;
    for( size_t i = 1; i < m_routePoints.size(); ++i )
    {
        double dx = m_routePoints[i].x - m_routePoints[i - 1].x;
        double dy = m_routePoints[i].y - m_routePoints[i - 1].y;
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
