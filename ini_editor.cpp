// ═══════════════════════════════════════════════════════════════════════
//  ini_editor.cpp — see ini_editor.h for the interface
// ═══════════════════════════════════════════════════════════════════════

#include "ini_editor.h"
#include "core.h"   // ReadTextFile, WriteTextFile

void TrimWs(wstring& s) {
    while (!s.empty() && (s.front() == L' ' || s.front() == L'\t')) s.erase(s.begin());
    while (!s.empty() && (s.back()  == L' ' || s.back()  == L'\t')) s.pop_back();
}

bool ParseIniLine(const wstring& line, const wstring& key, wstring& value) {
    wstring s = line;
    // Strip a trailing comment marker, but only if it isn't inside the value
    // — INI comments are conventionally at line start (';') so this is rare.
    TrimWs(s);
    if (s.empty() || s[0] == L';' || s[0] == L'[') return false;
    size_t eq = s.find(L'=');
    if (eq == wstring::npos) return false;
    wstring k = s.substr(0, eq);
    TrimWs(k);
    if (k != key) return false;
    value = s.substr(eq + 1);
    TrimWs(value);
    return true;
}

int IniGetInt(const wstring& path, const wstring& section,
              const wstring& key, int def) {
    wstring contents = ReadTextFile(path);
    if (contents.empty()) return def;

    wstring wantSection = L"[" + section + L"]";
    wstring currentSection;
    size_t lineStart = 0;
    while (lineStart <= contents.size()) {
        size_t lineEnd = contents.find(L'\n', lineStart);
        wstring line = (lineEnd == wstring::npos)
                       ? contents.substr(lineStart)
                       : contents.substr(lineStart, lineEnd - lineStart);
        // Strip trailing \r if file has CRLF endings
        if (!line.empty() && line.back() == L'\r') line.pop_back();

        wstring trimmed = line; TrimWs(trimmed);
        if (!trimmed.empty() && trimmed[0] == L'[') {
            currentSection = trimmed;
        } else if (currentSection == wantSection) {
            wstring val;
            if (ParseIniLine(line, key, val)) {
                int n = 0;
                bool any = false;
                size_t p = 0;
                while (p < val.size() && iswdigit(val[p])) {
                    n = n * 10 + (val[p] - L'0');
                    ++p; any = true;
                }
                return any ? n : def;
            }
        }

        if (lineEnd == wstring::npos) break;
        lineStart = lineEnd + 1;
    }
    return def;
}

void IniSetInt(const wstring& path, const wstring& section,
               const wstring& key, int newValue) {
    wstring contents = ReadTextFile(path);
    wstring wantSection = L"[" + section + L"]";
    wchar_t valBuf[32];
    swprintf(valBuf, 32, L"%d", newValue);
    wstring valStr = valBuf;

    // Fresh file — write minimal stub
    if (contents.empty()) {
        wstring out = wantSection + L"\r\n" + key + L" = " + valStr + L"\r\n";
        WriteTextFile(path, out);
        return;
    }

    // Split into lines (preserve EOL style — detect CRLF vs LF)
    bool isCrlf = contents.find(L"\r\n") != wstring::npos;
    wstring eol = isCrlf ? L"\r\n" : L"\n";

    vector<wstring> lines;
    size_t p = 0;
    while (p <= contents.size()) {
        size_t e = contents.find(L'\n', p);
        if (e == wstring::npos) { lines.push_back(contents.substr(p)); break; }
        wstring ln = contents.substr(p, e - p);
        if (!ln.empty() && ln.back() == L'\r') ln.pop_back();
        lines.push_back(ln);
        p = e + 1;
    }

    // Find/replace within the right section, or note where to insert
    wstring currentSection;
    int sectionStart = -1;      // first line index inside our section
    int sectionEnd   = -1;      // line index of next section (or end)
    bool replaced = false;
    for (size_t i = 0; i < lines.size(); ++i) {
        wstring trimmed = lines[i]; TrimWs(trimmed);
        if (!trimmed.empty() && trimmed[0] == L'[') {
            if (currentSection == wantSection && sectionEnd < 0) {
                sectionEnd = (int)i;
            }
            currentSection = trimmed;
            if (currentSection == wantSection) sectionStart = (int)i + 1;
        } else if (currentSection == wantSection) {
            wstring oldVal;
            if (ParseIniLine(lines[i], key, oldVal)) {
                // Replace the value while keeping any leading whitespace
                // and any inline indentation around `=`.
                size_t eq = lines[i].find(L'=');
                wstring lhs = lines[i].substr(0, eq + 1);
                // Strip trailing comment on the line — preserve only key = val
                lines[i] = lhs + L" " + valStr;
                replaced = true;
                break;
            }
        }
    }

    if (!replaced) {
        if (sectionStart < 0) {
            // Section doesn't exist — append new section at end
            if (!lines.empty() && !lines.back().empty()) lines.push_back(L"");
            lines.push_back(wantSection);
            lines.push_back(key + L" = " + valStr);
        } else {
            // Section exists, key doesn't — insert at end of section
            int insertAt = (sectionEnd >= 0) ? sectionEnd : (int)lines.size();
            lines.insert(lines.begin() + insertAt, key + L" = " + valStr);
        }
    }

    // Rebuild
    wstring out;
    for (size_t i = 0; i < lines.size(); ++i) {
        out += lines[i];
        if (i + 1 < lines.size()) out += eol;
    }
    WriteTextFile(path, out);
}
