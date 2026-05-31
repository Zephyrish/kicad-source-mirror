/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Conduit system — in-memory data model for Phase 1.
 * Serialization comes in Phase 2.
 */

#ifndef CONDUIT_DATA_H
#define CONDUIT_DATA_H

#include <map>
#include <vector>
#include <wx/gdicmn.h>      // wxPoint
#include <wx/string.h>

#include <kiid.h>


enum class CONDUIT_TYPE
{
    EMT,    // Electrical Metallic Tubing
    PVC,    // Polyvinyl Chloride
    RGS,    // Rigid Galvanized Steel
    IMC,    // Intermediate Metal Conduit
    FMC,    // Flexible Metal Conduit
    LFNC,   // Liquidtight Flexible Nonmetallic
};


wxString ConduitTypeToString( CONDUIT_TYPE aType );

/// Reverse lookup. Returns true if aText matched a known type (case-insensitive),
/// and writes the corresponding CONDUIT_TYPE to aOut. Returns false otherwise.
bool ConduitTypeFromString( const wxString& aText, CONDUIT_TYPE& aOut );


/**
 * A single cable / circuit. In Phase 1 this is purely an in-memory record.
 * In Phase 2 these will be backed by real nets from the electrical schematic.
 */
class CABLE
{
public:
    /// aNetCode: positive int = the BOARD's net code (stable within a session,
    /// allows surviving net renames). -1 = not linked to a board net.
    CABLE( const wxString& aName, int aNetCode = -1 ) :
        m_name( aName ),
        m_netCode( aNetCode ),
        m_areaMm2( 0.0 )
    {}

    const wxString& GetName() const { return m_name; }
    void SetName( const wxString& aName ) { m_name = aName; }

    int  GetNetCode() const { return m_netCode; }
    void SetNetCode( int aCode ) { m_netCode = aCode; }

    const wxString& GetSource() const { return m_source; }
    void SetSource( const wxString& aSource ) { m_source = aSource; }

    const wxString& GetDestination() const { return m_destination; }
    void SetDestination( const wxString& aDest ) { m_destination = aDest; }

    /// First component (alphabetical) connected to this net.
    /// Auto-derived from the board on sync.
    const wxString& GetFromRef() const { return m_fromRef; }
    void SetFromRef( const wxString& aRef ) { m_fromRef = aRef; }

    /// Last component (alphabetical) connected to this net.
    const wxString& GetToRef() const { return m_toRef; }
    void SetToRef( const wxString& aRef ) { m_toRef = aRef; }

    /// Cross-sectional area used for conduit-fill calculations (mm^2).
    double GetAreaMm2() const { return m_areaMm2; }
    void   SetAreaMm2( double aArea ) { m_areaMm2 = aArea; m_areaKnown = true; }

    /// True when a cable spec has been resolved and the area is meaningful.
    bool   IsAreaKnown() const { return m_areaKnown; }
    void   ClearAreaKnown()    { m_areaKnown = false; m_areaMm2 = 0.0; }

    /// Pad UUIDs that were on this net when last synced.
    /// This is the truly-stable identity: when a net is renamed, KiCad assigns a
    /// new net code, but the pads stay the same. We use pad-set overlap to re-find
    /// a renamed net.
    const std::vector<KIID>& GetPadIds() const { return m_padIds; }
    void SetPadIds( std::vector<KIID> aIds ) { m_padIds = std::move( aIds ); }

    bool IsOrphan() const { return m_orphan; }
    void SetOrphan( bool aOrphan ) { m_orphan = aOrphan; }

private:
    wxString          m_name;
    int               m_netCode;
    wxString          m_source;
    wxString          m_destination;
    wxString          m_fromRef;          // first component (alphabetical) on this net
    wxString          m_toRef;            // last component (alphabetical) on this net
    double            m_areaMm2;
    bool              m_areaKnown = false;
    std::vector<KIID> m_padIds;
    bool              m_orphan = false;   // true if last sync couldn't find the net
};


/**
 * A run of conduit that carries one or more cables.
 * Phase 1: in-memory only, no length/bend info yet (that's Phase 4).
 */
class CONDUIT
{
public:
    CONDUIT( const wxString& aName ) :
        m_name( aName ),
        m_type( CONDUIT_TYPE::EMT ),
        m_diameterInches( 2.0 ),
        m_maxFillPercent( 40.0 ),
        m_posX( 50 ),
        m_posY( 50 )
    {}

    const wxString& GetName() const { return m_name; }
    void SetName( const wxString& aName ) { m_name = aName; }

    CONDUIT_TYPE GetType() const { return m_type; }
    void SetType( CONDUIT_TYPE aType ) { m_type = aType; }

    double GetDiameterInches() const { return m_diameterInches; }
    void   SetDiameterInches( double aDiameter ) { m_diameterInches = aDiameter; }

    double GetMaxFillPercent() const { return m_maxFillPercent; }
    void   SetMaxFillPercent( double aPct ) { m_maxFillPercent = aPct; }

    /// Name of the project Conduit Spec this conduit references (empty if none).
    /// When set, the spec's clearance/bend rules are authoritative; material and
    /// diameter values are typically copied from the spec into m_type / m_diameterInches
    /// when the engineer picks a spec, but they remain individually editable.
    const wxString& GetSpecName() const { return m_specName; }
    void SetSpecName( const wxString& aName ) { m_specName = aName; }

    /// Canvas position (virtual coords).
    int  GetPosX() const { return m_posX; }
    int  GetPosY() const { return m_posY; }
    void SetPosition( int aX, int aY ) { m_posX = aX; m_posY = aY; }

    // ---- Routing (Phase 4.F.1: manual entry only; routing tool comes in 4.F.2) ----

    /// Site Layout copper layer this conduit runs on (PCB_LAYER_ID). -1 if not set.
    int  GetRouteLayer() const { return m_routeLayer; }
    void SetRouteLayer( int aLayer ) { m_routeLayer = aLayer; }

    /// Horizontal run length (feet) along the layer. If route points are set, this
    /// is derived from them; otherwise it's a manually-entered value. Does NOT
    /// include the depth dive (surface ↔ layer); that's added per-layer using
    /// LayerDepthsInches.
    double GetHorizontalLengthFt() const { return m_horizontalLengthFt; }
    void   SetHorizontalLengthFt( double aFt ) { m_horizontalLengthFt = aFt; }

    /// Polyline points (board IU) representing the conduit run on its layer.
    /// Phase 4.F.2.A: editable via the route-points dialog. Phase 4.F.2.B: click tool.
    const std::vector<wxPoint>& GetRoutePoints() const { return m_routePoints; }
    void SetRoutePoints( std::vector<wxPoint> aPoints )
    {
        m_routePoints = std::move( aPoints );
        recomputeHorizontalFromRoute();
    }
    void AddRoutePoint( const wxPoint& aPoint )
    {
        m_routePoints.push_back( aPoint );
        recomputeHorizontalFromRoute();
    }
    void ClearRoutePoints()
    {
        m_routePoints.clear();
        // Don't touch m_horizontalLengthFt — let the engineer keep manual entry.
    }

    /// Total length = horizontal + 2 × layer depth (so the cable can dive down and
    /// come back up). aLayerDepthsInches maps PCB_LAYER_ID → depth in inches.
    /// If the conduit's layer isn't in the map (or depth is 0), no dive is added.
    double GetTotalLengthFt( const std::map<int, double>& aLayerDepthsInches ) const;

    /// Frame caches the recently-computed total length here so the canvas can show
    /// it without needing project-side data.
    double GetCachedTotalLengthFt() const { return m_cachedTotalLengthFt; }
    void   SetCachedTotalLengthFt( double aFt ) { m_cachedTotalLengthFt = aFt; }

    const std::vector<CABLE*>& GetCables() const { return m_cables; }
    void AddCable( CABLE* aCable ) { m_cables.push_back( aCable ); }
    void RemoveCable( CABLE* aCable );

    /// Current fill % based on assigned cables. Returns 0 if diameter is 0.
    /// Caller should check HasAllCableSizesKnown() first — fill % is meaningless if
    /// any cable doesn't have a cable spec assigned yet.
    double ComputeFillPercent() const;

    /// True if every cable in the conduit has a known cross-sectional area.
    bool   HasAllCableSizesKnown() const;

private:
    wxString            m_name;
    wxString            m_specName;       ///< project Conduit Spec reference, or empty
    CONDUIT_TYPE        m_type;
    double              m_diameterInches;
    double              m_maxFillPercent;
    int                 m_posX;
    int                 m_posY;
    int                  m_routeLayer = -1;
    double               m_horizontalLengthFt = 0.0;
    double               m_cachedTotalLengthFt = 0.0;   // updated by frame each refresh
    std::vector<wxPoint> m_routePoints;                 // board IU
    std::vector<CABLE*>  m_cables;   // non-owning: cables live in the project model

    void recomputeHorizontalFromRoute();
};

#endif // CONDUIT_DATA_H
