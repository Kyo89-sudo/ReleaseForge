// ReleaseForge - parser/scrittore JSON minimale (UTF-8), senza dipendenze.
#pragma once
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace rf {

class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Json() = default;
    static Json Parse(const std::string& text);   // lancia JsonError
    static std::string Quote(const std::string& s); // stringa JSON con escape

    Type type() const { return type_; }
    bool IsNull() const { return type_ == Type::Null; }
    bool IsObject() const { return type_ == Type::Object; }
    bool IsArray() const { return type_ == Type::Array; }
    bool IsString() const { return type_ == Type::String; }
    bool IsNumber() const { return type_ == Type::Number; }

    // Accesso tollerante: chiave/indice mancante -> Json nullo.
    const Json& operator[](const std::string& key) const;
    const Json& operator[](size_t index) const;
    bool Has(const std::string& key) const;
    size_t Size() const;

    // Testo: stringhe così come sono, numeri senza decimali se interi, bool "true"/"false", null "".
    std::string Str() const;
    double Num() const { return type_ == Type::Number ? num_ : 0.0; }
    bool Bool() const { return type_ == Type::Bool ? bool_ : false; }

    const std::vector<Json>& Items() const { return arr_; }
    const std::vector<std::pair<std::string, Json>>& Members() const { return obj_; }

private:
    friend class JsonParser;
    Type type_ = Type::Null;
    bool bool_ = false;
    double num_ = 0.0;
    std::string str_;
    std::vector<Json> arr_;
    std::vector<std::pair<std::string, Json>> obj_;
};

class JsonError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

}  // namespace rf
