#include "jinja.h"

#include <algorithm>
#include <cctype>
#include <functional>

#include "strutil.h"

namespace rf {

// ---------------------------------------------------------------- TValue

bool TValue::Truthy() const {
    switch (kind_) {
        case Kind::Undefined:
        case Kind::None: return false;
        case Kind::Bool:
        case Kind::Int: return int_ != 0;
        case Kind::Str: return !str_->empty();
        case Kind::List: return !list_->empty();
        case Kind::Map: return !map_->empty();
    }
    return false;
}

static std::string PyQuote(const std::string& s) {
    bool hasSingle = s.find('\'') != std::string::npos, hasDouble = s.find('"') != std::string::npos;
    char q = (hasSingle && !hasDouble) ? '"' : '\'';
    std::string out(1, q);
    for (char c : s) {
        if (c == q || c == '\\') { out += '\\'; out += c; }
        else if (c == '\n') out += "\\n";
        else if (c == '\t') out += "\\t";
        else if (c == '\r') out += "\\r";
        else out += c;
    }
    return out + q;
}

std::string TValue::Repr() const {
    switch (kind_) {
        case Kind::Str: return PyQuote(*str_);
        case Kind::List: {
            std::string out = "[";
            for (size_t i = 0; i < list_->size(); ++i) {
                if (i) out += ", ";
                out += (*list_)[i].Repr();
            }
            return out + "]";
        }
        case Kind::Map: {
            std::string out = "{";
            bool first = true;
            for (auto& kv : *map_) {
                if (!first) out += ", ";
                first = false;
                out += PyQuote(kv.first) + ": " + kv.second.Repr();
            }
            return out + "}";
        }
        default: return ToString();
    }
}

std::string TValue::ToString() const {
    switch (kind_) {
        case Kind::Undefined: return "";
        case Kind::None: return "None";
        case Kind::Bool: return int_ ? "True" : "False";
        case Kind::Int: return std::to_string(int_);
        case Kind::Str: return *str_;
        default: return Repr();
    }
}

long long TValue::AsInt() const {
    if (kind_ == Kind::Int || kind_ == Kind::Bool) return int_;
    if (kind_ == Kind::Str) {
        try { return std::stoll(Trim(*str_)); } catch (...) { return 0; }
    }
    return 0;
}

const std::string& TValue::StrRef() const {
    static const std::string empty;
    return kind_ == Kind::Str ? *str_ : empty;
}
const TValue::List& TValue::ListRef() const {
    static const List empty;
    return kind_ == Kind::List ? *list_ : empty;
}
const TValue::Map& TValue::MapRef() const {
    static const Map empty;
    return kind_ == Kind::Map ? *map_ : empty;
}

TValue TValue::Attr(const std::string& name) const {
    if (kind_ == Kind::Map) {
        auto it = map_->find(name);
        if (it != map_->end()) return it->second;
    }
    return TValue();
}

TValue TValue::Index(const TValue& key) const {
    if (kind_ == Kind::Map) return Attr(key.ToString());
    if (kind_ == Kind::List || kind_ == Kind::Str) {
        long long n = (long long)Length();
        long long i = key.AsInt();
        if (i < 0) i += n;
        if (i < 0 || i >= n) return TValue();
        if (kind_ == Kind::List) return (*list_)[size_t(i)];
        return TValue(std::string(1, (*str_)[size_t(i)]));
    }
    return TValue();
}

size_t TValue::Length() const {
    switch (kind_) {
        case Kind::Str: return str_->size();
        case Kind::List: return list_->size();
        case Kind::Map: return map_->size();
        default: return 0;
    }
}

// ---------------------------------------------------------------- Lexer

namespace {

enum class TokKind { Text, Output, Stmt };
struct Token {
    TokKind kind;
    std::string text;
    int line;
};

int CountLines(const std::string& s, size_t from, size_t to) {
    int n = 0;
    for (size_t i = from; i < to && i < s.size(); ++i) if (s[i] == '\n') ++n;
    return n;
}

bool IsWs(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }

std::string RStripAll(const std::string& s) {
    size_t e = s.size();
    while (e > 0 && IsWs(s[e - 1])) --e;
    return s.substr(0, e);
}

// Trova la chiusura del tag ignorando delimitatori dentro stringhe.
size_t FindClose(const std::string& src, size_t from, const std::string& close, bool quoteAware) {
    char quote = 0;
    for (size_t i = from; i + close.size() <= src.size(); ++i) {
        char c = src[i];
        if (quoteAware) {
            if (quote) {
                if (c == '\\') { ++i; continue; }
                if (c == quote) quote = 0;
                continue;
            }
            if (c == '\'' || c == '"') { quote = c; continue; }
        }
        if (src.compare(i, close.size(), close) == 0) return i;
    }
    return std::string::npos;
}

std::vector<Token> Lex(std::string src, const TemplateOptions& opt) {
    src = NormalizeNewlines(src);
    if (!opt.keep_trailing_newline && !src.empty() && src.back() == '\n') src.pop_back();

    std::vector<Token> out;
    size_t pos = 0;
    int line = 1;
    bool stripNextLeading = false;

    auto emitText = [&](std::string text) {
        if (stripNextLeading) {
            size_t b = 0;
            while (b < text.size() && IsWs(text[b])) ++b;
            text = text.substr(b);
            stripNextLeading = false;
        }
        if (!text.empty()) out.push_back({TokKind::Text, text, line});
    };

    while (pos <= src.size()) {
        size_t next = std::string::npos;
        for (const char* open : {"{{", "{%", "{#"}) {
            size_t f = src.find(open, pos);
            if (f < next) next = f;
        }
        if (next == std::string::npos) {
            std::string text = src.substr(pos);
            emitText(text);
            break;
        }
        std::string text = src.substr(pos, next - pos);
        char kindChar = src[next + 1];
        size_t inner = next + 2;
        char modifier = inner < src.size() ? src[inner] : 0;
        bool block = kindChar == '%' || kindChar == '#';
        bool atLineStart = (pos == 0 || src[pos - 1] == '\n');

        // Il testo precedente diventa definitivo solo dopo aver applicato strip/lstrip.
        bool pendingLeading = stripNextLeading;
        if (pendingLeading) {
            size_t b = 0;
            while (b < text.size() && IsWs(text[b])) ++b;
            text = text.substr(b);
            stripNextLeading = false;
        }
        if (modifier == '-') {
            text = RStripAll(text);
            ++inner;
        } else if (modifier == '+') {
            ++inner;
        } else if (block && opt.lstrip_blocks) {
            size_t nl = text.rfind('\n');
            size_t lpos = nl == std::string::npos ? 0 : nl + 1;
            if (nl != std::string::npos || (atLineStart && !pendingLeading) || (pendingLeading && text.empty() && atLineStart)) {
                bool onlyWs = true;
                for (size_t i = lpos; i < text.size(); ++i) if (text[i] != ' ' && text[i] != '\t') { onlyWs = false; break; }
                if (onlyWs) text.erase(lpos);
            }
        }
        if (!text.empty()) out.push_back({TokKind::Text, text, line});
        line += CountLines(src, pos, next);

        std::string close = kindChar == '{' ? "}}" : (kindChar == '%' ? "%}" : "#}");
        size_t end = FindClose(src, inner, close, kindChar != '#');
        if (end == std::string::npos) throw TemplateError("tag non chiuso (manca '" + close + "')", line);
        std::string body = src.substr(inner, end - inner);
        int tagLine = line;
        line += CountLines(src, next, end + close.size());
        size_t after = end + close.size();

        bool stripAfter = false, noTrim = false;
        if (!body.empty() && body.back() == '-') { stripAfter = true; body.pop_back(); }
        else if (!body.empty() && body.back() == '+') { noTrim = true; body.pop_back(); }
        body = Trim(body);

        if (kindChar == '{') {
            out.push_back({TokKind::Output, body, tagLine});
        } else if (kindChar == '%') {
            if (body == "raw") {
                // Cerca {% endraw %} (con eventuali modificatori).
                size_t scan = after;
                size_t rawEnd = std::string::npos, rawAfter = 0;
                while (true) {
                    size_t f = src.find("{%", scan);
                    if (f == std::string::npos) break;
                    size_t c = FindClose(src, f + 2, "%}", false);
                    if (c == std::string::npos) break;
                    std::string b = src.substr(f + 2, c - f - 2);
                    std::string tb = b;
                    if (!tb.empty() && (tb[0] == '-' || tb[0] == '+')) tb.erase(0, 1);
                    if (!tb.empty() && (tb.back() == '-' || tb.back() == '+')) tb.pop_back();
                    if (Trim(tb) == "endraw") { rawEnd = f; rawAfter = c + 2; break; }
                    scan = c + 2;
                }
                if (rawEnd == std::string::npos) throw TemplateError("manca {% endraw %}", tagLine);
                size_t rawStart = after;
                if (!stripAfter && opt.trim_blocks && !noTrim && rawStart < src.size() && src[rawStart] == '\n') ++rawStart;
                std::string raw = src.substr(rawStart, rawEnd - rawStart);
                if (stripAfter) { size_t b = 0; while (b < raw.size() && IsWs(raw[b])) ++b; raw = raw.substr(b); }
                if (rawEnd + 2 < src.size() && src[rawEnd + 2] == '-') raw = RStripAll(raw);
                else if (opt.lstrip_blocks) {
                    size_t nl = raw.rfind('\n');
                    size_t lpos = nl == std::string::npos ? 0 : nl + 1;
                    bool onlyWs = true;
                    for (size_t i = lpos; i < raw.size(); ++i) if (raw[i] != ' ' && raw[i] != '\t') { onlyWs = false; break; }
                    if (onlyWs && (nl != std::string::npos)) raw.erase(lpos);
                }
                if (!raw.empty()) out.push_back({TokKind::Text, raw, tagLine});
                line += CountLines(src, after, rawAfter);
                after = rawAfter;
                std::string endBody = src.substr(rawEnd + 2, rawAfter - 2 - rawEnd - 2);
                stripAfter = !endBody.empty() && endBody.back() == '-';
                noTrim = !endBody.empty() && endBody.back() == '+';
            } else {
                out.push_back({TokKind::Stmt, body, tagLine});
            }
        }
        // commenti: nessun token

        if (stripAfter) stripNextLeading = true;
        else if (block && opt.trim_blocks && !noTrim && after < src.size() && src[after] == '\n') {
            ++after;
            ++line;
        }
        pos = after;
        if (pos == src.size()) break;
    }
    return out;
}

// ---------------------------------------------------------------- Espressioni

struct ETok {
    enum K { Name, Str, Int, Op, End } k;
    std::string v;
};

std::vector<ETok> LexExpr(const std::string& s, int line) {
    std::vector<ETok> t;
    size_t i = 0;
    while (i < s.size()) {
        char c = s[i];
        if (IsWs(c)) { ++i; continue; }
        if (std::isalpha((unsigned char)c) || c == '_') {
            size_t b = i;
            while (i < s.size() && (std::isalnum((unsigned char)s[i]) || s[i] == '_')) ++i;
            t.push_back({ETok::Name, s.substr(b, i - b)});
        } else if (std::isdigit((unsigned char)c)) {
            size_t b = i;
            while (i < s.size() && std::isdigit((unsigned char)s[i])) ++i;
            t.push_back({ETok::Int, s.substr(b, i - b)});
        } else if (c == '\'' || c == '"') {
            char q = c;
            ++i;
            std::string v;
            bool closed = false;
            while (i < s.size()) {
                char d = s[i++];
                if (d == '\\' && i < s.size()) {
                    char e = s[i++];
                    if (e == 'n') v += '\n';
                    else if (e == 't') v += '\t';
                    else v += e;
                } else if (d == q) { closed = true; break; }
                else v += d;
            }
            if (!closed) throw TemplateError("stringa non chiusa", line);
            t.push_back({ETok::Str, v});
        } else {
            static const char* two[] = {"==", "!=", "<=", ">=", "//", "**"};
            bool done = false;
            for (const char* op : two) {
                if (s.compare(i, 2, op) == 0) { t.push_back({ETok::Op, op}); i += 2; done = true; break; }
            }
            if (done) continue;
            if (std::string("()[].,|~+-*/%<>=:").find(c) != std::string::npos) {
                t.push_back({ETok::Op, std::string(1, c)});
                ++i;
            } else {
                throw TemplateError(std::string("carattere inatteso '") + c + "'", line);
            }
        }
    }
    t.push_back({ETok::End, ""});
    return t;
}

struct Scope;
struct Expr;
using ExprP = std::shared_ptr<Expr>;

struct Scope {
    const TValue::Map* globals;
    std::vector<TValue::Map> frames;
    TValue Lookup(const std::string& name) const {
        for (auto it = frames.rbegin(); it != frames.rend(); ++it) {
            auto f = it->find(name);
            if (f != it->end()) return f->second;
        }
        auto g = globals->find(name);
        if (g != globals->end()) return g->second;
        return TValue();
    }
    void Set(const std::string& name, TValue v) {
        if (frames.empty()) frames.emplace_back();
        frames.back()[name] = std::move(v);
    }
};

struct Expr {
    virtual ~Expr() = default;
    virtual TValue Eval(Scope& s) const = 0;
    virtual void CollectNames(std::vector<std::string>&) const {}
};

struct LitExpr : Expr {
    TValue v;
    explicit LitExpr(TValue x) : v(std::move(x)) {}
    TValue Eval(Scope&) const override { return v; }
};

struct NameExpr : Expr {
    std::string name;
    explicit NameExpr(std::string n) : name(std::move(n)) {}
    TValue Eval(Scope& s) const override {
        if (name == "true" || name == "True") return TValue(true);
        if (name == "false" || name == "False") return TValue(false);
        if (name == "none" || name == "None") return TValue::None();
        return s.Lookup(name);
    }
    void CollectNames(std::vector<std::string>& out) const override { out.push_back(name); }
};

struct ListExpr : Expr {
    std::vector<ExprP> items;
    TValue Eval(Scope& s) const override {
        TValue::List l;
        for (auto& e : items) l.push_back(e->Eval(s));
        return TValue(std::move(l));
    }
    void CollectNames(std::vector<std::string>& out) const override { for (auto& e : items) e->CollectNames(out); }
};

struct AttrExpr : Expr {
    ExprP obj;
    std::string name;
    TValue Eval(Scope& s) const override {
        TValue o = obj->Eval(s);
        // makerelease documenta person.actor ma il suo template usa person.name: accetta entrambi.
        TValue v = o.Attr(name);
        if (!v.IsDefined() && name == "actor") v = o.Attr("name");
        if (!v.IsDefined() && name == "name") v = o.Attr("actor");
        return v;
    }
    void CollectNames(std::vector<std::string>& out) const override { obj->CollectNames(out); }
};

struct IndexExpr : Expr {
    ExprP obj, key;
    TValue Eval(Scope& s) const override { return obj->Eval(s).Index(key->Eval(s)); }
    void CollectNames(std::vector<std::string>& out) const override { obj->CollectNames(out); key->CollectNames(out); }
};

struct NotExpr : Expr {
    ExprP e;
    TValue Eval(Scope& s) const override { return TValue(!e->Eval(s).Truthy()); }
    void CollectNames(std::vector<std::string>& out) const override { e->CollectNames(out); }
};

struct NegExpr : Expr {
    ExprP e;
    TValue Eval(Scope& s) const override { return TValue(-e->Eval(s).AsInt()); }
    void CollectNames(std::vector<std::string>& out) const override { e->CollectNames(out); }
};

bool ValueEquals(const TValue& a, const TValue& b) {
    using K = TValue::Kind;
    if ((a.kind() == K::Int || a.kind() == K::Bool) && (b.kind() == K::Int || b.kind() == K::Bool)) return a.AsInt() == b.AsInt();
    if (a.kind() != b.kind()) {
        if (!a.IsDefined() && !b.IsDefined()) return true;
        return false;
    }
    if (a.kind() == K::Str) return a.StrRef() == b.StrRef();
    if (a.kind() == K::None || a.kind() == K::Undefined) return true;
    return a.Repr() == b.Repr();
}

int ValueCompare(const TValue& a, const TValue& b) {
    using K = TValue::Kind;
    if (a.kind() == K::Str && b.kind() == K::Str) return a.StrRef().compare(b.StrRef());
    long long x = a.AsInt(), y = b.AsInt();
    return x < y ? -1 : (x > y ? 1 : 0);
}

bool ValueContains(const TValue& container, const TValue& item) {
    using K = TValue::Kind;
    if (container.kind() == K::Str) return container.StrRef().find(item.ToString()) != std::string::npos;
    if (container.kind() == K::List) {
        for (auto& v : container.ListRef()) if (ValueEquals(v, item)) return true;
        return false;
    }
    if (container.kind() == K::Map) return container.MapRef().count(item.ToString()) > 0;
    return false;
}

struct BinExpr : Expr {
    std::string op;
    ExprP a, b;
    TValue Eval(Scope& s) const override {
        if (op == "and") {
            TValue x = a->Eval(s);
            return x.Truthy() ? b->Eval(s) : x;
        }
        if (op == "or") {
            TValue x = a->Eval(s);
            return x.Truthy() ? x : b->Eval(s);
        }
        TValue x = a->Eval(s), y = b->Eval(s);
        if (op == "==") return TValue(ValueEquals(x, y));
        if (op == "!=") return TValue(!ValueEquals(x, y));
        if (op == "<") return TValue(ValueCompare(x, y) < 0);
        if (op == ">") return TValue(ValueCompare(x, y) > 0);
        if (op == "<=") return TValue(ValueCompare(x, y) <= 0);
        if (op == ">=") return TValue(ValueCompare(x, y) >= 0);
        if (op == "in") return TValue(ValueContains(y, x));
        if (op == "not in") return TValue(!ValueContains(y, x));
        if (op == "~") return TValue(x.ToString() + y.ToString());
        if (op == "+") {
            if (x.kind() == TValue::Kind::Str || y.kind() == TValue::Kind::Str) return TValue(x.ToString() + y.ToString());
            if (x.kind() == TValue::Kind::List && y.kind() == TValue::Kind::List) {
                TValue::List l = x.ListRef();
                l.insert(l.end(), y.ListRef().begin(), y.ListRef().end());
                return TValue(std::move(l));
            }
            return TValue(x.AsInt() + y.AsInt());
        }
        if (op == "-") return TValue(x.AsInt() - y.AsInt());
        if (op == "*") return TValue(x.AsInt() * y.AsInt());
        if (op == "//" || op == "/") return TValue(y.AsInt() ? x.AsInt() / y.AsInt() : 0LL);
        if (op == "%") return TValue(y.AsInt() ? x.AsInt() % y.AsInt() : 0LL);
        return TValue();
    }
    void CollectNames(std::vector<std::string>& out) const override { a->CollectNames(out); b->CollectNames(out); }
};

struct CondExpr : Expr {
    ExprP value, cond, otherwise;
    TValue Eval(Scope& s) const override {
        if (cond->Eval(s).Truthy()) return value->Eval(s);
        return otherwise ? otherwise->Eval(s) : TValue();
    }
    void CollectNames(std::vector<std::string>& out) const override {
        value->CollectNames(out);
        cond->CollectNames(out);
        if (otherwise) otherwise->CollectNames(out);
    }
};

struct TestExpr : Expr {
    ExprP e;
    std::string test;
    bool negate = false;
    int line = 0;
    TValue Eval(Scope& s) const override {
        TValue v = e->Eval(s);
        using K = TValue::Kind;
        bool r;
        if (test == "defined") r = v.IsDefined();
        else if (test == "undefined") r = !v.IsDefined();
        else if (test == "none") r = v.kind() == K::None;
        else if (test == "string") r = v.kind() == K::Str;
        else if (test == "number") r = v.kind() == K::Int;
        else if (test == "iterable" || test == "sequence") r = v.kind() == K::List || v.kind() == K::Str || v.kind() == K::Map;
        else if (test == "mapping") r = v.kind() == K::Map;
        else if (test == "true") r = v.kind() == K::Bool && v.AsInt();
        else if (test == "false") r = v.kind() == K::Bool && !v.AsInt();
        else throw TemplateError("test sconosciuto: " + test, line);
        return TValue(negate ? !r : r);
    }
    void CollectNames(std::vector<std::string>& out) const override { e->CollectNames(out); }
};

struct FilterExpr : Expr {
    ExprP e;
    std::string name;
    std::vector<ExprP> args;
    int line = 0;
    TValue Arg(Scope& s, size_t i, TValue def = TValue()) const { return i < args.size() ? args[i]->Eval(s) : def; }
    TValue Eval(Scope& s) const override {
        TValue v = e->Eval(s);
        using K = TValue::Kind;
        if (name == "default" || name == "d") {
            bool boolean = Arg(s, 1, TValue(false)).Truthy();
            if (!v.IsDefined() || (boolean && !v.Truthy())) return Arg(s, 0, TValue(""));
            return v;
        }
        if (name == "upper") return TValue(ToUpper(v.ToString()));
        if (name == "lower") return TValue(ToLower(v.ToString()));
        if (name == "title") return TValue(TitleCase(v.ToString()));
        if (name == "capitalize") {
            std::string x = ToLower(v.ToString());
            if (!x.empty() && x[0] >= 'a' && x[0] <= 'z') x[0] = char(x[0] - 'a' + 'A');
            return TValue(x);
        }
        if (name == "trim") return TValue(Trim(v.ToString()));
        if (name == "length" || name == "count") return TValue((long long)v.Length());
        if (name == "string") return TValue(v.ToString());
        if (name == "int") return TValue(v.AsInt());
        if (name == "safe" || name == "e" || name == "escape") return v;  // BBCode: nessun escape HTML
        if (name == "list") {
            if (v.kind() == K::List) return v;
            TValue::List l;
            if (v.kind() == K::Str) for (char c : v.StrRef()) l.push_back(TValue(std::string(1, c)));
            return TValue(std::move(l));
        }
        if (name == "join") {
            std::string sep = Arg(s, 0, TValue("")).ToString();
            std::string out;
            bool first = true;
            for (auto& item : v.ListRef()) {
                if (!first) out += sep;
                first = false;
                out += item.ToString();
            }
            return TValue(out);
        }
        if (name == "replace") return TValue(ReplaceAll(v.ToString(), Arg(s, 0).ToString(), Arg(s, 1).ToString()));
        if (name == "first") return v.kind() == K::List ? v.Index(TValue(0)) : (v.kind() == K::Str ? v.Index(TValue(0)) : TValue());
        if (name == "last") return v.kind() == K::List || v.kind() == K::Str ? v.Index(TValue(-1)) : TValue();
        if (name == "sort") {
            TValue::List l = v.ListRef();
            std::stable_sort(l.begin(), l.end(), [](const TValue& a, const TValue& b) { return ValueCompare(a, b) < 0; });
            return TValue(std::move(l));
        }
        if (name == "truncate") {
            long long n = Arg(s, 0, TValue(255)).AsInt();
            std::string x = v.ToString();
            if ((long long)x.size() <= n) return TValue(x);
            std::string end = Arg(s, 2, TValue("...")).ToString();
            return TValue(x.substr(0, size_t(std::max<long long>(0, n - (long long)end.size()))) + end);
        }
        if (name == "indent") {
            long long n = Arg(s, 0, TValue(4)).AsInt();
            std::string pad(size_t(std::max<long long>(0, n)), ' ');
            std::string x = v.ToString(), out;
            for (size_t i = 0; i < x.size(); ++i) {
                out += x[i];
                if (x[i] == '\n' && i + 1 < x.size()) out += pad;
            }
            return TValue(out);
        }
        throw TemplateError("filtro sconosciuto: " + name, line);
    }
    void CollectNames(std::vector<std::string>& out) const override {
        e->CollectNames(out);
        for (auto& a : args) a->CollectNames(out);
    }
};

class ExprParser {
public:
    ExprParser(const std::string& src, int line) : t_(LexExpr(src, line)), line_(line) {}

    ExprP ParseAll() {
        ExprP e = ParseExpr();
        if (Peek().k != ETok::End) Fail("token inatteso '" + Peek().v + "'");
        return e;
    }
    ExprP ParseExpr() {
        ExprP v = ParseOr();
        if (IsName("if")) {
            Next();
            auto c = std::make_shared<CondExpr>();
            c->value = v;
            c->cond = ParseOr();
            if (IsName("else")) { Next(); c->otherwise = ParseExpr(); }
            return c;
        }
        return v;
    }
    // Espressione senza "x if c else y" (serve a {% for x in y if cond %}).
    ExprP ParseNoCond() { return ParseOr(); }
    // Per {% for a in b %}: legge un nome semplice.
    std::string ExpectName() {
        if (Peek().k != ETok::Name) Fail("atteso un nome");
        return Next().v;
    }
    bool IsName(const char* n) const { return Peek().k == ETok::Name && Peek().v == n; }
    bool IsOp(const char* o) const { return Peek().k == ETok::Op && Peek().v == o; }
    bool AtEnd() const { return Peek().k == ETok::End; }
    const ETok& Peek(size_t ahead = 0) const { return t_[std::min(p_ + ahead, t_.size() - 1)]; }
    ETok Next() { return t_[std::min(p_++, t_.size() - 1)]; }
    [[noreturn]] void Fail(const std::string& m) const { throw TemplateError(m, line_); }

private:
    std::vector<ETok> t_;
    size_t p_ = 0;
    int line_;

    ExprP ParseOr() {
        ExprP a = ParseAnd();
        while (IsName("or")) {
            Next();
            auto b = std::make_shared<BinExpr>();
            b->op = "or"; b->a = a; b->b = ParseAnd();
            a = b;
        }
        return a;
    }
    ExprP ParseAnd() {
        ExprP a = ParseNot();
        while (IsName("and")) {
            Next();
            auto b = std::make_shared<BinExpr>();
            b->op = "and"; b->a = a; b->b = ParseNot();
            a = b;
        }
        return a;
    }
    ExprP ParseNot() {
        if (IsName("not")) {
            Next();
            auto n = std::make_shared<NotExpr>();
            n->e = ParseNot();
            return n;
        }
        return ParseCompare();
    }
    ExprP ParseCompare() {
        ExprP a = ParseConcat();
        for (;;) {
            std::string op;
            if (Peek().k == ETok::Op && (Peek().v == "==" || Peek().v == "!=" || Peek().v == "<" || Peek().v == ">" || Peek().v == "<=" || Peek().v == ">=")) op = Next().v;
            else if (IsName("in")) { Next(); op = "in"; }
            else if (IsName("not") && Peek(1).k == ETok::Name && Peek(1).v == "in") { Next(); Next(); op = "not in"; }
            else break;
            auto b = std::make_shared<BinExpr>();
            b->op = op; b->a = a; b->b = ParseConcat();
            a = b;
        }
        return a;
    }
    ExprP ParseConcat() {
        ExprP a = ParseAdd();
        while (IsOp("~")) {
            Next();
            auto b = std::make_shared<BinExpr>();
            b->op = "~"; b->a = a; b->b = ParseAdd();
            a = b;
        }
        return a;
    }
    ExprP ParseAdd() {
        ExprP a = ParseMul();
        while (IsOp("+") || IsOp("-")) {
            std::string op = Next().v;
            auto b = std::make_shared<BinExpr>();
            b->op = op; b->a = a; b->b = ParseMul();
            a = b;
        }
        return a;
    }
    ExprP ParseMul() {
        ExprP a = ParseUnary();
        while (IsOp("*") || IsOp("/") || IsOp("//") || IsOp("%")) {
            std::string op = Next().v;
            auto b = std::make_shared<BinExpr>();
            b->op = op; b->a = a; b->b = ParseUnary();
            a = b;
        }
        return a;
    }
    ExprP ParseUnary() {
        if (IsOp("-")) {
            Next();
            auto n = std::make_shared<NegExpr>();
            n->e = ParseUnary();
            return n;
        }
        return ParseTestAndFilters();
    }
    ExprP ParseTestAndFilters() {
        ExprP e = ParsePostfix();
        for (;;) {
            if (IsOp("|")) {
                Next();
                auto f = std::make_shared<FilterExpr>();
                f->e = e;
                f->name = ExpectName();
                f->line = line_;
                if (IsOp("(")) f->args = ParseArgs();
                e = f;
            } else if (IsName("is")) {
                Next();
                auto t = std::make_shared<TestExpr>();
                t->e = e;
                t->line = line_;
                if (IsName("not")) { Next(); t->negate = true; }
                t->test = ExpectName();
                e = t;
            } else break;
        }
        return e;
    }
    std::vector<ExprP> ParseArgs() {
        std::vector<ExprP> args;
        Next();  // (
        if (IsOp(")")) { Next(); return args; }
        for (;;) {
            // Argomenti con nome (es. default(x, boolean=true)): il nome viene ignorato, conta la posizione.
            if (Peek().k == ETok::Name && Peek(1).k == ETok::Op && Peek(1).v == "=") { Next(); Next(); }
            args.push_back(ParseExpr());
            if (IsOp(",")) { Next(); continue; }
            if (IsOp(")")) { Next(); break; }
            Fail("attesa ',' o ')'");
        }
        return args;
    }
    ExprP ParsePostfix() {
        ExprP e = ParsePrimary();
        for (;;) {
            if (IsOp(".")) {
                Next();
                auto a = std::make_shared<AttrExpr>();
                a->obj = e;
                if (Peek().k == ETok::Int) {
                    auto ix = std::make_shared<IndexExpr>();
                    ix->obj = e;
                    ix->key = std::make_shared<LitExpr>(TValue(std::stoll(Next().v)));
                    e = ix;
                    continue;
                }
                a->name = ExpectName();
                e = a;
            } else if (IsOp("[")) {
                Next();
                auto ix = std::make_shared<IndexExpr>();
                ix->obj = e;
                ix->key = ParseExpr();
                if (!IsOp("]")) Fail("attesa ']'");
                Next();
                e = ix;
            } else break;
        }
        return e;
    }
    ExprP ParsePrimary() {
        const ETok& t = Peek();
        if (t.k == ETok::Str) return std::make_shared<LitExpr>(TValue(Next().v));
        if (t.k == ETok::Int) return std::make_shared<LitExpr>(TValue(std::stoll(Next().v)));
        if (t.k == ETok::Name) return std::make_shared<NameExpr>(Next().v);
        if (IsOp("(")) {
            Next();
            ExprP e = ParseExpr();
            if (!IsOp(")")) Fail("attesa ')'");
            Next();
            return e;
        }
        if (IsOp("[")) {
            Next();
            auto l = std::make_shared<ListExpr>();
            if (IsOp("]")) { Next(); return l; }
            for (;;) {
                l->items.push_back(ParseExpr());
                if (IsOp(",")) { Next(); if (IsOp("]")) { Next(); break; } continue; }
                if (IsOp("]")) { Next(); break; }
                Fail("attesa ',' o ']'");
            }
            return l;
        }
        if (t.k == ETok::End) Fail("espressione incompleta");
        Fail("token inatteso '" + t.v + "'");
    }
};

// ---------------------------------------------------------------- Nodi

struct Node;
using NodeP = std::shared_ptr<Node>;
using Body = std::vector<NodeP>;

struct Node {
    virtual ~Node() = default;
    virtual void Render(Scope& s, std::string& out) const = 0;
    virtual void CollectNames(std::vector<std::string>&) const {}
};

void RenderBody(const Body& b, Scope& s, std::string& out) {
    for (auto& n : b) n->Render(s, out);
}
void CollectBody(const Body& b, std::vector<std::string>& out) {
    for (auto& n : b) n->CollectNames(out);
}

struct TextNode : Node {
    std::string text;
    void Render(Scope&, std::string& out) const override { out += text; }
};

struct OutputNode : Node {
    ExprP e;
    void Render(Scope& s, std::string& out) const override { out += e->Eval(s).ToString(); }
    void CollectNames(std::vector<std::string>& o) const override { e->CollectNames(o); }
};

struct IfNode : Node {
    std::vector<std::pair<ExprP, Body>> branches;
    Body otherwise;
    void Render(Scope& s, std::string& out) const override {
        for (auto& br : branches) {
            if (br.first->Eval(s).Truthy()) { RenderBody(br.second, s, out); return; }
        }
        RenderBody(otherwise, s, out);
    }
    void CollectNames(std::vector<std::string>& o) const override {
        for (auto& br : branches) { br.first->CollectNames(o); CollectBody(br.second, o); }
        CollectBody(otherwise, o);
    }
};

struct ForNode : Node {
    std::vector<std::string> vars;
    ExprP iterable;
    ExprP filter;  // for x in y if cond
    Body body, otherwise;
    void Render(Scope& s, std::string& out) const override {
        TValue it = iterable->Eval(s);
        TValue::List items;
        if (it.kind() == TValue::Kind::List) items = it.ListRef();
        else if (it.kind() == TValue::Kind::Map) for (auto& kv : it.MapRef()) items.push_back(TValue(kv.first));
        else if (it.kind() == TValue::Kind::Str) for (char c : it.StrRef()) items.push_back(TValue(std::string(1, c)));
        if (filter) {
            TValue::List kept;
            for (auto& item : items) {
                s.frames.emplace_back();
                Bind(s, item);
                if (filter->Eval(s).Truthy()) kept.push_back(item);
                s.frames.pop_back();
            }
            items.swap(kept);
        }
        if (items.empty()) { RenderBody(otherwise, s, out); return; }
        const long long n = (long long)items.size();
        for (long long i = 0; i < n; ++i) {
            s.frames.emplace_back();
            Bind(s, items[size_t(i)]);
            TValue::Map loop;
            loop["index"] = TValue(i + 1);
            loop["index0"] = TValue(i);
            loop["revindex"] = TValue(n - i);
            loop["revindex0"] = TValue(n - i - 1);
            loop["first"] = TValue(i == 0);
            loop["last"] = TValue(i == n - 1);
            loop["length"] = TValue(n);
            s.Set("loop", TValue(std::move(loop)));
            RenderBody(body, s, out);
            s.frames.pop_back();
        }
    }
    void Bind(Scope& s, const TValue& item) const {
        if (vars.size() == 1) { s.Set(vars[0], item); return; }
        for (size_t k = 0; k < vars.size(); ++k) s.Set(vars[k], item.Index(TValue((long long)k)));
    }
    void CollectNames(std::vector<std::string>& o) const override {
        iterable->CollectNames(o);
        CollectBody(body, o);
        CollectBody(otherwise, o);
    }
};

struct SetNode : Node {
    std::string name;
    ExprP e;
    void Render(Scope& s, std::string&) const override {
        // Jinja: set dentro un for resta locale al ciclo; a livello radice è globale al template.
        s.Set(name, e->Eval(s));
    }
    void CollectNames(std::vector<std::string>& o) const override { e->CollectNames(o); }
};

class TemplateParser {
public:
    explicit TemplateParser(std::vector<Token> toks) : t_(std::move(toks)) {}

    Body ParseRoot() {
        Body b = ParseBody({}, nullptr);
        return b;
    }

private:
    std::vector<Token> t_;
    size_t p_ = 0;

    static std::string FirstWord(const std::string& s) {
        size_t i = 0;
        while (i < s.size() && !IsWs(s[i])) ++i;
        return s.substr(0, i);
    }
    static std::string Rest(const std::string& s) {
        size_t i = 0;
        while (i < s.size() && !IsWs(s[i])) ++i;
        return Trim(s.substr(i));
    }

    // Analizza fino a uno dei terminatori; restituisce quello trovato in *found.
    Body ParseBody(const std::vector<std::string>& terminators, Token* found) {
        Body body;
        while (p_ < t_.size()) {
            const Token& tok = t_[p_];
            if (tok.kind == TokKind::Text) {
                auto n = std::make_shared<TextNode>();
                n->text = tok.text;
                body.push_back(n);
                ++p_;
            } else if (tok.kind == TokKind::Output) {
                auto n = std::make_shared<OutputNode>();
                if (Trim(tok.text).empty()) throw TemplateError("espressione vuota in {{ }}", tok.line);
                ExprParser ep(tok.text, tok.line);
                n->e = ep.ParseAll();
                body.push_back(n);
                ++p_;
            } else {
                std::string word = FirstWord(tok.text);
                if (std::find(terminators.begin(), terminators.end(), word) != terminators.end()) {
                    if (found) *found = tok;
                    ++p_;
                    return body;
                }
                ++p_;
                if (word == "if") body.push_back(ParseIf(tok));
                else if (word == "for") body.push_back(ParseFor(tok));
                else if (word == "set") body.push_back(ParseSet(tok));
                else if (word == "endif" || word == "endfor" || word == "else" || word == "elif")
                    throw TemplateError("'" + word + "' senza blocco di apertura corrispondente", tok.line);
                else throw TemplateError("istruzione non supportata: '" + word + "'", tok.line);
            }
        }
        if (!terminators.empty()) {
            int line = t_.empty() ? 1 : t_.back().line;
            throw TemplateError("blocco non chiuso: manca {% " + terminators.back() + " %}", line);
        }
        return body;
    }

    NodeP ParseIf(const Token& open) {
        auto n = std::make_shared<IfNode>();
        ExprParser cond(Rest(open.text), open.line);
        ExprP c = cond.ParseAll();
        for (;;) {
            Token end{TokKind::Stmt, "", open.line};
            Body b = ParseBody({"elif", "else", "endif"}, &end);
            std::string w = FirstWord(end.text);
            n->branches.emplace_back(c, std::move(b));
            if (w == "elif") {
                ExprParser ep(Rest(end.text), end.line);
                c = ep.ParseAll();
                continue;
            }
            if (w == "else") {
                Token fin{TokKind::Stmt, "", end.line};
                n->otherwise = ParseBody({"endif"}, &fin);
            }
            break;
        }
        return n;
    }

    NodeP ParseFor(const Token& open) {
        auto n = std::make_shared<ForNode>();
        ExprParser ep(Rest(open.text), open.line);
        n->vars.push_back(ep.ExpectName());
        while (ep.IsOp(",")) { ep.Next(); n->vars.push_back(ep.ExpectName()); }
        if (!ep.IsName("in")) ep.Fail("atteso 'in' nel ciclo for");
        ep.Next();
        n->iterable = ep.ParseNoCond();
        if (ep.IsName("if")) { ep.Next(); n->filter = ep.ParseNoCond(); }
        if (!ep.AtEnd()) ep.Fail("token inatteso nel ciclo for");
        Token end{TokKind::Stmt, "", open.line};
        n->body = ParseBody({"else", "endfor"}, &end);
        if (FirstWord(end.text) == "else") {
            Token fin{TokKind::Stmt, "", end.line};
            n->otherwise = ParseBody({"endfor"}, &fin);
        }
        return n;
    }

    NodeP ParseSet(const Token& open) {
        auto n = std::make_shared<SetNode>();
        ExprParser ep(Rest(open.text), open.line);
        n->name = ep.ExpectName();
        if (!ep.IsOp("=")) ep.Fail("atteso '=' in set");
        ep.Next();
        n->e = ep.ParseExpr();
        if (!ep.AtEnd()) ep.Fail("token inatteso in set");
        return n;
    }
};

Body Compile(const std::string& source, const TemplateOptions& options) {
    TemplateParser parser(Lex(source, options));
    return parser.ParseRoot();
}

}  // namespace

std::string RenderTemplate(const std::string& source, const TValue::Map& context, const TemplateOptions& options) {
    Body body = Compile(source, options);
    Scope scope{&context, {}};
    scope.frames.emplace_back();
    std::string out;
    RenderBody(body, scope, out);
    return out;
}

void ValidateTemplate(const std::string& source, const TemplateOptions& options) { (void)Compile(source, options); }

bool TemplateReferences(const std::string& source, const std::string& variable) {
    try {
        Body body = Compile(source, TemplateOptions());
        std::vector<std::string> names;
        CollectBody(body, names);
        return std::find(names.begin(), names.end(), variable) != names.end();
    } catch (...) {
        return source.find(variable) != std::string::npos;
    }
}

}  // namespace rf
