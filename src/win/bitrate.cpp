#include "bitrate.h"
#include "winutil.h"  // prima delle intestazioni Win32 specifiche

#include <objidl.h>
// GDI+ richiede min/max nello scope globale quando NOMINMAX è attivo.
#include <algorithm>
namespace Gdiplus {
using std::max;
using std::min;
}  // namespace Gdiplus
#include <gdiplus.h>

#include <cmath>
#include <stdexcept>
#include <vector>

#include "../core/strutil.h"
#include "process.h"
#include "winutil.h"

namespace rf {

namespace {

std::vector<double> GaussianSmooth(const std::vector<double>& in, double sigma) {
    // Equivalente a scipy.ndimage.gaussian_filter1d (mode="reflect", truncate=4.0).
    int radius = int(4.0 * sigma + 0.5);
    std::vector<double> kernel(size_t(2 * radius + 1));
    double sum = 0;
    for (int i = -radius; i <= radius; ++i) sum += kernel[size_t(i + radius)] = std::exp(-0.5 * i * i / (sigma * sigma));
    for (auto& k : kernel) k /= sum;
    const int n = int(in.size());
    std::vector<double> out(in.size());
    auto at = [&](int i) {
        if (n == 1) return in[0];
        int period = 2 * n;
        i %= period;
        if (i < 0) i += period;
        if (i >= n) i = period - 1 - i;
        return in[size_t(i)];
    };
    for (int i = 0; i < n; ++i) {
        double acc = 0;
        for (int k = -radius; k <= radius; ++k) acc += kernel[size_t(k + radius)] * at(i + k);
        out[size_t(i)] = acc;
    }
    return out;
}

bool PngEncoder(CLSID* clsid) {
    UINT num = 0, size = 0;
    Gdiplus::GetImageEncodersSize(&num, &size);
    if (!size) return false;
    std::vector<char> buf(size);
    auto* info = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buf.data());
    Gdiplus::GetImageEncoders(num, size, info);
    for (UINT i = 0; i < num; ++i) {
        if (wcscmp(info[i].MimeType, L"image/png") == 0) { *clsid = info[i].Clsid; return true; }
    }
    return false;
}

std::wstring FormatThousands(long long v) {
    std::wstring s = std::to_wstring(v), out;
    int c = 0;
    for (auto it = s.rbegin(); it != s.rend(); ++it) {
        if (c && c % 3 == 0) out.insert(out.begin(), L'.');
        out.insert(out.begin(), *it);
        ++c;
    }
    return out;
}

std::wstring FormatTime(long long sec) {
    wchar_t b[32];
    swprintf(b, 32, L"%lld:%02lld:%02lld", sec / 3600, (sec / 60) % 60, sec % 60);
    return b;
}

}  // namespace

void CreateBitrateGraph(const std::wstring& ffprobe, const std::wstring& video, const std::wstring& outputPng,
                        const std::atomic<bool>* cancel, const std::function<void(double)>& progress) {
    if (ffprobe.empty()) throw std::runtime_error("ffprobe.exe non trovato (incluso nella distribuzione FFmpeg)");
    std::vector<double> bins;  // kbit per secondo
    std::string pending;
    auto consume = [&](const std::string& line) {
        // pts_time=12.345|dts_time=12.300|size=4567
        double t = -1;
        long long size = -1;
        for (auto& field : Split(line, '|')) {
            size_t eq = field.find('=');
            if (eq == std::string::npos) continue;
            std::string k = field.substr(0, eq), v = field.substr(eq + 1);
            if (v == "N/A") continue;
            if (k == "pts_time" || (k == "dts_time" && t < 0)) t = std::atof(v.c_str());
            else if (k == "size") size = std::atoll(v.c_str());
        }
        if (t < 0 || size < 0 || t > 360000) return;
        size_t idx = size_t(t);
        if (idx >= bins.size()) {
            bins.resize(idx + 1, 0.0);
            if (progress && idx % 60 == 0) progress(double(idx));
        }
        bins[idx] += size * 8.0 / 1000.0;
    };
    ProcessOptions o;
    o.cancel = cancel;
    o.lowPriority = true;
    o.onStdout = [&](const std::string& chunk) {
        pending += chunk;
        size_t nl;
        while ((nl = pending.find('\n')) != std::string::npos) {
            consume(Trim(pending.substr(0, nl)));
            pending.erase(0, nl + 1);
        }
    };
    ProcessResult r = RunProcess(ffprobe, {L"-v", L"error", L"-fflags", L"+genpts", L"-select_streams", L"v:0", L"-show_entries",
                                           L"packet=pts_time,dts_time,size", L"-of", L"compact=p=0:nk=0", video}, o);
    if (!pending.empty()) consume(Trim(pending));
    if (!r.started) throw std::runtime_error("ffprobe non avviabile: " + r.startError);
    if (bins.size() < 2) throw std::runtime_error("ffprobe non ha restituito pacchetti video" + (r.err.empty() ? "" : ": " + Trim(AnsiToUtf8(r.err))));
    if (bins.size() > 2) bins.pop_back();  // ultimo secondo parziale
    std::vector<double> smooth = GaussianSmooth(bins, 64.0);

    ULONG_PTR token = 0;
    Gdiplus::GdiplusStartupInput in;
    if (Gdiplus::GdiplusStartup(&token, &in, nullptr) != Gdiplus::Ok) throw std::runtime_error("GDI+ non disponibile");
    {
        using namespace Gdiplus;
        const int W = 1600, H = 900;
        const float L = 110, R = 30, T = 60, B = 80;
        Bitmap bmp(W, H, PixelFormat24bppRGB);
        Graphics g(&bmp);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
        g.Clear(Color(255, 255, 255, 255));
        FontFamily ff(L"Segoe UI");
        Font font(&ff, 15, FontStyleRegular, UnitPixel), title(&ff, 20, FontStyleRegular, UnitPixel);
        SolidBrush text(Color(255, 40, 40, 40));
        double maxV = 0;
        for (double v : bins) maxV = std::max(maxV, v);
        // scala Y "arrotondata"
        double stepY = std::pow(10.0, std::floor(std::log10(std::max(maxV, 1.0) / 5)));
        for (double m : {1.0, 2.0, 2.5, 5.0, 10.0}) if (maxV / (stepY * m) <= 8) { stepY *= m; break; }
        double topY = std::ceil(maxV / stepY) * stepY;
        if (topY <= 0) topY = 1;
        const double n = double(bins.size() - 1);
        auto X = [&](double i) { return REAL(L + (W - L - R) * (n > 0 ? i / n : 0)); };
        auto Y = [&](double v) { return REAL(H - B - (H - T - B) * (v / topY)); };
        Pen grid(Color(255, 224, 224, 224), 1);
        StringFormat right, center;
        right.SetAlignment(StringAlignmentFar);
        right.SetLineAlignment(StringAlignmentCenter);
        center.SetAlignment(StringAlignmentCenter);
        for (double v = 0; v <= topY + 1e-9; v += stepY) {
            g.DrawLine(&grid, REAL(L), Y(v), REAL(W - R), Y(v));
            std::wstring label = FormatThousands((long long)v);
            g.DrawString(label.c_str(), -1, &font, RectF(0, Y(v) - 10, L - 10, 20), &right, &text);
        }
        long long total = (long long)bins.size() - 1;
        long long step = std::max<long long>(total / 9, 1);
        for (long long s = 0; s <= total; s += step) {
            std::wstring label = FormatTime(s);
            g.DrawString(label.c_str(), -1, &font, RectF(X(double(s)) - 60, REAL(H - B + 8), 120, 20), &center, &text);
            g.DrawLine(&grid, X(double(s)), REAL(H - B), X(double(s)), REAL(H - B + 5));
        }
        std::vector<PointF> raw, sm;
        raw.reserve(bins.size());
        for (size_t i = 0; i < bins.size(); ++i) {
            raw.emplace_back(X(double(i)), Y(bins[i]));
            sm.emplace_back(X(double(i)), Y(std::max(0.0, smooth[i])));
        }
        Pen gray(Color(255, 0x9e, 0x9e, 0x9e), 1), red(Color(255, 0xf4, 0x43, 0x36), 2.5f);
        g.DrawLines(&gray, raw.data(), INT(raw.size()));
        g.DrawLines(&red, sm.data(), INT(sm.size()));
        Pen axis(Color(255, 120, 120, 120), 1);
        g.DrawRectangle(&axis, REAL(L), REAL(T), REAL(W - L - R), REAL(H - T - B));
        std::wstring name = PathFileName(video);
        g.DrawString(name.c_str(), -1, &title, RectF(L, 15, W - L - R, 30), &center, &text);
        g.DrawString(L"Tempo", -1, &font, RectF(L, REAL(H - 40), W - L - R, 20), &center, &text);
        // legenda
        SolidBrush legendBg(Color(235, 255, 255, 255));
        g.FillRectangle(&legendBg, REAL(W - R - 230), REAL(T + 10), 220.f, 56.f);
        g.DrawLine(&gray, REAL(W - R - 220), REAL(T + 28), REAL(W - R - 190), REAL(T + 28));
        g.DrawLine(&red, REAL(W - R - 220), REAL(T + 52), REAL(W - R - 190), REAL(T + 52));
        g.DrawString(L"Bitrate", -1, &font, PointF(REAL(W - R - 180), REAL(T + 18)), &text);
        g.DrawString(L"Bitrate (smussato)", -1, &font, PointF(REAL(W - R - 180), REAL(T + 42)), &text);
        // etichetta asse Y ruotata
        GraphicsState st = g.Save();
        g.TranslateTransform(22, REAL(T + (H - T - B) / 2));
        g.RotateTransform(-90);
        g.DrawString(L"Bitrate (Kbps)", -1, &font, RectF(-150, -10, 300, 20), &center, &text);
        g.Restore(st);
        CLSID png;
        Status saved = PngEncoder(&png) ? bmp.Save(outputPng.c_str(), &png, nullptr) : GenericError;
        if (saved != Ok) {
            Gdiplus::GdiplusShutdown(token);
            throw std::runtime_error("salvataggio PNG fallito");
        }
    }
    Gdiplus::GdiplusShutdown(token);
}

}  // namespace rf
