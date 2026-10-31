// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// A small JSON value type (RFC 8259) used for project files. Objects keep insertion order.
#include <string>
#include <utility>
#include <vector>

namespace core {

class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };
    using Array = std::vector<Json>;
    using Member = std::pair<std::string, Json>;
    using Object = std::vector<Member>;

    Json() = default;
    Json(std::nullptr_t) {}
    Json(bool b) : m_type(Type::Bool), m_bool(b) {}
    Json(int n) : m_type(Type::Number), m_num(n) {}
    Json(long n) : m_type(Type::Number), m_num(double(n)) {}
    Json(long long n) : m_type(Type::Number), m_num(double(n)) {}
    Json(double n) : m_type(Type::Number), m_num(n) {}
    Json(float n) : m_type(Type::Number), m_num(double(n)) {}
    Json(const char* s) : m_type(Type::String), m_str(s) {}
    Json(std::string s) : m_type(Type::String), m_str(std::move(s)) {}
    Json(Array a) : m_type(Type::Array), m_arr(std::move(a)) {}
    Json(Object o) : m_type(Type::Object), m_obj(std::move(o)) {}

    static Json array() { return Json(Array{}); }
    static Json object() { return Json(Object{}); }

    Type type() const { return m_type; }
    bool isNull() const { return m_type == Type::Null; }
    bool isArray() const { return m_type == Type::Array; }
    bool isObject() const { return m_type == Type::Object; }

    // Lenient accessors: a wrong type or a missing key yields the default, like QJsonValue.
    bool toBool(bool def = false) const { return m_type == Type::Bool ? m_bool : def; }
    double toDouble(double def = 0) const { return m_type == Type::Number ? m_num : def; }
    int toInt(int def = 0) const { return m_type == Type::Number ? int(m_num) : def; }
    std::string toString(const std::string& def = {}) const { return m_type == Type::String ? m_str : def; }
    const Array& items() const { return m_type == Type::Array ? m_arr : emptyArray(); }
    const Object& members() const { return m_type == Type::Object ? m_obj : emptyObject(); }

    bool contains(const std::string& key) const { return find(key) != nullptr; }
    /// Member lookup; a Null value when missing.
    const Json& operator[](const std::string& key) const;
    const Json& operator[](const char* key) const { return (*this)[std::string(key)]; }
    /// Creates the object member when missing (turns Null into an object).
    Json& operator[](const std::string& key);
    Json& operator[](const char* key) { return (*this)[std::string(key)]; }
    /// Appends to an array (turns Null into an array).
    void push(Json v);
    size_t size() const { return m_type == Type::Array ? m_arr.size() : m_type == Type::Object ? m_obj.size() : 0; }

    /// Serialises; 'indent' > 0 pretty-prints.
    std::string dump(int indent = 2) const;
    /// Parses text; returns false and sets *error (with a position) on malformed input.
    static bool parse(const std::string& text, Json& out, std::string* error = nullptr);

private:
    const Json* find(const std::string& key) const;
    static const Array& emptyArray();
    static const Object& emptyObject();
    void write(std::string& out, int indent, int level) const;

    Type m_type = Type::Null;
    bool m_bool = false;
    double m_num = 0;
    std::string m_str;
    Array m_arr;
    Object m_obj;
};

}  // namespace core
