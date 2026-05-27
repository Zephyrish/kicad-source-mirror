/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Conduit system — in-memory data model for Phase 1.
 * Serialization comes in Phase 2.
 */

#ifndef CONDUIT_DATA_H
#define CONDUIT_DATA_H

#include <vector>
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

    /// Cross-sectional area used for conduit-fill calculations (mm^2).
    double GetAreaMm2() const { return m_areaMm2; }
    void   SetAreaMm2( double aArea ) { m_areaMm2 = aArea; }

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
    double            m_areaMm2;
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
        m_maxFillPercent( 40.0 )
    {}

    const wxString& GetName() const { return m_name; }
    void SetName( const wxString& aName ) { m_name = aName; }

    CONDUIT_TYPE GetType() const { return m_type; }
    void SetType( CONDUIT_TYPE aType ) { m_type = aType; }

    double GetDiameterInches() const { return m_diameterInches; }
    void   SetDiameterInches( double aDiameter ) { m_diameterInches = aDiameter; }

    double GetMaxFillPercent() const { return m_maxFillPercent; }
    void   SetMaxFillPercent( double aPct ) { m_maxFillPercent = aPct; }

    const std::vector<CABLE*>& GetCables() const { return m_cables; }
    void AddCable( CABLE* aCable ) { m_cables.push_back( aCable ); }
    void RemoveCable( CABLE* aCable );

    /// Current fill % based on assigned cables. Returns 0 if diameter is 0.
    double ComputeFillPercent() const;

private:
    wxString            m_name;
    CONDUIT_TYPE        m_type;
    double              m_diameterInches;
    double              m_maxFillPercent;
    std::vector<CABLE*> m_cables;   // non-owning: cables live in the project model
};

#endif // CONDUIT_DATA_H
