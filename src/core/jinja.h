// ReleaseForge - motore template compatibile con il sottoinsieme di Jinja2
// usato dai template BBCode (makerelease e personalizzati).
//
// Supporta:
//   {{ espressione }}  {% if/elif/else/endif %}  {% for x in LISTA %}...{% else %}...{% endfor %}
//   {% set NOME = espressione %}  {# commenti #}  {% raw %}...{% endraw %}
//   controllo spazi {%- -%} {{- -}}, trim_blocks e lstrip_blocks (come makerelease)
//   operatori: and or not == != < > <= >= in, not in, ~ (concatenazione), + -, x if c else y
//   test: is defined / undefined / none / string / number / iterable / mapping  (anche "is not")
//   filtri: default(d), d, upper, lower, title, capitalize, trim, length, count, join(sep),
//           replace(a,b), first, last, string, int, truncate(n), safe, e, list, sort,
//           wordwrap? (no), indent(n)
//   attributi: x.nome, x['nome'], x[0], loop.index/index0/first/last/length/revindex
// Variabili non definite: stringa vuota (come Jinja2 con Undefined predefinito).
#pragma once
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace rf {

class TValue {
public:
    enum class Kind { Undefined, None, Bool, Int, Str, List, Map };
    using List = std::vector<TValue>;
    using Map = std::map<std::string, TValue>;

    TValue() = default;
    static TValue Undefined() { return TValue(); }
    static TValue None() { TValue v; v.kind_ = Kind::None; return v; }
    TValue(bool b) : kind_(Kind::Bool), int_(b ? 1 : 0) {}
    TValue(int i) : kind_(Kind::Int), int_(i) {}
    TValue(long long i) : kind_(Kind::Int), int_(i) {}
    TValue(const char* s) : kind_(Kind::Str), str_(std::make_shared<std::string>(s)) {}
    TValue(std::string s) : kind_(Kind::Str), str_(std::make_shared<std::string>(std::move(s))) {}
    TValue(List l) : kind_(Kind::List), list_(std::make_shared<List>(std::move(l))) {}
    TValue(Map m) : kind_(Kind::Map), map_(std::make_shared<Map>(std::move(m))) {}

    Kind kind() const { return kind_; }
    bool IsDefined() const { return kind_ != Kind::Undefined; }
    bool Truthy() const;
    std::string ToString() const;   // rendering
    std::string Repr() const;       // rappresentazione stile Python
    long long AsInt() const;
    const std::string& StrRef() const;
    const List& ListRef() const;
    const Map& MapRef() const;
    TValue Attr(const std::string& name) const;
    TValue Index(const TValue& key) const;
    size_t Length() const;

private:
    Kind kind_ = Kind::Undefined;
    long long int_ = 0;
    std::shared_ptr<std::string> str_;
    std::shared_ptr<List> list_;
    std::shared_ptr<Map> map_;
};

class TemplateError : public std::runtime_error {
public:
    TemplateError(const std::string& msg, int line)
        : std::runtime_error("Template, riga " + std::to_string(line) + ": " + msg), line_(line) {}
    int line() const { return line_; }

private:
    int line_;
};

struct TemplateOptions {
    bool trim_blocks = true;            // makerelease: Template(trim_blocks=True, ...)
    bool lstrip_blocks = true;          // makerelease: lstrip_blocks=True
    bool keep_trailing_newline = false; // default Jinja2
};

// Compila ed esegue il template. Lancia TemplateError con numero di riga.
std::string RenderTemplate(const std::string& source, const TValue::Map& context,
                           const TemplateOptions& options = TemplateOptions());

// Verifica sintattica senza rendering (per l'editor dei template).
void ValidateTemplate(const std::string& source, const TemplateOptions& options = TemplateOptions());

// Elenco delle variabili di primo livello referenziate (per saltare passi costosi,
// ad es. BITRATE_GRAPH o REPORT_AVINAPTIC, come fa makerelease).
bool TemplateReferences(const std::string& source, const std::string& variable);

}  // namespace rf
