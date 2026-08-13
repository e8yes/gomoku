#include "match_json.h"

#include <cmath>
#include <cstddef>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace {

class Parser {
 public:
  explicit Parser(const std::string& text) : text_(text) {}

  JsonValue Parse() {
    SkipWhitespace();
    JsonValue value = ParseValue();
    SkipWhitespace();
    if (position_ != text_.size()) {
      Fail("trailing characters after JSON value");
    }
    return value;
  }

 private:
  [[noreturn]] void Fail(const std::string& message) const {
    throw std::invalid_argument("JSON parse error at byte " +
                                std::to_string(position_) + ": " + message);
  }

  void SkipWhitespace() {
    while (position_ < text_.size()) {
      const char c = text_[position_];
      if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
      ++position_;
    }
  }

  char Take() {
    if (position_ >= text_.size()) Fail("unexpected end of input");
    return text_[position_++];
  }

  void Expect(char expected) {
    if (Take() != expected) {
      Fail(std::string("expected '") + expected + "'");
    }
  }

  JsonValue ParseValue() {
    if (position_ >= text_.size()) Fail("expected a value");
    switch (text_[position_]) {
      case 'n':
        ParseLiteral("null");
        return JsonValue();
      case 't':
        ParseLiteral("true");
        return JsonValue(true);
      case 'f':
        ParseLiteral("false");
        return JsonValue(false);
      case '"':
        return JsonValue(ParseString());
      case '[':
        return ParseArray();
      case '{':
        return ParseObject();
      default:
        if (text_[position_] == '-' ||
            (text_[position_] >= '0' && text_[position_] <= '9')) {
          return ParseNumber();
        }
        Fail("unexpected character");
    }
  }

  void ParseLiteral(const char* literal) {
    for (const char* p = literal; *p != '\0'; ++p) {
      if (position_ >= text_.size() || text_[position_++] != *p) {
        Fail("invalid literal");
      }
    }
  }

  static void AppendUtf8(std::string* output, std::uint32_t codepoint) {
    if (codepoint <= 0x7f) {
      output->push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7ff) {
      output->push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
      output->push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else if (codepoint <= 0xffff) {
      output->push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
      output->push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
      output->push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else if (codepoint <= 0x10ffff) {
      output->push_back(static_cast<char>(0xf0 | (codepoint >> 18)));
      output->push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
      output->push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
      output->push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else {
      throw std::invalid_argument("JSON string contains an invalid codepoint");
    }
  }

  std::uint32_t ParseHexCodepoint() {
    std::uint32_t value = 0;
    for (int i = 0; i < 4; ++i) {
      const char c = Take();
      value <<= 4;
      if (c >= '0' && c <= '9') {
        value |= static_cast<std::uint32_t>(c - '0');
      } else if (c >= 'a' && c <= 'f') {
        value |= static_cast<std::uint32_t>(c - 'a' + 10);
      } else if (c >= 'A' && c <= 'F') {
        value |= static_cast<std::uint32_t>(c - 'A' + 10);
      } else {
        Fail("invalid unicode escape");
      }
    }
    return value;
  }

  std::string ParseString() {
    Expect('"');
    std::string result;
    while (position_ < text_.size()) {
      const unsigned char c = static_cast<unsigned char>(Take());
      if (c == '"') return result;
      if (c < 0x20) Fail("unescaped control character in string");
      if (c != '\\') {
        result.push_back(static_cast<char>(c));
        continue;
      }

      const char escaped = Take();
      switch (escaped) {
        case '"':
          result.push_back('"');
          break;
        case '\\':
          result.push_back('\\');
          break;
        case '/':
          result.push_back('/');
          break;
        case 'b':
          result.push_back('\b');
          break;
        case 'f':
          result.push_back('\f');
          break;
        case 'n':
          result.push_back('\n');
          break;
        case 'r':
          result.push_back('\r');
          break;
        case 't':
          result.push_back('\t');
          break;
        case 'u': {
          const std::uint32_t first = ParseHexCodepoint();
          // Combine a UTF-16 surrogate pair when present. This is enough for
          // arbitrary player names while retaining a small parser footprint.
          if (first >= 0xd800 && first <= 0xdbff) {
            if (position_ + 6 > text_.size() || text_[position_] != '\\' ||
                text_[position_ + 1] != 'u') {
              Fail("high surrogate without low surrogate");
            }
            position_ += 2;
            const std::uint32_t second = ParseHexCodepoint();
            if (second < 0xdc00 || second > 0xdfff) {
              Fail("invalid low surrogate");
            }
            AppendUtf8(&result,
                       0x10000 + ((first - 0xd800) << 10) + (second - 0xdc00));
          } else if (first >= 0xdc00 && first <= 0xdfff) {
            Fail("unexpected low surrogate");
          } else {
            AppendUtf8(&result, first);
          }
          break;
        }
        default:
          Fail("invalid escape sequence");
      }
    }
    Fail("unterminated string");
  }

  JsonValue ParseArray() {
    Expect('[');
    JsonValue::Array result;
    SkipWhitespace();
    if (position_ < text_.size() && text_[position_] == ']') {
      ++position_;
      return JsonValue(std::move(result));
    }
    while (true) {
      SkipWhitespace();
      result.push_back(ParseValue());
      SkipWhitespace();
      const char separator = Take();
      if (separator == ']') break;
      if (separator != ',') Fail("expected ',' or ']' in array");
    }
    return JsonValue(std::move(result));
  }

  JsonValue ParseObject() {
    Expect('{');
    JsonValue::Object result;
    SkipWhitespace();
    if (position_ < text_.size() && text_[position_] == '}') {
      ++position_;
      return JsonValue(std::move(result));
    }
    while (true) {
      SkipWhitespace();
      if (position_ >= text_.size() || text_[position_] != '"') {
        Fail("object keys must be strings");
      }
      std::string key = ParseString();
      SkipWhitespace();
      Expect(':');
      SkipWhitespace();
      auto [it, inserted] = result.emplace(std::move(key), ParseValue());
      if (!inserted) Fail("duplicate object key");
      SkipWhitespace();
      const char separator = Take();
      if (separator == '}') break;
      if (separator != ',') Fail("expected ',' or '}' in object");
    }
    return JsonValue(std::move(result));
  }

  JsonValue ParseNumber() {
    const std::size_t start = position_;
    if (text_[position_] == '-') ++position_;
    if (position_ >= text_.size()) Fail("incomplete number");
    if (text_[position_] == '0') {
      ++position_;
      if (position_ < text_.size() && text_[position_] >= '0' &&
          text_[position_] <= '9') {
        Fail("leading zero in number");
      }
    } else {
      if (text_[position_] < '1' || text_[position_] > '9') {
        Fail("invalid number");
      }
      while (position_ < text_.size() && text_[position_] >= '0' &&
             text_[position_] <= '9') {
        ++position_;
      }
    }
    if (position_ < text_.size() && text_[position_] == '.') {
      ++position_;
      const std::size_t fraction_start = position_;
      while (position_ < text_.size() && text_[position_] >= '0' &&
             text_[position_] <= '9') {
        ++position_;
      }
      if (position_ == fraction_start) Fail("fraction has no digits");
    }
    if (position_ < text_.size() &&
        (text_[position_] == 'e' || text_[position_] == 'E')) {
      ++position_;
      if (position_ < text_.size() &&
          (text_[position_] == '+' || text_[position_] == '-')) {
        ++position_;
      }
      const std::size_t exponent_start = position_;
      while (position_ < text_.size() && text_[position_] >= '0' &&
             text_[position_] <= '9') {
        ++position_;
      }
      if (position_ == exponent_start) Fail("exponent has no digits");
    }

    const std::string number = text_.substr(start, position_ - start);
    std::size_t consumed = 0;
    double value = 0.0;
    try {
      value = std::stod(number, &consumed);
    } catch (const std::exception&) {
      Fail("invalid number");
    }
    if (consumed != number.size() || !std::isfinite(value)) {
      Fail("number is not finite");
    }
    return JsonValue(value);
  }

  const std::string& text_;
  std::size_t position_ = 0;
};

void SerializeString(const std::string& value, std::string* output) {
  output->push_back('"');
  for (unsigned char c : value) {
    switch (c) {
      case '"':
        *output += "\\\"";
        break;
      case '\\':
        *output += "\\\\";
        break;
      case '\b':
        *output += "\\b";
        break;
      case '\f':
        *output += "\\f";
        break;
      case '\n':
        *output += "\\n";
        break;
      case '\r':
        *output += "\\r";
        break;
      case '\t':
        *output += "\\t";
        break;
      default:
        if (c < 0x20) {
          std::ostringstream escaped;
          escaped << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                  << static_cast<int>(c);
          *output += escaped.str();
        } else {
          output->push_back(static_cast<char>(c));
        }
    }
  }
  output->push_back('"');
}

void SerializeValue(const JsonValue& value, std::string* output) {
  if (value.IsNull()) {
    *output += "null";
  } else if (value.IsBool()) {
    *output += value.AsBool() ? "true" : "false";
  } else if (value.IsNumber()) {
    std::ostringstream number;
    number << std::setprecision(std::numeric_limits<double>::max_digits10)
           << value.AsNumber();
    *output += number.str();
  } else if (value.IsString()) {
    SerializeString(value.AsString(), output);
  } else if (value.IsArray()) {
    output->push_back('[');
    bool first = true;
    for (const JsonValue& child : value.AsArray()) {
      if (!first) output->push_back(',');
      first = false;
      SerializeValue(child, output);
    }
    output->push_back(']');
  } else {
    output->push_back('{');
    bool first = true;
    for (const auto& [key, child] : value.AsObject()) {
      if (!first) output->push_back(',');
      first = false;
      SerializeString(key, output);
      output->push_back(':');
      SerializeValue(child, output);
    }
    output->push_back('}');
  }
}

}  // namespace

JsonValue::JsonValue() : value_(nullptr) {}
JsonValue::JsonValue(std::nullptr_t) : value_(nullptr) {}
JsonValue::JsonValue(bool value) : value_(value) {}
JsonValue::JsonValue(int value) : value_(static_cast<double>(value)) {}
JsonValue::JsonValue(std::int64_t value) : value_(static_cast<double>(value)) {}
JsonValue::JsonValue(double value) : value_(value) {
  if (!std::isfinite(value)) {
    throw std::invalid_argument("JSON numbers must be finite");
  }
}
JsonValue::JsonValue(const char* value) : value_(std::string(value)) {}
JsonValue::JsonValue(std::string value) : value_(std::move(value)) {}
JsonValue::JsonValue(Array value) : value_(std::move(value)) {}
JsonValue::JsonValue(Object value) : value_(std::move(value)) {}

bool JsonValue::IsNull() const {
  return std::holds_alternative<std::nullptr_t>(value_);
}
bool JsonValue::IsBool() const { return std::holds_alternative<bool>(value_); }
bool JsonValue::IsNumber() const {
  return std::holds_alternative<double>(value_);
}
bool JsonValue::IsString() const {
  return std::holds_alternative<std::string>(value_);
}
bool JsonValue::IsArray() const {
  return std::holds_alternative<Array>(value_);
}
bool JsonValue::IsObject() const {
  return std::holds_alternative<Object>(value_);
}

bool JsonValue::AsBool() const {
  if (!IsBool()) throw std::runtime_error("JSON value is not a boolean");
  return std::get<bool>(value_);
}

double JsonValue::AsNumber() const {
  if (!IsNumber()) throw std::runtime_error("JSON value is not a number");
  return std::get<double>(value_);
}

std::int64_t JsonValue::AsInt() const {
  const double number = AsNumber();
  if (std::trunc(number) != number ||
      number < static_cast<double>(std::numeric_limits<std::int64_t>::min()) ||
      number > static_cast<double>(std::numeric_limits<std::int64_t>::max())) {
    throw std::runtime_error("JSON number is not an integer");
  }
  return static_cast<std::int64_t>(number);
}

const std::string& JsonValue::AsString() const {
  if (!IsString()) throw std::runtime_error("JSON value is not a string");
  return std::get<std::string>(value_);
}

const JsonValue::Array& JsonValue::AsArray() const {
  if (!IsArray()) throw std::runtime_error("JSON value is not an array");
  return std::get<Array>(value_);
}

const JsonValue::Object& JsonValue::AsObject() const {
  if (!IsObject()) throw std::runtime_error("JSON value is not an object");
  return std::get<Object>(value_);
}

JsonValue::Object& JsonValue::AsObject() {
  if (!IsObject()) throw std::runtime_error("JSON value is not an object");
  return std::get<Object>(value_);
}

const JsonValue* JsonValue::Find(const std::string& key) const {
  if (!IsObject()) throw std::runtime_error("JSON value is not an object");
  const auto& object = std::get<Object>(value_);
  const auto it = object.find(key);
  return it == object.end() ? nullptr : &it->second;
}

const JsonValue& JsonValue::At(const std::string& key) const {
  const JsonValue* value = Find(key);
  if (value == nullptr) {
    throw std::out_of_range("JSON object is missing key '" + key + "'");
  }
  return *value;
}

JsonValue ParseJson(const std::string& text) { return Parser(text).Parse(); }

std::string SerializeJson(const JsonValue& value) {
  std::string result;
  SerializeValue(value, &result);
  return result;
}
