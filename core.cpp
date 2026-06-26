// ═══════════════════════════════════════════════════════════════════════
//  core.cpp — see core.h for the interface
// ═══════════════════════════════════════════════════════════════════════

#include "core.h"

// Definition of the extern declared in core.h. Initialized to 1.0
// (96 dpi); wWinMain rewrites this from GetDpiForMonitor before any
// layout code runs.
double g_dpiScale = 1.0;

// Definition of the extern declared in core.h. wWinMain sets this
// after CreateWindow returns; background threads check for non-null
// before PostMessage.
HWND g_hwMain = nullptr;

wstring AppDir() {
    wchar_t buf[MAX_PATH] = {};
    GetModuleFileName(nullptr, buf, MAX_PATH);
    wstring p(buf);
    size_t s = p.rfind(L'\\');
    return s != wstring::npos ? p.substr(0, s) : p;
}

wstring ReadTextFile(const wstring& path) {
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return L"";
    fseek(f, 0, SEEK_END); long sz = ftell(f); rewind(f);
    string raw(sz, '\0');
    fread(&raw[0], 1, sz, f); fclose(f);
    if (sz >= 3 && (unsigned char)raw[0] == 0xEF
                && (unsigned char)raw[1] == 0xBB
                && (unsigned char)raw[2] == 0xBF) raw = raw.substr(3);
    int len = MultiByteToWideChar(CP_UTF8, 0, raw.c_str(), (int)raw.size(), nullptr, 0);
    wstring out(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, raw.c_str(), (int)raw.size(), &out[0], len);
    return out;
}

void WriteTextFile(const wstring& path, const wstring& content) {
    FILE* f = _wfopen(path.c_str(), L"wb");
    if (!f) return;
    int len = WideCharToMultiByte(CP_UTF8, 0, content.c_str(), -1, nullptr, 0, nullptr, nullptr);
    string bytes(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, content.c_str(), -1, &bytes[0], len, nullptr, nullptr);
    if (len > 0) fwrite(bytes.c_str(), 1, len - 1, f);
    fclose(f);
}

wstring Utf8ToWide(const string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

wstring EscapeJson(const wstring& s) {
    wstring out;
    for (wchar_t c : s) {
        if      (c == L'\\') out += L"\\\\";
        else if (c == L'"')  out += L"\\\"";
        else                 out += c;
    }
    return out;
}

// JSON string extractor with escape-decoding (round-trip stable)
wstring JsonStr(const wstring& j, const wstring& key) {
    wstring k = L"\"" + key + L"\"";
    size_t p = j.find(k);
    if (p == wstring::npos) return L"";
    p = j.find(L':', p + k.size());
    if (p == wstring::npos) return L"";
    p = j.find(L'"', p);
    if (p == wstring::npos) return L"";
    size_t s = p + 1, e = s;
    while (e < j.size() && !(j[e] == L'"' && j[e - 1] != L'\\')) ++e;

    wstring raw = j.substr(s, e - s);
    wstring out; out.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == L'\\' && i + 1 < raw.size()) {
            switch (raw[i + 1]) {
                case L'\\': out += L'\\'; ++i; break;
                case L'"':  out += L'"';  ++i; break;
                case L'/':  out += L'/';  ++i; break;
                case L'n':  out += L'\n'; ++i; break;
                case L't':  out += L'\t'; ++i; break;
                case L'r':  out += L'\r'; ++i; break;
                default:    out += raw[i]; break;
            }
        } else {
            out += raw[i];
        }
    }
    return out;
}

bool JsonBool(const wstring& j, const wstring& key, bool def) {
    wstring k = L"\"" + key + L"\"";
    size_t p = j.find(k);
    if (p == wstring::npos) return def;
    p = j.find(L':', p);
    if (p == wstring::npos) return def;
    while (p < j.size() && (j[p] == L':' || j[p] == L' ' || j[p] == L'\t')) ++p;
    if (p + 4 <= j.size() && j.compare(p, 4, L"true")  == 0) return true;
    if (p + 5 <= j.size() && j.compare(p, 5, L"false") == 0) return false;
    return def;
}

int JsonInt(const wstring& j, const wstring& key, int def) {
    wstring k = L"\"" + key + L"\"";
    size_t p = j.find(k);
    if (p == wstring::npos) return def;
    p = j.find(L':', p);
    if (p == wstring::npos) return def;
    while (p < j.size() && (j[p] == L':' || j[p] == L' ' || j[p] == L'\t')) ++p;
    if (p >= j.size()) return def;
    bool neg = (j[p] == L'-');
    if (neg) ++p;
    if (p >= j.size() || !iswdigit(j[p])) return def;
    int val = 0;
    while (p < j.size() && iswdigit(j[p])) {
        val = val * 10 + (j[p] - L'0');
        ++p;
    }
    return neg ? -val : val;
}

double JsonDouble(const wstring& j, const wstring& key, double def) {
    wstring k = L"\"" + key + L"\"";
    size_t p = j.find(k);
    if (p == wstring::npos) return def;
    p = j.find(L':', p);
    if (p == wstring::npos) return def;
    while (p < j.size() && (j[p] == L':' || j[p] == L' ' || j[p] == L'\t')) ++p;
    if (p >= j.size()) return def;
    bool neg = (j[p] == L'-');
    if (neg) ++p;
    if (p >= j.size() || (!iswdigit(j[p]) && j[p] != L'.')) return def;
    double val = 0.0;
    while (p < j.size() && iswdigit(j[p])) {
        val = val * 10.0 + (double)(j[p] - L'0');
        ++p;
    }
    if (p < j.size() && j[p] == L'.') {
        ++p;
        double frac = 0.1;
        while (p < j.size() && iswdigit(j[p])) {
            val += frac * (double)(j[p] - L'0');
            frac *= 0.1;
            ++p;
        }
    }
    return neg ? -val : val;
}
