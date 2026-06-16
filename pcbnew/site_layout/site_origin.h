/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Site Layout — local <-> global (lat/lon) coordinate conversion.
 *
 * Convention:
 *  - Local +Y axis points at compass bearing m_rotationDeg (clockwise from True North).
 *  - When rotation is 0, local +Y = true North and local +X = true East.
 *  - Distance interpretation: KiCad displays distances in mm. The engineer mentally
 *    treats those mm values as feet. So we read board-mm as feet for the geo math.
 */

#ifndef SITE_ORIGIN_H
#define SITE_ORIGIN_H

#include <cmath>


struct SITE_ORIGIN
{
    double latDeg      = 0.0;  // origin latitude, decimal degrees
    double lonDeg      = 0.0;  // origin longitude, decimal degrees
    double rotationDeg = 0.0;  // bearing of local +Y axis, clockwise from True North

    bool IsConfigured() const
    {
        // Treat (0,0,0) as "not set". (0,0) is in the Gulf of Guinea, no real site
        // is going to legitimately use it as origin.
        return !( latDeg == 0.0 && lonDeg == 0.0 && rotationDeg == 0.0 );
    }
};


/// Result of converting a local point to global coords.
struct GLOBAL_POINT
{
    double latDeg;
    double lonDeg;
};


/// KiCad-IU-mm conversion. KiCad uses 1 IU = 1 nm internally; 1 mm = 1,000,000 IU.
constexpr double IU_PER_MM = 1000000.0;

/// Feet -> meters. We interpret board-mm as feet, per the project's unit convention.
constexpr double METERS_PER_FOOT = 0.3048;

/// Approximate meters per degree latitude (WGS-84 equatorial radius).
constexpr double METERS_PER_DEG_LAT = 111320.0;


/// Convert a board-local point (IU) to global lat/lon under the given origin.
inline GLOBAL_POINT LocalIuToGlobal( double aXIu, double aYIu, const SITE_ORIGIN& aOrigin )
{
    // 1. IU -> mm-as-feet -> meters
    double xMeters = ( aXIu / IU_PER_MM ) * METERS_PER_FOOT;
    double yMeters = ( aYIu / IU_PER_MM ) * METERS_PER_FOOT;

    // 2. Rotate (site +Y is at bearing rotationDeg clockwise from North)
    double theta = aOrigin.rotationDeg * M_PI / 180.0;
    double east  =  xMeters * std::cos( theta ) + yMeters * std::sin( theta );
    double north = -xMeters * std::sin( theta ) + yMeters * std::cos( theta );

    // 3. Meters -> degree offsets
    double dLat = north / METERS_PER_DEG_LAT;
    double dLon = east  / ( METERS_PER_DEG_LAT
                            * std::cos( aOrigin.latDeg * M_PI / 180.0 ) );

    return GLOBAL_POINT{ aOrigin.latDeg + dLat, aOrigin.lonDeg + dLon };
}

#endif // SITE_ORIGIN_H
