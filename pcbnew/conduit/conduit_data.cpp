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


void CONDUIT::RemoveCable( CABLE* aCable )
{
    m_cables.erase( std::remove( m_cables.begin(), m_cables.end(), aCable ),
                    m_cables.end() );
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
