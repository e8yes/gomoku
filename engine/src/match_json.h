#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <variant>
#include <vector>

// A deliberately small JSON value/parser used by the native match client.
// The match protocol is JSON-RPC over JSON Lines; keeping this codec local to
// the engine avoids adding a third-party dependency to the C++ build.
class JsonValue {
 public:
  using Array = std::vector<JsonValue>;
  using Object = std::map<std::string, JsonValue>;

  JsonValue();
  JsonValue(std::nullptr_t);
  JsonValue(bool value);
  JsonValue(int value);
  JsonValue(std::int64_t value);
  JsonValue(double value);
  JsonValue(const char* value);
  JsonValue(std::string value);
  JsonValue(Array value);
  JsonValue(Object value);

  bool IsNull() const;
  bool IsBool() const;
  bool IsNumber() const;
  bool IsString() const;
  bool IsArray() const;
  bool IsObject() const;

  bool AsBool() const;
  double AsNumber() const;
  std::int64_t AsInt() const;
  const std::string& AsString() const;
  const Array& AsArray() const;
  const Object& AsObject() const;
  Object& AsObject();

  const JsonValue* Find(const std::string& key) const;
  const JsonValue& At(const std::string& key) const;

 private:
  std::variant<std::nullptr_t, bool, double, std::string, Array, Object> value_;
};

JsonValue ParseJson(const std::string& text);
std::string SerializeJson(const JsonValue& value);
