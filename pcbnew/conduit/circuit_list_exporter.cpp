/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Conduit system — Circuit List CSV writer.
 */

#include "circuit_list_exporter.h"

#include <fstream>

#include "conduit_data.h"

#include <board.h>
#include <project.h>
#include <project/project_file.h>
#include <project/net_settings.h>
#include <netclass.h>


// ---- CSV helpers ----------------------------------------------------------

static std::string csvEscape( const wxString& aValue )
{
    std::string s( aValue.utf8_str() );
    bool needsQuotes = ( s.find( ',' )  != std::string::npos
                       || s.find( '"' )  != std::string::npos
                       || s.find( '\n' ) != std::string::npos
                       || s.find( '\r' ) != std::string::npos );
    if( !needsQuotes )
        return s;

    std::string out = "\"";
    for( char c : s )
    {
        if( c == '"' ) out += "\"\"";   // double-quote inner quotes (RFC 4180)
        else           out += c;
    }
    out += "\"";
    return out;
}


static std::string formatSize( double aValue, PROJECT_FILE::CABLE_SIZE_UNIT aUnit )
{
    if( aValue == 0.0 )
        return "";

    const char* unit = ( aUnit == PROJECT_FILE::CABLE_SIZE_UNIT::AWG ) ? "AWG" : "KCMIL";
    char buf[64];
    snprintf( buf, sizeof( buf ), "%g %s", aValue, unit );
    return buf;
}


// ---- Spec lookup ----------------------------------------------------------

static bool resolveSpec( const CABLE* aCable,
                         PROJECT_FILE& aProj,
                         const wxString& aDefaultClassName,
                         const std::map<wxString, wxString>& aNetToClass,
                         PROJECT_FILE::CABLE_SPEC& aOut )
{
    // 1. Per-perspective override
    wxString key = aCable->GetName() + wxT( "||" ) + aCable->GetFromRef();
    auto it = aProj.m_CableSpecsByNet.find( key );
    if( it != aProj.m_CableSpecsByNet.end() )
    {
        aOut = it->second;
        return true;
    }

    // 2. Class-level spec
    auto cit = aNetToClass.find( aCable->GetName() );
    wxString className = ( cit != aNetToClass.end() ) ? cit->second : aDefaultClassName;
    auto sit = aProj.m_CableSpecs.find( className );
    if( sit != aProj.m_CableSpecs.end() )
    {
        aOut = sit->second;
        return true;
    }

    return false;
}


// ---- Main export ----------------------------------------------------------

bool CIRCUIT_LIST_EXPORTER::ExportCsv(
        const wxString& aFilePath,
        const std::vector<std::unique_ptr<CONDUIT>>& aConduits,
        PROJECT_FILE& aProjectFile,
        BOARD* aBoard )
{
    std::ofstream ofs( aFilePath.fn_str() );
    if( !ofs.is_open() )
        return false;

    // Header row — matches the construction-crew template the engineer specified.
    ofs << "Circuit #,"
        << "Type,"
        << "From Equip,"
        << "To Equip,"
        << "Primary QTY,"
        << "Primary CND,"
        << "Primary SIZE,"
        << "Primary TYPE,"
        << "Secondary QTY,"
        << "Secondary CND,"
        << "Secondary SIZE,"
        << "Secondary TYPE,"
        << "TOTAL CABLE LENGTH (FT),"
        << "CIRCUIT LENGTH (FT),"
        << "Supplier,"
        << "Part Number,"
        << "NOTES"
        << "\n";

    // Build net name -> class name lookup once.
    std::map<wxString, wxString> netToClass;
    wxString defaultClassName = wxT( "Default" );

    auto netSettings = aProjectFile.NetSettings();
    if( netSettings )
    {
        if( auto def = netSettings->GetDefaultNetclass() )
        {
            wxString nm = def->GetName();
            if( !nm.IsEmpty() )
                defaultClassName = nm;
        }

        const auto& assignments = netSettings->GetNetclassLabelAssignments();
        for( const auto& [netName, classes] : assignments )
        {
            if( !classes.empty() )
                netToClass[ netName ] = *classes.begin();
        }
    }

    // One row per (cable, conduit) pairing.
    for( const std::unique_ptr<CONDUIT>& conduit : aConduits )
    {
        for( const CABLE* cable : conduit->GetCables() )
        {
            PROJECT_FILE::CABLE_SPEC spec;
            bool hasSpec = resolveSpec( cable, aProjectFile, defaultClassName,
                                        netToClass, spec );

            // Build each cell. Empty cells for fields that need Site Layout data.
            ofs << csvEscape( cable->GetName() ) << ",";          // Circuit #

            // Type = layer label (depth stratum) when set
            wxString layerLabel;
            if( aBoard && conduit->GetRouteLayer() >= 0 )
            {
                layerLabel = aBoard->GetLayerName(
                        static_cast<PCB_LAYER_ID>( conduit->GetRouteLayer() ) );
            }
            ofs << csvEscape( layerLabel ) << ",";

            ofs << csvEscape( cable->GetFromRef() ) << ",";        // From Equip
            ofs << csvEscape( cable->GetToRef() ) << ",";          // To Equip

            if( hasSpec )
            {
                ofs << spec.primary_qty << ",";
                ofs << spec.primary_conductors << ",";
                ofs << csvEscape( formatSize( spec.primary_size_value,
                                              spec.primary_size_unit ) ) << ",";
                ofs << csvEscape( spec.insulation_type ) << ",";

                ofs << spec.secondary_qty << ",";
                ofs << spec.secondary_conductors << ",";
                ofs << csvEscape( formatSize( spec.secondary_size_value,
                                              spec.secondary_size_unit ) ) << ",";
                // Secondary TYPE — falls back to primary insulation_type for now;
                // can be split out later if your supplier sheets differentiate.
                ofs << csvEscape( spec.insulation_type ) << ",";
            }
            else
            {
                // Empty cells for spec fields when nothing is assigned.
                ofs << ",,,,,,,,";   // 8 empty cells (4 primary + 4 secondary)
            }

            // Length data (computed by frame; 0 means not set)
            double circuitLen = conduit->GetCachedTotalLengthFt();
            int totalConductors = 0;
            if( hasSpec )
            {
                totalConductors = spec.primary_qty * spec.primary_conductors
                                + spec.secondary_qty * spec.secondary_conductors;
            }

            // TOTAL CABLE LENGTH (FT) = circuit length × conductor count
            if( circuitLen > 0.0 && totalConductors > 0 )
            {
                char buf[64];
                snprintf( buf, sizeof( buf ), "%.2f", circuitLen * totalConductors );
                ofs << buf;
            }
            ofs << ",";

            // CIRCUIT LENGTH (FT)
            if( circuitLen > 0.0 )
            {
                char buf[64];
                snprintf( buf, sizeof( buf ), "%.2f", circuitLen );
                ofs << buf;
            }
            ofs << ",";

            // Supplier
            ofs << ( hasSpec ? csvEscape( spec.supplier ) : std::string() ) << ",";
            // Part Number
            ofs << ( hasSpec ? csvEscape( spec.part_number ) : std::string() ) << ",";
            // NOTES — engineer fills in
            ofs << "";

            ofs << "\n";
        }
    }

    ofs.close();
    return ofs.good();
}


// ===========================================================================
// Raceway List
// ===========================================================================

bool RACEWAY_LIST_EXPORTER::ExportCsv(
        const wxString& aFilePath,
        const std::vector<std::unique_ptr<CONDUIT>>& aConduits )
{
    std::ofstream ofs( aFilePath.fn_str() );
    if( !ofs.is_open() )
        return false;

    ofs << "RACEWAY #,"
        << "Spec,"
        << "CONDUIT SIZE,"
        << "Material,"
        << "RACEWAY LENGTH (FT),"
        << "Circuit #'s inside"
        << "\n";

    for( const std::unique_ptr<CONDUIT>& conduit : aConduits )
    {
        ofs << csvEscape( conduit->GetName() ) << ",";
        ofs << csvEscape( conduit->GetSpecName() ) << ",";

        // CONDUIT SIZE — diameter with quote suffix matching the rest of the UI
        char sizeBuf[32];
        snprintf( sizeBuf, sizeof( sizeBuf ), "%.2f\"", conduit->GetDiameterInches() );
        ofs << csvEscape( wxString::FromUTF8( sizeBuf ) ) << ",";

        // Material — EMT, PVC, etc.
        ofs << csvEscape( ConduitTypeToString( conduit->GetType() ) ) << ",";

        // RACEWAY LENGTH (FT)
        {
            double len = conduit->GetCachedTotalLengthFt();
            if( len > 0.0 )
            {
                char buf[64];
                snprintf( buf, sizeof( buf ), "%.2f", len );
                ofs << buf;
            }
        }
        ofs << ",";

        // Circuit #'s inside — comma-joined cable names (quoted so the embedded commas
        // don't break the CSV row).
        wxString joined;
        for( size_t i = 0; i < conduit->GetCables().size(); ++i )
        {
            if( i > 0 ) joined += wxT( ", " );
            joined += conduit->GetCables()[ i ]->GetName();
        }
        ofs << csvEscape( joined );

        ofs << "\n";
    }

    ofs.close();
    return ofs.good();
}
