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
#include <math/vector2d.h>


/**
 * Given a polyline of "virtual center" vertices and a bend radius (board IU),
 * return a flattened polyline in which each interior corner is replaced by a
 * circular fillet of that radius, tangent to both adjacent segments. The arc is
 * approximated by short line segments so callers can both draw it and measure
 * its length from the same data.
 *
 * If aRadiusIu <= 0, or fewer than 3 points are given, the input is returned
 * unchanged. If a corner doesn't have room for the full-radius fillet (the
 * required tangent length exceeds half the shorter adjacent segment), the corner
 * is left SHARP (no fillet — never silently shrunk) and, if aFitsOk is provided,
 * *aFitsOk is set false.
 */
std::vector<VECTOR2I> BuildFilletedPolyline( const std::vector<VECTOR2I>& aCenters,
                                             double aRadiusIu, bool* aFitsOk = nullptr );


/**
 * Move node aMovedIdx of a route to aTarget while keeping every segment's direction
 * fixed (snapped angles never change) and holding the FAR endpoint fixed (the last
 * point if aFixedIsLast, else the first). Only segment lengths change, distributed
 * by least-squares; nodes on the near side of the moved node translate rigidly.
 *
 * Returns true if a valid solution was found. Returns false (and leaves aPts
 * unchanged) when the target can't be reached without changing an angle or with a
 * non-negative segment length — i.e. the conduit was stretched too far; the caller
 * should flag it faulty.
 */
bool SolveRoutePreserveAngles( std::vector<wxPoint>& aPts, int aMovedIdx,
                               const wxPoint& aTarget, bool aFixedIsLast );


/**
 * Move node aIdx to aNewPos, then restore validity by sliding ONLY the immediate
 * neighbour on each side along its existing segment direction until the segment
 * toward the edited node lands on the 22.5° grid (local frame, aSiteRotRad) with a
 * bend within aMaxBendDeg. End nodes (first/last) never move — if an end blocks a
 * fix, that side is left invalid. Edits aPts in place.
 *
 * Returns true if every affected angle is valid; false → the route is in an error
 * state (an end couldn't move, no valid snap within max-bend, or the edited node's
 * own bend is too sharp). The caller should flag the conduit faulty.
 */
bool EditRouteNode( std::vector<wxPoint>& aPts, int aIdx, const wxPoint& aNewPos,
                    double aMaxBendDeg, double aSiteRotRad );


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

    /// Bend radius (board IU) used to fillet the route corners and to compute the
    /// filleted run length. Resolved from the conduit's spec by the frame. 0 =
    /// sharp corners (no fillet). Setting it recomputes the horizontal length.
    double GetFilletRadiusIu() const { return m_filletRadiusIu; }
    void   SetFilletRadiusIu( double aRadiusIu )
    {
        m_filletRadiusIu = aRadiusIu;
        recomputeHorizontalFromRoute();
    }

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

    /// Total cumulative bend angle (degrees) the cable is pulled through: the sum of
    /// deflection angles at every route vertex, PLUS 180° for the two 90° dives from
    /// the run depth up to the surface at each end (added only if the layer has a
    /// nonzero depth). Used for pull calcs / export.
    double GetTotalBendAngleDeg( const std::map<int, double>& aLayerDepthsInches ) const;

    double GetCachedTotalBendDeg() const { return m_cachedTotalBendDeg; }
    void   SetCachedTotalBendDeg( double aDeg ) { m_cachedTotalBendDeg = aDeg; }

    /// Count the route's bends by type (deflection bucketed to the nearest 22.5°
    /// increment). The two 90° depth dives are added to n90 when the layer has a
    /// nonzero depth (mirrors GetTotalBendAngleDeg).
    void GetBendCounts( const std::map<int, double>& aLayerDepthsInches,
                        int& aN22, int& aN45, int& aN67, int& aN90 ) const;

    /// Frame caches the bend-type counts so the (project-less) exporter can read them.
    int  GetCachedBend22() const { return m_cachedBend22; }
    int  GetCachedBend45() const { return m_cachedBend45; }
    int  GetCachedBend67() const { return m_cachedBend67; }
    int  GetCachedBend90() const { return m_cachedBend90; }
    void SetCachedBendCounts( int aN22, int aN45, int aN67, int aN90 )
    {
        m_cachedBend22 = aN22; m_cachedBend45 = aN45;
        m_cachedBend67 = aN67; m_cachedBend90 = aN90;
    }

    // ---- Equipment anchors (Phase 4.F.4) ----
    // An endpoint can be anchored to a footprint (by UUID). The stored offset is
    // (endpoint − footprint origin) captured at bind time; on a move the endpoint
    // tracks the footprint as (origin + offset). m_faulty is set when a follow
    // leaves the run unable to satisfy the spec (rendered dashed).
    bool HasStartAnchor() const { return m_hasStartAnchor; }
    bool HasEndAnchor()   const { return m_hasEndAnchor; }
    const KIID&    GetStartAnchor() const { return m_startAnchor; }
    const KIID&    GetEndAnchor()   const { return m_endAnchor; }
    const wxPoint& GetStartOffset() const { return m_startOffset; }
    const wxPoint& GetEndOffset()   const { return m_endOffset; }

    void SetStartAnchor( const KIID& aFp, const wxPoint& aOffset )
    { m_hasStartAnchor = true; m_startAnchor = aFp; m_startOffset = aOffset; }
    void SetEndAnchor( const KIID& aFp, const wxPoint& aOffset )
    { m_hasEndAnchor = true; m_endAnchor = aFp; m_endOffset = aOffset; }
    void ClearStartAnchor() { m_hasStartAnchor = false; }
    void ClearEndAnchor()   { m_hasEndAnchor = false; }

    bool IsFaulty() const { return m_faulty; }
    void SetFaulty( bool aFaulty ) { m_faulty = aFaulty; }

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
    bool                 m_hasStartAnchor = false;
    bool                 m_hasEndAnchor = false;
    KIID                 m_startAnchor;
    KIID                 m_endAnchor;
    wxPoint              m_startOffset;
    wxPoint              m_endOffset;
    bool                 m_faulty = false;
    double               m_filletRadiusIu = 0.0;
    double               m_horizontalLengthFt = 0.0;
    double               m_cachedTotalLengthFt = 0.0;   // updated by frame each refresh
    double               m_cachedTotalBendDeg = 0.0;     // updated by frame each refresh
    int                  m_cachedBend22 = 0;
    int                  m_cachedBend45 = 0;
    int                  m_cachedBend67 = 0;
    int                  m_cachedBend90 = 0;
    std::vector<wxPoint> m_routePoints;                 // board IU
    std::vector<CABLE*>  m_cables;   // non-owning: cables live in the project model

    void recomputeHorizontalFromRoute();
};

#endif // CONDUIT_DATA_H
