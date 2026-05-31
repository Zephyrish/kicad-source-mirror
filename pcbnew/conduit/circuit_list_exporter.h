/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Conduit system — Circuit List exporter.
 *
 * Writes a CSV file with one row per (cable, conduit) pair. The columns mirror the
 * Circuit List format used by the construction crew. Some fields (Type/layer,
 * Circuit Length) are placeholders that will be filled once the Site Layout pipeline
 * lands — they're emitted as empty cells today.
 */

#ifndef CIRCUIT_LIST_EXPORTER_H
#define CIRCUIT_LIST_EXPORTER_H

#include <memory>
#include <vector>

#include <wx/string.h>

class BOARD;
class CONDUIT;
class PROJECT_FILE;


class CIRCUIT_LIST_EXPORTER
{
public:
    /// Write a Circuit List CSV. Returns true on success.
    static bool ExportCsv( const wxString& aFilePath,
                           const std::vector<std::unique_ptr<CONDUIT>>& aConduits,
                           PROJECT_FILE& aProjectFile,
                           BOARD* aBoard );
};


class RACEWAY_LIST_EXPORTER
{
public:
    /// Write a Raceway List CSV — one row per conduit. Returns true on success.
    static bool ExportCsv( const wxString& aFilePath,
                           const std::vector<std::unique_ptr<CONDUIT>>& aConduits );
};

#endif // CIRCUIT_LIST_EXPORTER_H
