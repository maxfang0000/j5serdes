#include "j5serdes.h"
#include <sstream>

namespace J5Serdes {

using namespace std;

////////////////////////////////////////////////////////////////////////////////

#define assert_msg(cond, msg)            \
  if (!(cond)) {                         \
    stringstream errss;                  \
    errss << __func__ << "(): " << msg;  \
    throw runtime_error(errss.str());    \
  }

static void
__put_spaces(ostream& os, int n)
{
  for (int i=0; i<n; ++i) { os << ' '; }
}

static void
__skip_spaces(istream& istrm)
{
  while (istrm && std::isspace(istrm.peek())) { istrm.get(); }
}

/* called when one '/' is consumed from istream */
static void
__skip_comment(istream& istrm)
{
  char c = static_cast<char>(istrm.get());
  if (c == '/') {
    while (istrm && !istrm.eof()) {
      if (istrm.get() == '\n') { break; }
    }
  } else if (c == '*') {
    while (istrm && !istrm.eof()) {
      if (istrm.get() == '*') {
        if (istrm.get() == '/') { break; }
      }
    }
  } else {
    stringstream errss;
    errss << __func__ << "(): unexpected character `" << c << "' after '/'.";
    throw runtime_error(errss.str());
  }
}

static void
__skip_no_parse(istream& istrm)
{
  while (istrm && !istrm.eof()) {
    char c = istrm.peek();
    if (c == '/') {
      istrm.get(); __skip_comment(istrm);
    } else if (std::isspace(c)) {
      __skip_spaces(istrm);
    } else {
      break;
    }
  }
}

static const unordered_map<char, char> __escape_map = {
  { '\b', 'b' },
  { '\f', 'f' },
  { '\n', 'n' },
  { '\r', 'r' },
  { '\t', 't' },
  { '\\', '\\' },
  { '/',  '/' },
  { '"',  '"' },
  { '\'', '\'' },
};
static const unordered_map<char, char> __unescape_map = {
  { 'b',  '\b' },
  { 'f',  '\f' },
  { 'n',  '\n' },
  { 'r',  '\r' },
  { 't',  '\t' },
  { '\\', '\\' },
  { '/',  '/' },
  { '"',  '"' },
  { '\'', '\'' },
};

static string
__retrieve_escaped_string(istream& istrm, bool escape_sq = false)
{
  char c = static_cast<char>(istrm.get());
  if (__unescape_map.count(c)) {
    return string(1, __unescape_map.at(c));
  } else if (c == '\n') {
    return "";
  } else if (c == '\r') {
    char c2 = static_cast<char>(istrm.peek());
    if (c2 == '\n') { istrm.get(); }
    return "";
  } else if (c == 'u') {
    char u16[5] = { 0 };
    for (int i=0; i<4; ++i) {
      u16[i] = static_cast<char>(istrm.get());
      if (!std::isxdigit(u16[i])) {
        stringstream errss;
        errss << __func__ << "(): unexpected character `" << u16[i] << "'.";
        throw runtime_error(errss.str());
      }
    }
    uint16_t u16_val = std::stoul(u16, nullptr, 16);
    string ret;
    if (u16_val <= 0x7f) {
      ret += static_cast<char>(u16_val);
    } else if (u16_val <= 0x7ff) {
      ret += static_cast<char>(0xc0 | (u16_val >> 6));
      ret += static_cast<char>(0x80 | (u16_val & 0x3f));
    } else if (u16_val <= 0xd7ff || u16_val >= 0xdc00) {
      ret += static_cast<char>(0xe0 | (u16_val >> 12));
      ret += static_cast<char>(0x80 | ((u16_val >> 6) & 0x3f));
      ret += static_cast<char>(0x80 | (u16_val & 0x3f));
    } else {
      if (istrm.eof() || istrm.get() != '\\') {
        stringstream errss;
        errss << __func__ << "(): unexpected end of input stream.";
        throw runtime_error(errss.str());
      }
      if (istrm.eof() || istrm.get() != 'u') {
        stringstream errss;
        errss << __func__ << "(): unexpected character `" << c << "'.";
        throw runtime_error(errss.str());
      }
      char u16s[5] = { 0 };
      for (int i=0; i<4; ++i) {
        u16s[i] = static_cast<char>(istrm.get());
        if (!std::isxdigit(u16s[i])) {
          stringstream errss;
          errss << __func__ << "(): unexpected character `" << u16s[i] << "'.";
          throw runtime_error(errss.str());
        }
      }
      uint16_t u16s_val = std::stoul(u16s, nullptr, 16);
      if (u16s_val < 0xdc00 || u16s_val > 0xdfff) {
        stringstream errss;
        errss << __func__ << "(): unexpected character `" << u16s << "'.";
        throw runtime_error(errss.str());
      }
      uint32_t u32_val = 0x10000 + (((u16_val - 0xd800) << 10) |
                                    (u16s_val - 0xdc00));
      ret += static_cast<char>(0xf0 | (u32_val >> 18));
      ret += static_cast<char>(0x80 | ((u32_val >> 12) & 0x3f));
      ret += static_cast<char>(0x80 | ((u32_val >> 6) & 0x3f));
      ret += static_cast<char>(0x80 | (u32_val & 0x3f));
    }
    return ret;
  } else {
    stringstream errss;
    errss << __func__ << "(): unexpected escape sequence beginning with "
                         "character `" << c << "'.";
    throw runtime_error(errss.str());
  }
  return string();
}

string
__retrieve_quoted_string(istream& istrm)
{
  char open_quote = istrm.get();
  if (open_quote != '"' && open_quote != '\'') {
    stringstream errss;
    errss << __func__ << "(): unexpected open quote character `"
          << open_quote << "'.";
    throw runtime_error(errss.str());
  }
  string ret;
  bool closed = false;
  while (istrm && !istrm.eof()) {
    char c = istrm.get();
    if (c == '\\') {
      ret += __retrieve_escaped_string(istrm, open_quote == '\'');
    } else if (c == open_quote) {
      closed = true; break;
    } else {
      ret += c;
    }
  }
  if (!closed) {
    stringstream errss;
    errss << __func__ << "(): missing closing quote character `"
          << open_quote << "'.";
    throw runtime_error(errss.str());
  }
  return ret;
}

string
__escape_string(string_view sv, bool in_sq = false)
{
  string ret;
  for (char c : sv) {
    if (in_sq && c == '"') {
      ret += c;
    } else if (!in_sq && c == '\'') {
      ret += c;
    } else if (__escape_map.count(c)) {
      ret += '\\';
      ret += __escape_map.at(c);
    } else {
      ret += c;
    }
  }
  return ret;
}

////////////////////////////////////////////////////////////////////////////////

class JsonObjectImpl : public JsonObject {
public:
  JsonObjectImpl() {};
  virtual ~JsonObjectImpl() {};

  friend JsonObjectPtr make_json_object(istream&, const d_config_t&);

private:
  JsonRecordPtr             clone() const;
  void                      serialize(ostream&, const s_config_t&) const;

  pair<iterator, bool>      insert(value_type&&);
  pair<iterator, bool>      insert(string_view, JsonRecordPtr&&);
  pair<iterator, bool>      insert(string_view, const JsonRecordPtr&);

  iterator                  find(const string& key);
  const_iterator            find(const string& key) const;

  iterator                  erase(const_iterator);
  size_t                    erase(const string& key);

  iterator                  begin() { return _data.begin(); };
  const_iterator            begin() const { return _data.begin(); };
  iterator                  end() { return _data.end(); };
  const_iterator            end() const { return _data.end(); };

  void                      clear() { _data.clear(); _map.clear(); };

  size_t                    count(const string& key) const
                            { return _map.count(key); };
  bool                      empty() const { return _data.empty(); };
  size_t                    size() const { return _data.size(); };

private:
  list<value_type> _data;
  unordered_map<string, list<value_type>::iterator> _map;
};

JsonRecordPtr
JsonObjectImpl::clone() const
{
  return JsonObjectPtr();
}

void
JsonObjectImpl::serialize(ostream& strm, const s_config_t& cfg) const
{
  strm << '{' << endl;
  s_config_t cfg_next = cfg;
  cfg_next.global_indentation += cfg.indentation_width;
  bool first = true;
  for (const auto& entry : _data) {
    if (first) { first = false; } else { strm << ',' << endl; }
    __put_spaces(strm, cfg_next.global_indentation);
    strm << '"' << entry.first << "\" : ";
    entry.second->serialize(strm, cfg_next);
  }
  strm << endl;
  __put_spaces(strm, cfg.global_indentation);
  strm << '}';
}

pair<JsonObject::iterator, bool>
JsonObjectImpl::insert(value_type&& v)
{
  if (_map.count(v.first)) { return { _data.end(), false }; }
  _data.emplace_back(std::move(v));
  auto it = prev(_data.end());
  _map.insert({ it->first, it });
  return { it, true };
}

pair<JsonObject::iterator, bool>
JsonObjectImpl::insert(string_view key, JsonRecordPtr&& v)
{
  return insert(value_type(key, std::move(v)));
}

pair<JsonObject::iterator, bool>
JsonObjectImpl::insert(string_view key, const JsonRecordPtr& v)
{
  return insert(value_type(key, v->clone()));
}

JsonObject::iterator
JsonObjectImpl::find(const string& key)
{
  if (_map.count(key) == 0) { return _data.end(); }
  return _map.find(key)->second;
}

JsonObject::const_iterator
JsonObjectImpl::find(const string& key) const
{
  if (_map.count(key) == 0) { return _data.end(); }
  return _map.find(key)->second;
}

JsonObject::iterator
JsonObjectImpl::erase(JsonObject::const_iterator it)
{
  _map.erase(it->first);
  return _data.erase(it);
}

size_t
JsonObjectImpl::erase(const string& key)
{
  auto mit = _map.find(key);
  if (mit == _map.end()) { return 0; }
  _data.erase(mit->second);
  _map.erase(mit);
  return 1;
}

////////////////////////////////////////////////////////////////////////////////

class JsonArrayImpl : public JsonArray {
public:
  JsonArrayImpl() {};
  virtual ~JsonArrayImpl() {};

  friend JsonArrayPtr make_json_array(istream&, const d_config_t&);

private:
  JsonRecordPtr  clone() const;
  void           serialize(ostream&, const s_config_t&) const;

  void           push_back(JsonRecordPtr&&);
  void           push_back(const JsonRecordPtr&);

  iterator       begin()       { return _data.begin(); };
  const_iterator begin() const { return _data.begin(); };
  iterator       end()         { return _data.end(); };
  const_iterator end() const   { return _data.end(); };

  JsonRecord&    at(size_t idx)          { return *_data.at(idx); };
  const JsonRecord& at(size_t idx) const { return *_data.at(idx); };

  void           clear()       { _data.clear(); };

  bool           empty() const { return _data.empty(); };
  size_t         size() const  { return _data.size();  };

private:
  vector<JsonRecordPtr> _data;
};

JsonRecordPtr
JsonArrayImpl::clone() const
{
  JsonArrayPtr ret = make_json_array();
  for (const auto& item : _data) {
    ret->push_back(item->clone());
  }
  return ret;
}

void
JsonArrayImpl::serialize(ostream& strm, const s_config_t& cfg) const
{
  strm << '[' << endl;
  s_config_t cfg_next = cfg;
  cfg_next.global_indentation += cfg.indentation_width;
  bool first = true;
  for (const auto& item : _data) {
    if (first) { first = false; }
    else       { strm << ',' << endl; }
    __put_spaces(strm, cfg_next.global_indentation);
    item->serialize(strm, cfg_next);
  }
  strm << endl;
  __put_spaces(strm, cfg.global_indentation);
  strm << ']';
}

void
JsonArrayImpl::push_back(JsonRecordPtr&& v)
{
  _data.push_back(std::move(v));
}

void
JsonArrayImpl::push_back(const JsonRecordPtr& v)
{
  _data.push_back(v->clone());
}

////////////////////////////////////////////////////////////////////////////////

class JsonDataImpl : public JsonData {
public:
  enum class NativeType : uint8_t {
    NONE = 0,
    FLOAT = 1,
    INT = 2,
    BOOL = 3,
    STRING = 4,
  };
  JsonDataImpl() : _native_type(NativeType::NONE) {};
  JsonDataImpl(double value) : _native_type(NativeType::FLOAT)
    { *reinterpret_cast<double*>(&_long_buf) = value; };
  JsonDataImpl(string_view value) : _native_type(NativeType::STRING),
                                    _string_buf(value), _long_buf(0)
    { };
  JsonDataImpl(bool value) : _native_type(NativeType::BOOL),
                             _long_buf(value ? 1 : 0)
    { };
  virtual ~JsonDataImpl() {};

  friend JsonDataPtr make_json_data(istream&, const d_config_t&);

private:
  JsonRecordPtr clone() const;
  void          serialize(ostream&, const s_config_t&) const;

private:
  uint64_t   _long_buf;
  string     _string_buf;
  NativeType _native_type;
};

JsonRecordPtr
JsonDataImpl::clone() const
{
  return make_unique<JsonDataImpl>(*this);
}

void
JsonDataImpl::serialize(ostream& strm, const s_config_t& cfg) const
{
  switch (_native_type) {
    case NativeType::NONE:
      strm << "null";
      break;
    case NativeType::FLOAT:
      strm << *reinterpret_cast<const double*>(&_long_buf);
      break;
    case NativeType::INT:
      strm << *reinterpret_cast<const int64_t*>(&_long_buf);
      break;
    case NativeType::BOOL:
      strm << (_long_buf ? "true" : "false");
      break;
    case NativeType::STRING:
      strm << '"' << __escape_string(_string_buf) << '"';
      break;
    default:
      throw runtime_error("JsonData::serialize(): unknown native type.");
  }
}

JsonObjectPtr
make_json_object()
{
  return make_unique<JsonObjectImpl>();
}

JsonObjectPtr
make_json_object(istream& istrm, const s_config_t& cfg)
{
  return make_unique<JsonObjectImpl>();
}

JsonArrayPtr
make_json_array()
{
  return make_unique<JsonArrayImpl>();
}

template<typename T>
JsonDataPtr
make_json_data(T value)
{
  return make_unique<JsonDataImpl>(value);
}
template<>
JsonDataPtr
make_json_data<const char*>(const char* value)
{
  return make_unique<JsonDataImpl>(string_view(value));
}

template JsonDataPtr
make_json_data<bool>(bool);
template JsonDataPtr
make_json_data<double>(double);
template JsonDataPtr
make_json_data<string_view>(string_view);
template JsonDataPtr
make_json_data<const char*>(const char*);
template JsonDataPtr
make_json_data<const string&>(const string&);
template JsonDataPtr
make_json_data<string>(string);

////////////////////////////////////////////////////////////////////////////////

enum class JsonDeserializeState : uint8_t {
  OBJECT_OPEN,
  OBJECT_KEY,
  OBJECT_COLON,
  OBJECT_VALUE,
  OBJECT_COMMA,
  OBJECT_CLOSE,
  ARRAY_OPEN,
  ARRAY_ENTRY,
  ARRAY_COMMA,
  ARRAY_CLOSE
};

JsonRecordPtr
make_json_record(istream& istrm, const d_config_t& cfg)
{
  __skip_no_parse(istrm);
  if (istrm.eof()) { return JsonRecordPtr(); }
  if (istrm.peek() == '{') { return make_json_object(istrm, cfg); }
  else if (istrm.peek() == '[') { return make_json_array(istrm, cfg); }
  else { return make_json_data(istrm, cfg); }
}

JsonDataPtr
make_json_data(istream& istrm, const d_config_t& cfg)
{
  // Skip leading whitespace characters in the input stream
  __skip_no_parse(istrm);
  if (istrm.eof()) {
    return JsonDataPtr();  /* return nullptr if nothing matched */
  }
  unique_ptr<JsonDataImpl> ret = make_unique<JsonDataImpl>();
  if (istrm.peek() == '"' || istrm.peek() == '\'') {
    ret->_string_buf = __retrieve_quoted_string(istrm);
    ret->_native_type = JsonDataImpl::NativeType::STRING;
    ret->_long_buf = 0;
  } else {
    // read till a separator
    string token;
    while (istrm && !istrm.eof()) {
      char c = istrm.peek();
      if (std::isspace(c) || c == ',' || c == ']' || c == '}') { break; }
      token += static_cast<char>(istrm.get());
    }
    if (token.empty()) {
      return JsonDataPtr();  /* return nullptr if nothing matched */
    }
    if (token == "null") {
      ret->_native_type = JsonDataImpl::NativeType::NONE;
      ret->_long_buf = 0;
    } else if (token == "true") {
      ret->_native_type = JsonDataImpl::NativeType::BOOL;
      ret->_long_buf = 1;
    } else if (token == "false") {
      ret->_native_type = JsonDataImpl::NativeType::BOOL;
      ret->_long_buf = 0;
    } else {
      if (token[0] == '+') {
        if (token.size() == 1) {
          stringstream errss;
          errss << __func__ << "(): unexpected token `+'.";
          throw runtime_error(errss.str());
        } else if (token[1] == '-') {
          stringstream errss;
          errss << __func__ << "(): unexpected token `+-`.";
          throw runtime_error(errss.str());
        } else {
          token = token.substr(1);
        }
      }
      if (token.find(".") != string::npos) {
        ret->_native_type = JsonDataImpl::NativeType::FLOAT;
        size_t after_pos = 0;
        double d = stod(token, &after_pos);
        if (after_pos != token.size()) {
          stringstream errss;
          errss << __func__ << "(): unexpected trailing characters "
                << "after floating point number.";
          throw runtime_error(errss.str());
        }
        ret->_long_buf = *(reinterpret_cast<const uint64_t*>(&d));
      } else {
        ret->_native_type = JsonDataImpl::NativeType::INT;
        size_t after_pos = 0;
        int64_t i = stoll(token, &after_pos, 0);
        if (after_pos != token.size()) {
          stringstream errss;
          errss << __func__ << "(): unexpected trailing characters "
                << "after integer number.";
          throw runtime_error(errss.str());
        }
        ret->_long_buf = *(reinterpret_cast<const uint64_t*>(&i));
      }
    }
  }
  return ret;
}

////////////////////////////////////////////////////////////////////////////////

JsonArrayPtr
make_json_array(istream& istrm, const d_config_t& cfg)
{
  // Skip leading whitespace characters in the input stream
  __skip_no_parse(istrm);
  if (istrm.eof() || istrm.peek() != '[') { return JsonArrayPtr(); }
  JsonDeserializeState state = JsonDeserializeState::ARRAY_OPEN;
  istrm.get();  /* consume the opening square bracket */
  unique_ptr<JsonArrayImpl> ret = make_unique<JsonArrayImpl>();
  state = JsonDeserializeState::ARRAY_ENTRY;
  while (istrm && !istrm.eof()) {
    __skip_no_parse(istrm);
    if (istrm.eof()) { break; }
    char c = istrm.peek();
    if (c == ']') {
      assert_msg(state == JsonDeserializeState::ARRAY_COMMA ||
                 state == JsonDeserializeState::ARRAY_ENTRY,
                 "unexpected closing square bracket in json array.");
      istrm.get();
      state = JsonDeserializeState::ARRAY_CLOSE;
      break;
    } else if (c == ',') {
      assert_msg(state == JsonDeserializeState::ARRAY_COMMA,
                 "unexpected comma in json array.");
      istrm.get();
      state = JsonDeserializeState::ARRAY_ENTRY;
    } else {
      assert_msg(state == JsonDeserializeState::ARRAY_ENTRY,
                 "unexpected non-json characters in json array.");
      auto item = make_json_record(istrm, cfg);
      assert_msg(item != JsonRecordPtr(),
                 "unexpected non-json characters in json array entry.");
      ret->push_back(std::move(item));
      state = JsonDeserializeState::ARRAY_COMMA;
    }
  }
  assert_msg(state == JsonDeserializeState::ARRAY_CLOSE,
             "missing closing square bracket in json array.");
  return ret;
}

////////////////////////////////////////////////////////////////////////////////

JsonObjectPtr
make_json_object(istream& istrm, const d_config_t& cfg)
{
  // Skip leading whitespace characters in the input stream
  __skip_no_parse(istrm);
  if (istrm.eof() || istrm.peek() != '{') { return JsonObjectPtr(); }
  JsonDeserializeState state = JsonDeserializeState::OBJECT_OPEN;
  istrm.get();  /* consume the opening curly brace */
  unique_ptr<JsonObjectImpl> ret = make_unique<JsonObjectImpl>();
  state = JsonDeserializeState::OBJECT_KEY;
  string key;
  while (istrm && !istrm.eof()) {
    __skip_no_parse(istrm);
    if (istrm.eof()) { break; }
    char c = istrm.peek();
    if (c == '}') {
      assert_msg(state == JsonDeserializeState::OBJECT_COMMA ||
                 state == JsonDeserializeState::OBJECT_KEY,
                 "unexpected closing curly brace in json object.");
      istrm.get();
      state = JsonDeserializeState::OBJECT_CLOSE;
      break;
    } else if (c == ',') {
      assert_msg(state == JsonDeserializeState::OBJECT_COMMA,
                 "unexpected comma in json object.");
      istrm.get();
      state = JsonDeserializeState::OBJECT_KEY;
    } else if (c == ':') {
      assert_msg(state == JsonDeserializeState::OBJECT_COLON,
                 "unexpected colon in json object.");
      istrm.get();
      state = JsonDeserializeState::OBJECT_VALUE;
    } else if (state == JsonDeserializeState::OBJECT_KEY) {
      if (c == '"' || c == '\'') {
        key = __retrieve_quoted_string(istrm);
      } else {
        key.clear();
        while (istrm && !istrm.eof()) {
          char c = istrm.peek();
          if (std::isspace(c) || c == ':' || c == ',' || c == '}') { break; }
          key += static_cast<char>(istrm.get());
        }
      }
      assert_msg(key.length(),
                 "unexpected non-json characters in json object key.");
      state = JsonDeserializeState::OBJECT_COLON;
    } else {
      assert_msg(state == JsonDeserializeState::OBJECT_VALUE,
                 "unexpected non-json characters in json object value.");
      auto value = make_json_record(istrm, cfg);
      assert_msg(value != JsonRecordPtr(),
                 "unexpected non-json characters in json object value.");
      ret->insert(std::string_view(key), std::move(value));
      state = JsonDeserializeState::OBJECT_COMMA;
    }
  }
  assert_msg(state == JsonDeserializeState::OBJECT_CLOSE,
             "missing closing curly brace in json object.");
  return ret;
}

}
