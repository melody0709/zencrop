#include "ToolbarIconRenderer.h"

#include <gdiplus.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "core/WideStringUtils.h"
#include "AppMessages.h"

namespace {

constexpr double Pi = 3.14159265358979323846;

struct SvgPathParser {
    explicit SvgPathParser(const std::string& source) : text(source) {}

    bool Parse(Gdiplus::GraphicsPath& path) {
        char command = 0;
        Gdiplus::PointF current(0.0f, 0.0f);
        Gdiplus::PointF start(0.0f, 0.0f);
        Gdiplus::PointF lastCubicControl(0.0f, 0.0f);
        Gdiplus::PointF lastQuadControl(0.0f, 0.0f);
        bool hasLastCubic = false;
        bool hasLastQuad = false;

        while (SkipSeparators()) {
            if (IsCommand(Peek())) {
                command = text[pos++];
            }
            if (!command) return false;

            bool relative = command >= 'a' && command <= 'z';
            char lower = (char)std::tolower((unsigned char)command);

            if (lower == 'z') {
                path.CloseFigure();
                current = start;
                hasLastCubic = false;
                hasLastQuad = false;
                command = 0;
                continue;
            }

            if (lower == 'm') {
                double x = 0.0, y = 0.0;
                if (!ReadNumber(x) || !ReadNumber(y)) return false;
                current = MakePoint(x, y, current, relative);
                path.StartFigure();
                path.AddLine(current, current);
                start = current;
                command = relative ? 'l' : 'L';
                hasLastCubic = false;
                hasLastQuad = false;
                continue;
            }

            if (lower == 'l') {
                double x = 0.0, y = 0.0;
                if (!ReadNumber(x) || !ReadNumber(y)) return false;
                Gdiplus::PointF next = MakePoint(x, y, current, relative);
                path.AddLine(current, next);
                current = next;
                hasLastCubic = false;
                hasLastQuad = false;
                continue;
            }

            if (lower == 'h') {
                double x = 0.0;
                if (!ReadNumber(x)) return false;
                Gdiplus::PointF next(relative ? current.X + (float)x : (float)x, current.Y);
                path.AddLine(current, next);
                current = next;
                hasLastCubic = false;
                hasLastQuad = false;
                continue;
            }

            if (lower == 'v') {
                double y = 0.0;
                if (!ReadNumber(y)) return false;
                Gdiplus::PointF next(current.X, relative ? current.Y + (float)y : (float)y);
                path.AddLine(current, next);
                current = next;
                hasLastCubic = false;
                hasLastQuad = false;
                continue;
            }

            if (lower == 'c') {
                double x1 = 0.0, y1 = 0.0, x2 = 0.0, y2 = 0.0, x = 0.0, y = 0.0;
                if (!ReadNumber(x1) || !ReadNumber(y1) || !ReadNumber(x2) || !ReadNumber(y2) ||
                    !ReadNumber(x) || !ReadNumber(y)) return false;
                Gdiplus::PointF c1 = MakePoint(x1, y1, current, relative);
                Gdiplus::PointF c2 = MakePoint(x2, y2, current, relative);
                Gdiplus::PointF next = MakePoint(x, y, current, relative);
                path.AddBezier(current, c1, c2, next);
                current = next;
                lastCubicControl = c2;
                hasLastCubic = true;
                hasLastQuad = false;
                continue;
            }

            if (lower == 's') {
                double x2 = 0.0, y2 = 0.0, x = 0.0, y = 0.0;
                if (!ReadNumber(x2) || !ReadNumber(y2) || !ReadNumber(x) || !ReadNumber(y)) return false;
                Gdiplus::PointF c1 = hasLastCubic
                    ? Gdiplus::PointF(2.0f * current.X - lastCubicControl.X, 2.0f * current.Y - lastCubicControl.Y)
                    : current;
                Gdiplus::PointF c2 = MakePoint(x2, y2, current, relative);
                Gdiplus::PointF next = MakePoint(x, y, current, relative);
                path.AddBezier(current, c1, c2, next);
                current = next;
                lastCubicControl = c2;
                hasLastCubic = true;
                hasLastQuad = false;
                continue;
            }

            if (lower == 'q') {
                double x1 = 0.0, y1 = 0.0, x = 0.0, y = 0.0;
                if (!ReadNumber(x1) || !ReadNumber(y1) || !ReadNumber(x) || !ReadNumber(y)) return false;
                Gdiplus::PointF q = MakePoint(x1, y1, current, relative);
                Gdiplus::PointF next = MakePoint(x, y, current, relative);
                AddQuadratic(path, current, q, next);
                current = next;
                lastQuadControl = q;
                hasLastQuad = true;
                hasLastCubic = false;
                continue;
            }

            if (lower == 't') {
                double x = 0.0, y = 0.0;
                if (!ReadNumber(x) || !ReadNumber(y)) return false;
                Gdiplus::PointF q = hasLastQuad
                    ? Gdiplus::PointF(2.0f * current.X - lastQuadControl.X, 2.0f * current.Y - lastQuadControl.Y)
                    : current;
                Gdiplus::PointF next = MakePoint(x, y, current, relative);
                AddQuadratic(path, current, q, next);
                current = next;
                lastQuadControl = q;
                hasLastQuad = true;
                hasLastCubic = false;
                continue;
            }

            if (lower == 'a') {
                double rx = 0.0, ry = 0.0, angle = 0.0, largeArc = 0.0, sweep = 0.0, x = 0.0, y = 0.0;
                if (!ReadNumber(rx) || !ReadNumber(ry) || !ReadNumber(angle) || !ReadNumber(largeArc) ||
                    !ReadNumber(sweep) || !ReadNumber(x) || !ReadNumber(y)) return false;
                Gdiplus::PointF next = MakePoint(x, y, current, relative);
                AddArc(path, current, next, std::abs(rx), std::abs(ry), angle, largeArc != 0.0, sweep != 0.0);
                current = next;
                hasLastCubic = false;
                hasLastQuad = false;
                continue;
            }

            return false;
        }

        return true;
    }

    bool SkipSeparators() {
        while (pos < text.size()) {
            char c = text[pos];
            if (c == ',' || c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                ++pos;
            } else {
                break;
            }
        }
        return pos < text.size();
    }

    char Peek() const {
        return pos < text.size() ? text[pos] : '\0';
    }

    static bool IsCommand(char c) {
        switch (c) {
        case 'M': case 'm': case 'L': case 'l': case 'H': case 'h': case 'V': case 'v':
        case 'C': case 'c': case 'S': case 's': case 'Q': case 'q': case 'T': case 't':
        case 'A': case 'a': case 'Z': case 'z':
            return true;
        default:
            return false;
        }
    }

    bool ReadNumber(double& value) {
        SkipSeparators();
        if (pos >= text.size()) return false;
        const char* begin = text.c_str() + pos;
        char* end = nullptr;
        value = std::strtod(begin, &end);
        if (end == begin) return false;
        pos = (size_t)(end - text.c_str());
        return true;
    }

    static Gdiplus::PointF MakePoint(double x, double y, const Gdiplus::PointF& current, bool relative) {
        return relative
            ? Gdiplus::PointF(current.X + (float)x, current.Y + (float)y)
            : Gdiplus::PointF((float)x, (float)y);
    }

    static void AddQuadratic(Gdiplus::GraphicsPath& path, const Gdiplus::PointF& p0,
        const Gdiplus::PointF& q, const Gdiplus::PointF& p1) {
        Gdiplus::PointF c1(p0.X + (q.X - p0.X) * 2.0f / 3.0f, p0.Y + (q.Y - p0.Y) * 2.0f / 3.0f);
        Gdiplus::PointF c2(p1.X + (q.X - p1.X) * 2.0f / 3.0f, p1.Y + (q.Y - p1.Y) * 2.0f / 3.0f);
        path.AddBezier(p0, c1, c2, p1);
    }

    static double VectorAngle(double ux, double uy, double vx, double vy) {
        double sign = (ux * vy - uy * vx) < 0.0 ? -1.0 : 1.0;
        double dot = ux * vx + uy * vy;
        double len = std::sqrt((ux * ux + uy * uy) * (vx * vx + vy * vy));
        if (len <= 0.0) return 0.0;
        double v = std::clamp(dot / len, -1.0, 1.0);
        return sign * std::acos(v);
    }

    static void AddArc(Gdiplus::GraphicsPath& path, const Gdiplus::PointF& p0, const Gdiplus::PointF& p1,
        double rx, double ry, double angleDeg, bool largeArc, bool sweep) {
        if (rx <= 0.0 || ry <= 0.0 || (p0.X == p1.X && p0.Y == p1.Y)) {
            path.AddLine(p0, p1);
            return;
        }

        double phi = angleDeg * Pi / 180.0;
        double cosPhi = std::cos(phi);
        double sinPhi = std::sin(phi);
        double dx = ((double)p0.X - (double)p1.X) / 2.0;
        double dy = ((double)p0.Y - (double)p1.Y) / 2.0;
        double x1p = cosPhi * dx + sinPhi * dy;
        double y1p = -sinPhi * dx + cosPhi * dy;

        double lambda = (x1p * x1p) / (rx * rx) + (y1p * y1p) / (ry * ry);
        if (lambda > 1.0) {
            double s = std::sqrt(lambda);
            rx *= s;
            ry *= s;
        }

        double rx2 = rx * rx;
        double ry2 = ry * ry;
        double x1p2 = x1p * x1p;
        double y1p2 = y1p * y1p;
        double sign = largeArc == sweep ? -1.0 : 1.0;
        double coefDen = rx2 * y1p2 + ry2 * x1p2;
        double coef = coefDen <= 0.0 ? 0.0 : sign * std::sqrt((std::max)(0.0, (rx2 * ry2 - rx2 * y1p2 - ry2 * x1p2) / coefDen));
        double cxp = coef * (rx * y1p / ry);
        double cyp = coef * (-ry * x1p / rx);
        double cx = cosPhi * cxp - sinPhi * cyp + ((double)p0.X + (double)p1.X) / 2.0;
        double cy = sinPhi * cxp + cosPhi * cyp + ((double)p0.Y + (double)p1.Y) / 2.0;

        double theta1 = VectorAngle(1.0, 0.0, (x1p - cxp) / rx, (y1p - cyp) / ry);
        double delta = VectorAngle((x1p - cxp) / rx, (y1p - cyp) / ry, (-x1p - cxp) / rx, (-y1p - cyp) / ry);
        if (!sweep && delta > 0.0) delta -= 2.0 * Pi;
        if (sweep && delta < 0.0) delta += 2.0 * Pi;

        int segments = (std::max)(1, (int)std::ceil(std::abs(delta) / (Pi / 2.0)));
        double step = delta / segments;
        Gdiplus::PointF current = p0;

        for (int i = 0; i < segments; ++i) {
            double t1 = theta1 + step * i;
            double t2 = t1 + step;
            double alpha = 4.0 / 3.0 * std::tan((t2 - t1) / 4.0);
            double x1 = std::cos(t1);
            double y1 = std::sin(t1);
            double x2 = std::cos(t2);
            double y2 = std::sin(t2);

            auto map = [&](double x, double y) {
                return Gdiplus::PointF(
                    (float)(cx + rx * (cosPhi * x - sinPhi * y)),
                    (float)(cy + ry * (sinPhi * x + cosPhi * y)));
            };

            Gdiplus::PointF c1 = map(x1 - alpha * y1, y1 + alpha * x1);
            Gdiplus::PointF c2 = map(x2 + alpha * y2, y2 - alpha * x2);
            Gdiplus::PointF end = (i == segments - 1) ? p1 : map(x2, y2);
            path.AddBezier(current, c1, c2, end);
            current = end;
        }
    }

    const std::string& text;
    size_t pos = 0;
};

std::unordered_map<unsigned int, std::string> LoadPathTable() {
    std::unordered_map<unsigned int, std::string> table;
    // Stage4 4-A: production PATH_TABLE only — exeDir\PATH_TABLE.tsv.
    // docs/reverse search deleted (Gate: 生产不搜 docs/reverse).
    std::vector<std::filesystem::path> candidates;

    std::wstring exeDirWide;
    wchar_t modulePath[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, modulePath, MAX_PATH) > 0) {
        exeDirWide = WideExeDirFromModulePath(modulePath);
    }
    if (!exeDirWide.empty()) {
        std::filesystem::path exeDir = exeDirWide;
        candidates.push_back(exeDir / L"PATH_TABLE.tsv");
    }
    // CWD production fallback only (build.bat stages PATH_TABLE.tsv next to exe).
    candidates.push_back(L"PATH_TABLE.tsv");

    std::ifstream file;
    for (const auto& candidate : candidates) {
        file.open(candidate, std::ios::binary);
        if (file.is_open()) break;
    }
    if (!file.is_open()) {
        OutputDebugStringW(L"[ToolbarIconRenderer] WARNING: PATH_TABLE.tsv not found in production paths\n");
        return table;
    }

    std::string line;
    std::getline(file, line);
    while (std::getline(file, line)) {
        size_t tab = line.find('\t');
        if (tab == std::string::npos) continue;
        unsigned long code = std::strtoul(line.substr(0, tab).c_str(), nullptr, 16);
        if (code == 0) continue;
        table[(unsigned int)code] = line.substr(tab + 1);
    }
    // OWN-114: pure PATH_TABLE load count debug label (WideStringUtils).
    OutputDebugStringW(WideFormatPathTableLoadedCount(static_cast<int>(table.size())).c_str());
    return table;
}

const std::string* FindPath(unsigned int codepoint) {
    static const std::unordered_map<unsigned int, std::string> table = LoadPathTable();
    auto it = table.find(codepoint);
    return it == table.end() ? nullptr : &it->second;
}

bool DrawPath(HDC hdc, const std::string& svgPath, const RECT& rect, COLORREF color,
    float viewWidth, float viewHeight) {
    if (!hdc || rect.right <= rect.left || rect.bottom <= rect.top) return false;

    Gdiplus::GraphicsPath path;
    SvgPathParser parser(svgPath);
    if (!parser.Parse(path)) return false;

    float width = (float)(rect.right - rect.left);
    float height = (float)(rect.bottom - rect.top);
    float scale = (std::min)(width / viewWidth, height / viewHeight);
    float drawW = viewWidth * scale;
    float drawH = viewHeight * scale;
    float dx = (float)rect.left + (width - drawW) * 0.5f;
    float dy = (float)rect.top + (height - drawH) * 0.5f;

    Gdiplus::Matrix matrix;
    matrix.Translate(dx, dy);
    matrix.Scale(scale, scale);
    path.Transform(&matrix);

    Gdiplus::Graphics graphics(hdc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::SolidBrush brush(Gdiplus::Color(255, WideUnpackR(static_cast<unsigned int>(color)), WideUnpackG(static_cast<unsigned int>(color)), WideUnpackB(static_cast<unsigned int>(color))));
    return graphics.FillPath(&brush, &path) == Gdiplus::Ok;
}

}

namespace Screenshot {

bool DrawToolbarIcon(HDC hdc, unsigned int codepoint, const RECT& rect, COLORREF color) {
    const std::string* path = FindPath(codepoint);
    if (!path) {
        static std::mutex loggedMutex;
        static std::unordered_set<unsigned int> logged;
        std::lock_guard<std::mutex> lock(loggedMutex);
        if (logged.insert(codepoint).second) {
            // OWN-114: pure missing-codepoint debug label (WideStringUtils).
            OutputDebugStringW(WideFormatMissingCodepoint(codepoint).c_str());
        }
        return false;
    }
    return DrawPath(hdc, *path, rect, color, 1024.0f, 1024.0f);
}

bool DrawDropdownArrow(HDC hdc, const RECT& rect, COLORREF color) {
    static const std::string arrowPath =
        "M7 15.0006L2.75732 10.758L4.17154 9.34375L7 12.1722L9.8284 9.34375L11.2426 10.758L7 15.0006Z";
    return DrawPath(hdc, arrowPath, rect, color, 14.0f, 24.0f);
}

}
