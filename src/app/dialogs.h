// ReleaseForge - finestre di dialogo modali costruite in memoria (DLGTEMPLATEEX),
// ridimensionate automaticamente da Windows in base ai DPI del monitor.
#pragma once
#include "../win/winutil.h"

#include <functional>
#include <string>
#include <vector>

#include "../win/pipeline.h"
#include "../win/settings.h"

namespace rf {

class DlgBuilder {
public:
    // Unità di dialogo (DLU). Font Segoe UI 9.
    DlgBuilder(const std::wstring& title, short cx, short cy, DWORD extraStyle = 0);
    void Static(int id, const std::wstring& text, short x, short y, short cx, short cy, DWORD style = 0);
    void Edit(int id, short x, short y, short cx, short cy, DWORD style = 0);
    void Button(int id, const std::wstring& text, short x, short y, short cx, short cy, DWORD style = 0);
    void Check(int id, const std::wstring& text, short x, short y, short cx, short cy);
    void Combo(int id, short x, short y, short cx, short cy);
    void List(int id, short x, short y, short cx, short cy);
    void Group(const std::wstring& text, short x, short y, short cx, short cy);
    // Esegue il dialogo; proc riceve i messaggi (WM_INITDIALOG con lParam=0). Restituisce EndDialog.
    INT_PTR Run(HWND owner, std::function<INT_PTR(HWND, UINT, WPARAM, LPARAM)> proc);

private:
    void Item(DWORD style, DWORD exStyle, short x, short y, short cx, short cy, int id, WORD atom, const std::wstring& text);
    void Align();
    void Word(WORD w) { buf_.push_back(w); }
    void DWord(DWORD d) { buf_.push_back(LOWORD(d)); buf_.push_back(HIWORD(d)); }
    void Str(const std::wstring& s) { for (wchar_t c : s) buf_.push_back(WORD(c)); buf_.push_back(0); }
    std::vector<WORD> buf_;
    size_t countPos_ = 0;
    WORD count_ = 0;
};

std::wstring GetDlgText(HWND dlg, int id);

// Scelta del risultato TMDB (lista + ID/URL manuale).
TmdbChoice ShowTmdbChoice(HWND owner, const std::vector<TmdbHit>& hits, const std::string& searched);

// Richiesta di un testo breve. Restituisce false se annullato.
bool ShowInput(HWND owner, const std::wstring& title, const std::wstring& prompt, std::wstring& value);

// Impostazioni e strumenti. action: 0 = nessuna, 1 = scarica FFmpeg, 2 = installa MediaInfo completo.
bool ShowSettings(HWND owner, Settings& s, int& action);

// Liste di tracker remote: URL (salvati in tracker_lists.txt), aggiornamento automatico, limite.
bool ShowTrackerLists(HWND owner, Settings& s);

// File/cartelle con IFileDialog.
bool PickFile(HWND owner, const std::wstring& title, const std::vector<std::pair<std::wstring, std::wstring>>& filters, std::wstring& path);
bool PickFolder(HWND owner, const std::wstring& title, std::wstring& path);

}  // namespace rf
