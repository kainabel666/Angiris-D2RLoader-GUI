// ═══════════════════════════════════════════════════════════════════════
//  version.cpp — see version.h for the interface
// ═══════════════════════════════════════════════════════════════════════

#include "version.h"

wstring NormalizeVersion(const wstring& v) {
    wstring s = v;
    if (!s.empty() && (s[0] == L'v' || s[0] == L'V')) s = s.substr(1);
    // Trim whitespace
    while (!s.empty() && iswspace(s.front())) s.erase(s.begin());
    while (!s.empty() && iswspace(s.back()))  s.pop_back();
    return s;
}

int CompareVersions(const wstring& a, const wstring& b) {
    wstring sa = NormalizeVersion(a);
    wstring sb = NormalizeVersion(b);
    size_t i = 0, j = 0;
    while (i < sa.size() || j < sb.size()) {
        // Parse numeric segment from each
        long long na = 0, nb = 0;
        bool hasA = (i < sa.size() && iswdigit(sa[i]));
        bool hasB = (j < sb.size() && iswdigit(sb[j]));
        while (i < sa.size() && iswdigit(sa[i])) { na = na * 10 + (sa[i] - L'0'); ++i; }
        while (j < sb.size() && iswdigit(sb[j])) { nb = nb * 10 + (sb[j] - L'0'); ++j; }
        if (hasA || hasB) {
            if (na != nb) return (na < nb) ? -1 : 1;
        }
        // Now compare any trailing non-digit chars up to next digit or end
        wstring tailA, tailB;
        while (i < sa.size() && !iswdigit(sa[i])) { tailA += sa[i]; ++i; }
        while (j < sb.size() && !iswdigit(sb[j])) { tailB += sb[j]; ++j; }
        // Strip a single dot separator for fair compare
        if (!tailA.empty() && tailA[0] == L'.') tailA.erase(0, 1);
        if (!tailB.empty() && tailB[0] == L'.') tailB.erase(0, 1);
        if (tailA != tailB) return (tailA < tailB) ? -1 : 1;
    }
    return 0;
}
