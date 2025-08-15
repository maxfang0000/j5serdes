#include "j5serdes.h"
#include <cstring>
#include <deque>
#include <functional>
#include <iomanip>
#include <sstream>
#include <stack>
#include <unordered_map>
#include <variant>

namespace J5Serdes {

using namespace std;

////////////////////////////////////////////////////////////////////////////////
// helper functions

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
    assert_msg(0, "unexpected character `" << c << "' after '/'.");
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
__retrieve_escaped_string(istream& istrm)
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
      assert_msg(std::isxdigit(u16[i]),
                 "unexpected non-hexadecimal character `" << u16[i] << "'.");
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
      assert_msg(!istrm.eof() && istrm.get() == '\\',
                 "expecting a subsequent code unit after the first code unit "
                 "in the surrogate range, starting with \\u.");
      assert_msg(!istrm.eof() && istrm.get() == 'u',
                 "expecting a subsequent code unit after the first code unit "
                 "in the surrogate range, received \\ but no u.");
      char u16s[5] = { 0 };
      for (int i=0; i<4; ++i) {
        u16s[i] = static_cast<char>(istrm.get());
        assert_msg(std::isxdigit(u16s[i]),
                   "unexpected non-hexadecimal character `" << u16s[i] << "'.");
      }
      uint16_t u16s_val = std::stoul(u16s, nullptr, 16);
      assert_msg(u16s_val >= 0xdc00 && u16s_val <= 0xdfff,
                 "the second code unit, or low surrogate, should be in the "
                 "range [0xdc00, 0xdfff].");
      uint32_t u32_val = 0x10000 + (((u16_val - 0xd800) << 10) |
                                    (u16s_val - 0xdc00));
      ret += static_cast<char>(0xf0 | (u32_val >> 18));
      ret += static_cast<char>(0x80 | ((u32_val >> 12) & 0x3f));
      ret += static_cast<char>(0x80 | ((u32_val >> 6) & 0x3f));
      ret += static_cast<char>(0x80 | (u32_val & 0x3f));
    }
    return ret;
  } else {
    assert_msg(0, "unexpected escape sequence beginning with character `"
                  << c << "'.");
  }
  return string();
}

string
__retrieve_quoted_string(istream& istrm)
{
  char open_quote = istrm.get();
  assert_msg(open_quote == '"' || open_quote == '\'',
             "unexpected openquote character `" << open_quote << "'.");
  string ret;
  bool closed = false;
  while (istrm && !istrm.eof()) {
    char c = istrm.get();
    if (c == '\\') {
      ret += __retrieve_escaped_string(istrm);
    } else if (c == open_quote) {
      closed = true; break;
    } else {
      ret += c;
    }
  }
  assert_msg(closed, "missing closing quote character `" << open_quote << "'.");
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
// implementation class declarations

class JsonObjectImpl : public JsonObject {
public:
  JsonObjectImpl() {};
  JsonObjectImpl(const JsonObjectImpl&);
  JsonObjectImpl(JsonObjectImpl&&) noexcept;
  virtual ~JsonObjectImpl();
  JsonObject& operator=(const JsonObject&);
  JsonObject& operator=(JsonObject&&);

  void unlink_child_records(deque<JsonRecord*>&);

private:
  JsonRecordPtr        clone() const;
  void                 serialize(ostream&, const s_config_t&) const;

  pair<iterator, bool> insert(value_type&&);
  pair<iterator, bool> insert(string_view, JsonRecordPtr&&);
  pair<iterator, bool> insert(string_view, const JsonRecordPtr&);

  iterator             find(const string& key);
  const_iterator       find(const string& key) const;

  JsonRecordPtr&       at(const string& key);
  const JsonRecord*    at(const string& key) const;

  iterator             erase(const_iterator);
  size_t               erase(const string& key);

  iterator             begin() { return _data.begin(); };
  const_iterator       begin() const { return _data.begin(); };
  iterator             end() { return _data.end(); };
  const_iterator       end() const { return _data.end(); };

  void                 clear() { _data.clear(); _map.clear(); };

  size_t               count(const string& key) const
                         { return _map.count(key); };
  bool                 empty() const { return _data.empty(); };
  size_t               size() const { return _data.size(); };

  JsonRecordPtr&       operator[](const string& key);

  JsonObject&          as_object() { return *this; };
  const JsonObject&    as_object() const { return *this; };
  JsonArray&           as_array();
  const JsonArray&     as_array() const;
  JsonData&            as_data();
  const JsonData&      as_data() const;

private:
  list<value_type> _data;
  unordered_map<string, list<value_type>::iterator> _map;
};

class JsonArrayImpl : public JsonArray {
public:
  JsonArrayImpl() {};
  JsonArrayImpl(const JsonArrayImpl&);
  JsonArrayImpl(JsonArrayImpl&&) noexcept;
  JsonArray& operator=(const JsonArray&);
  JsonArray& operator=(JsonArray&&);
  virtual ~JsonArrayImpl();

  void unlink_child_records(deque<JsonRecord*>&);

private:
  JsonRecordPtr     clone() const;
  void              serialize(ostream&, const s_config_t&) const;

  void              push_back(JsonRecordPtr&&);
  void              push_back(const JsonRecordPtr&);

  iterator          begin()       { return _data.begin(); };
  const_iterator    begin() const { return _data.begin(); };
  iterator          end()         { return _data.end(); };
  const_iterator    end() const   { return _data.end(); };

  JsonRecordPtr&    at(size_t i)       { return _data.at(i); };
  const JsonRecord* at(size_t i) const { return _data.at(i).get(); };

  void              clear()       { _data.clear(); };

  bool              empty() const { return _data.empty(); };
  size_t            size() const  { return _data.size();  };

  JsonRecordPtr&    operator[](size_t i)       { return _data.at(i); };
  const JsonRecord* operator[](size_t i) const { return _data.at(i).get(); };

  JsonArray&        as_array() { return *this; };
  const JsonArray&  as_array() const { return *this; };
  JsonObject&       as_object();
  const JsonObject& as_object() const;
  JsonData&         as_data();
  const JsonData&   as_data() const;

private:
  vector<JsonRecordPtr> _data;
};

class JsonDataImpl final : public JsonData {
public:
  enum class NativeType : uint8_t {
    NONE = 0,
    FLOAT = 1,
    INT = 2,
    BOOL = 3,
    STRING = 4,
  };
  JsonDataImpl() : _native_type(NativeType::NONE) {
    _content.l = 0;
  };
  JsonDataImpl(double value) : _native_type(NativeType::FLOAT) {
    _content.d = value;
  };
  JsonDataImpl(int64_t value) : _native_type(NativeType::INT) {
    _content.l = value;
  };
  JsonDataImpl(uint64_t value) : _native_type(NativeType::INT) {
    _content.l = value;
  };
  JsonDataImpl(int value) : _native_type(NativeType::INT) {
    _content.l = value;
  };
  JsonDataImpl(unsigned value) : _native_type(NativeType::INT) {
    _content.l = value;
  };
  JsonDataImpl(string_view value) : _native_type(NativeType::STRING) {
    _content.s = new string(value);
  };
  JsonDataImpl(bool value) : _native_type(NativeType::BOOL) {
    _content.l = value ? 1 : 0;
  };
  JsonDataImpl(const JsonDataImpl&);
  JsonDataImpl(JsonDataImpl&&) noexcept;
  JsonData& operator=(const JsonData&);
  JsonData& operator=(JsonData&&) noexcept;
  ~JsonDataImpl() noexcept;

  friend JsonDataPtr make_json_data(istream&, const d_config_t&);
  friend void        write_json_data_text(ostream&, const JsonData*,
                                          const s_config_t&);

private:
  JsonRecordPtr      clone() const;
  void               serialize(ostream&, const s_config_t&) const;

  string             as_string() const;
  bool               as_bool() const;
  double             as_double() const;
  long long          as_int() const;
  unsigned long long as_unsigned() const;

  JsonData&          as_data() { return *this; };
  const JsonData&    as_data() const { return *this; };
  JsonObject&        as_object();
  const JsonObject&  as_object() const;
  JsonArray&         as_array();
  const JsonArray&   as_array() const;

private:
  union {
    uint64_t l;
    double   d;
    string*  s;
  } _content;
  NativeType _native_type;
};

////////////////////////////////////////////////////////////////////////////////

JsonObjectImpl::JsonObjectImpl(const JsonObjectImpl& src)
{
  for (auto& entry : src._data) {
    _data.push_back({ entry.first, entry.second->clone() });
    _map.insert({ entry.first, prev(_data.end()) });
  }
}

JsonObjectImpl::JsonObjectImpl(JsonObjectImpl&& src) noexcept
  : _data(std::move(src._data))
{
  src._data.clear();
  src._map.clear();
  for (auto it=_data.begin(); it!=_data.end(); ++it) {
    _map.insert({ it->first, it });
  }
}

JsonObject&
JsonObjectImpl::operator=(const JsonObject& src)
{
  _map.clear();
  _data.clear();
  const JsonObjectImpl& src_impl = static_cast<const JsonObjectImpl&>(src);
  for (auto& entry : src_impl._data) {
    _data.push_back({ entry.first, entry.second->clone() });
    _map.insert({ entry.first, prev(_data.end()) });
  }
  return *this;
}

JsonObject&
JsonObjectImpl::operator=(JsonObject&& src)
{
  _map.clear();
  _data.clear();
  JsonObjectImpl&& src_impl = static_cast<JsonObjectImpl&&>(src);
  _data = std::move(src_impl._data);
  for (auto it=_data.begin(); it!=_data.end(); ++it) {
    _map.insert({ it->first, it });
  }
  src_impl._data.clear();
  src_impl._map.clear();
  return *this;
}

JsonObjectImpl::~JsonObjectImpl()
{
  deque<JsonRecord*> ptrs;
  unlink_child_records(ptrs);
  while (ptrs.size()) {
    auto ptr = ptrs.front();
    ptrs.pop_front();
    switch (ptr->type()) {
    case JsonRecord::Type::OBJECT:
      static_cast<JsonObjectImpl*>(ptr)->unlink_child_records(ptrs);
      break;
    case JsonRecord::Type::ARRAY:
      static_cast<JsonArrayImpl*>(ptr)->unlink_child_records(ptrs);
      break;
    default:
      break;
    }
    delete ptr;
  }
}

void
JsonObjectImpl::unlink_child_records(deque<JsonRecord*>& ptrs)
{
  for (auto& entry : _data) {
    auto ptr = entry.second.release();
    if (ptr) { ptrs.push_back(ptr); }
  }
  _map.clear();
  _data.clear();
}

JsonRecordPtr
JsonObjectImpl::clone() const
{
  JsonObjectPtr ret = make_json_object();
  for (const auto& entry : _data) {
    ret->insert(entry.first, entry.second->clone());
  }
  return ret;
}

void
JsonObjectImpl::serialize(ostream& strm, const s_config_t& cfg) const
{
  write_json_text(strm, this, cfg);
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

JsonRecordPtr&
JsonObjectImpl::at(const string& key)
{
  return (*(_map.at(key))).second;
}

const JsonRecord*
JsonObjectImpl::at(const string& key) const
{
  return (*(_map.at(key))).second.get();
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

JsonRecordPtr&
JsonObjectImpl::operator[](const string& key)
{
  if (_map.count(key) == 0) {
    insert(value_type(key, unique_ptr<JsonRecord>()));
  }
  return at(key);
}

////////////////////////////////////////////////////////////////////////////////

JsonArrayImpl::JsonArrayImpl(const JsonArrayImpl& src)
{
  for (auto& entry : src._data) { _data.push_back(entry->clone()); }
}

JsonArrayImpl::JsonArrayImpl(JsonArrayImpl&& src) noexcept
  : _data(std::move(src._data))
{
  src._data.clear();
}

JsonArray&
JsonArrayImpl::operator=(const JsonArray& src)
{
  _data.clear();
  const JsonArrayImpl& src_impl = static_cast<const JsonArrayImpl&>(src);
  for (auto& entry : src_impl._data) { _data.push_back(entry->clone()); }
  return *this;
}

JsonArray&
JsonArrayImpl::operator=(JsonArray&& src)
{
  _data.clear();
  JsonArrayImpl&& src_impl = static_cast<JsonArrayImpl&&>(src);
  _data = std::move(src_impl._data);
  return *this;
}

void
JsonArrayImpl::unlink_child_records(deque<JsonRecord*>& ptrs)
{
  for (auto& item : _data) {
    auto ptr = item.release();
    if (ptr) { ptrs.push_back(ptr); }
  }
  _data.clear();
}

JsonArrayImpl::~JsonArrayImpl()
{
  deque<JsonRecord*> ptrs;
  unlink_child_records(ptrs);
  while (ptrs.size()) {
    auto ptr = ptrs.front();
    ptrs.pop_front();
    switch (ptr->type()) {
    case JsonRecord::Type::OBJECT:
      static_cast<JsonObjectImpl*>(ptr)->unlink_child_records(ptrs);
      break;
    case JsonRecord::Type::ARRAY:
      static_cast<JsonArrayImpl*>(ptr)->unlink_child_records(ptrs);
      break;
    default:
      break;
    }
    delete ptr;
  }
}

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
  write_json_text(strm, this, cfg);
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

JsonDataImpl::JsonDataImpl(const JsonDataImpl& src)
  : _native_type(src._native_type)
{
  switch (_native_type) {
  case NativeType::STRING:
    _content.s = new string(*src._content.s);
    break;
  case NativeType::FLOAT:
    _content.d = src._content.d;
    break;
  default:
    _content.l = src._content.l;
    break;
  }
}

JsonDataImpl::JsonDataImpl(JsonDataImpl&& src) noexcept
  : _native_type(src._native_type)
{
  switch (_native_type) {
  case NativeType::STRING:
    _content.s = src._content.s;
    src._content.s = nullptr;
    break;
  case NativeType::FLOAT:
    _content.d = src._content.d;
    src._content.d = 0.;
    break;
  default:
    _content.l = src._content.l;
    src._content.l = 0;
    break;
  }
  src._native_type = NativeType::NONE;
  src._content.l = 0;
}

JsonData&
JsonDataImpl::operator=(const JsonData& src)
{
  const JsonDataImpl& src_impl = static_cast<const JsonDataImpl&>(src);
  _native_type = src_impl._native_type;
  switch (_native_type) {
  case NativeType::STRING:
    _content.s = new string(*src_impl._content.s);
    break;
  case NativeType::FLOAT:
    _content.d = src_impl._content.d;
    break;
  default:
    _content.l = src_impl._content.l;
    break;
  }
  return *this;
}

JsonData&
JsonDataImpl::operator=(JsonData&& src) noexcept
{
  JsonDataImpl&& src_impl = static_cast<JsonDataImpl&&>(src);
  _native_type = src_impl._native_type;
  switch (_native_type) {
  case NativeType::STRING:
    _content.s = src_impl._content.s;
    src_impl._content.s = nullptr;
    break;
  case NativeType::FLOAT:
    _content.d = src_impl._content.d;
    src_impl._content.d = 0.;
    break;
  default:
    _content.l = src_impl._content.l;
    src_impl._content.l = 0;
    break;
  }
  src_impl._native_type = NativeType::NONE;
  src_impl._content.l = 0;
  return *this;
}

JsonDataImpl::~JsonDataImpl() noexcept
{
  if (_native_type == NativeType::STRING) {
    delete _content.s;
  }
}

JsonRecordPtr
JsonDataImpl::clone() const
{
  return make_unique<JsonDataImpl>(*this);
}

void
JsonDataImpl::serialize(ostream& strm, const s_config_t& cfg) const
{
  write_json_text(strm, this, cfg);
}

string
JsonDataImpl::as_string() const
{
  switch (_native_type) {
  case NativeType::NONE:
    return "null";
  case NativeType::BOOL:
    return _content.l ? "true" : "false";
  case NativeType::STRING:
    break;
  case NativeType::FLOAT:
    return to_string(_content.d);
  case NativeType::INT:
    return to_string(static_cast<int64_t>(_content.l));
  default:
    throw runtime_error("JsonData::as_string(): unknown native type.");
  }
  return *_content.s;
}

bool
JsonDataImpl::as_bool() const
{
  switch (_native_type) {
  case NativeType::NONE:
    return false;
  case NativeType::BOOL:
    return _content.l ? true : false;
  case NativeType::FLOAT:
    return _content.d != 0.;
  case NativeType::INT:
    return _content.l != 0;
  case NativeType::STRING:
    if (*_content.s == "true" || *_content.s == "True") { return true; }
    else if (*_content.s == "false" || *_content.s == "False") { return false; }
    return as_int() != 0;
  default:
    throw runtime_error("JsonData::as_bool(): unknown native type.");
  }
  return false;
}

double
JsonDataImpl::as_double() const
{
  switch (_native_type) {
  case NativeType::NONE:
    return 0.;
  case NativeType::BOOL:
    return _content.l ? 1. : 0.;
  case NativeType::FLOAT:
    return _content.d;
  case NativeType::INT:
    return static_cast<double>(static_cast<int64_t>(_content.l));
  case NativeType::STRING:
    return stod(*_content.s);
  default:
    throw runtime_error("JsonData::as_double(): unknown native type.");
  }
  return 0.;
}

long long
JsonDataImpl::as_int() const
{
  switch (_native_type) {
  case NativeType::NONE:
    return 0;
  case NativeType::BOOL:
    return _content.l ? 1 : 0;
  case NativeType::FLOAT:
    return static_cast<long long>(_content.d);
  case NativeType::INT:
    return static_cast<long long>(_content.l);
  case NativeType::STRING:
    return stoll(*_content.s);
  default:
    throw runtime_error("JsonData::as_int(): unknown native type.");
  }
  return 0;
}

unsigned long long
JsonDataImpl::as_unsigned() const
{
  switch (_native_type) {
  case NativeType::NONE:
    return 0;
  case NativeType::BOOL:
    return _content.l ? 1 : 0;
  case NativeType::FLOAT:
    return static_cast<uint64_t>(_content.d);
  case NativeType::INT:
    return static_cast<uint64_t>(_content.l);
  case NativeType::STRING:
    return stoull(*_content.s);
  default:
    throw runtime_error("JsonData::as_unsigned(): unknown native type.");
  }
  return 0;
}

////////////////////////////////////////////////////////////////////////////////

JsonObjectPtr
make_json_object()
{
  return make_unique<JsonObjectImpl>();
}

JsonArrayPtr
make_json_array()
{
  return make_unique<JsonArrayImpl>();
}
JsonArrayPtr
make_json_array(const JsonArray& src)
{
  const JsonArrayImpl& src_impl = static_cast<const JsonArrayImpl&>(src);
  return make_unique<JsonArrayImpl>(src_impl);
}
JsonArrayPtr
make_json_array(JsonArray&& src)
{
  JsonArrayImpl&& src_impl = static_cast<JsonArrayImpl&&>(src);
  return make_unique<JsonArrayImpl>(std::move(src_impl));
}

JsonDataPtr
make_json_data()
{
  return make_unique<JsonDataImpl>();
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
make_json_data<int64_t>(int64_t);
template JsonDataPtr
make_json_data<uint64_t>(uint64_t);
template JsonDataPtr
make_json_data<int>(int);
template JsonDataPtr
make_json_data<unsigned>(unsigned);
template JsonDataPtr
make_json_data<string_view>(string_view);
template JsonDataPtr
make_json_data<const string&>(const string&);
template JsonDataPtr
make_json_data<string>(string);

////////////////////////////////////////////////////////////////////////////////

#define DISABLE_CONVERSION(dest, Dest, Src)                                    \
  Json ## Dest & Json ## Src ## Impl::as_ ## dest()                            \
  { throw runtime_error("Json" #Src "::as_" #dest "() is not allowed."); }     \
  const Json ## Dest & Json ## Src ## Impl::as_ ## dest() const                \
  { throw runtime_error("Json" #Src "::as_" #dest "() is not allowed."); }

DISABLE_CONVERSION(array, Array, Object);
DISABLE_CONVERSION(data, Data, Object);
DISABLE_CONVERSION(object, Object, Array);
DISABLE_CONVERSION(data, Data, Array);
DISABLE_CONVERSION(object, Object, Data);
DISABLE_CONVERSION(array, Array, Data);

#undef DISABLE_CONVERSION

////////////////////////////////////////////////////////////////////////////////
// deserialization functions

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
    ret->_content.s = new string(__retrieve_quoted_string(istrm));
    ret->_native_type = JsonDataImpl::NativeType::STRING;
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
      ret->_content.l = 0;
    } else if (token == "true") {
      ret->_native_type = JsonDataImpl::NativeType::BOOL;
      ret->_content.l = 1;
    } else if (token == "false") {
      ret->_native_type = JsonDataImpl::NativeType::BOOL;
      ret->_content.l = 0;
    } else {
      if (token[0] == '+') {
        assert_msg(token.size() > 1, "unexpected token `+' without a number.");
        assert_msg(token[1] != '-', "unexpected token `+-'.");
        token = token.substr(1);
      }
      if (token.find(".") != string::npos) {
        ret->_native_type = JsonDataImpl::NativeType::FLOAT;
        size_t after_pos = 0;
        ret->_content.d = stod(token, &after_pos);
        assert_msg(after_pos == token.size(), "unexpected trailing characters "
                   "after floating point number.");
      } else {
        ret->_native_type = JsonDataImpl::NativeType::INT;
        size_t after_pos = 0;
        ret->_content.l = stoll(token, &after_pos, 0);
        assert_msg(after_pos == token.size(), "unexpected trailing characters "
                   "after integer number.");
      }
    }
  }
  return ret;
}

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

struct des_job_state_t {
  JsonDeserializeState state;
  JsonRecord* record;
  string active_key;
};

void
handle_array_open(istream& istrm, const d_config_t& cfg,
                  std::stack<des_job_state_t>& job_stack,
                  JsonRecordPtr& root_record)
{
  assert_msg(istrm.peek() == '[', "expecting opening square bracket.");
  istrm.get();  /* consume the opening square bracket */
  unique_ptr<JsonArrayImpl> ret = make_unique<JsonArrayImpl>();
  des_job_state_t* active_job = nullptr;
  if (!job_stack.empty()) active_job = &(job_stack.top());
  job_stack.push({ JsonDeserializeState::ARRAY_ENTRY, ret.get(), string() });
  if (active_job) {
    switch (active_job->state) {
    case JsonDeserializeState::ARRAY_ENTRY:
      static_cast<JsonArray*>(active_job->record)
        ->push_back(std::move(ret));
      active_job->state = JsonDeserializeState::ARRAY_COMMA;
      break;
    case JsonDeserializeState::OBJECT_VALUE:
      static_cast<JsonObject*>(active_job->record)
        ->insert(active_job->active_key, std::move(ret));
      active_job->state = JsonDeserializeState::OBJECT_COMMA;
      break;
    default:
      assert_msg(0, "unexpected state.");
    }
  } else {
    root_record = std::move(ret);
  }
}

void
handle_object_open(istream& istrm, const d_config_t& cfg,
                   std::stack<des_job_state_t>& job_stack,
                   JsonRecordPtr& root_record)
{
  assert_msg(istrm.peek() == '{', "expecting opening curly brace.");
  istrm.get();  /* consume the opening curly brace */
  unique_ptr<JsonObjectImpl> ret = make_unique<JsonObjectImpl>();
  des_job_state_t* active_job = nullptr;
  if (!job_stack.empty()) active_job = &(job_stack.top());
  job_stack.push({ JsonDeserializeState::OBJECT_KEY, ret.get(), string() });
  if (active_job) {
    switch (active_job->state) {
    case JsonDeserializeState::OBJECT_VALUE:
      static_cast<JsonObject*>(active_job->record)
        ->insert(active_job->active_key, std::move(ret));
      active_job->state = JsonDeserializeState::OBJECT_COMMA;
      break;
    case JsonDeserializeState::ARRAY_ENTRY:
      static_cast<JsonArray*>(active_job->record)
        ->push_back(std::move(ret));
      active_job->state = JsonDeserializeState::ARRAY_COMMA;
      break;
    default:
      assert_msg(0, "unexpected state.");
    }
  } else {
    root_record = std::move(ret);
  }
}

void
handle_array_close(istream& istrm, const d_config_t& cfg,
                   std::stack<des_job_state_t>& job_stack,
                   JsonRecordPtr& root_record)
{
  assert_msg(istrm.peek() == ']', "expecting closing square bracket.");
  istrm.get();  /* consume the closing square bracket */
  assert_msg(!job_stack.empty(), "unexpected closing square bracket outside of "
                                 "array.");
  auto& active_job = job_stack.top();
  assert_msg(active_job.state == JsonDeserializeState::ARRAY_ENTRY ||
             active_job.state == JsonDeserializeState::ARRAY_COMMA,
             "unexpected closing square bracket in json array.");
  job_stack.pop();
}

void
handle_object_close(istream& istrm, const d_config_t& cfg,
                    std::stack<des_job_state_t>& job_stack,
                    JsonRecordPtr& root_record)
{
  assert_msg(istrm.peek() == '}', "expecting closing curly brace.");
  istrm.get();  /* consume the closing curly brace */
  assert_msg(!job_stack.empty(), "unexpected closing curly brace outside of "
                                 "object.");
  auto& active_job = job_stack.top();
  assert_msg(active_job.state == JsonDeserializeState::OBJECT_KEY ||
             active_job.state == JsonDeserializeState::OBJECT_COMMA,
             "unexpected closing curly brace in json object.");
  job_stack.pop();
}

void
handle_object_key(istream& istrm, const d_config_t& cfg,
                  std::stack<des_job_state_t>& job_stack,
                  JsonRecordPtr& root_record)
{
  assert_msg(!job_stack.empty(), "unexpected object key outside of "
                                 "object.");
  auto& active_job = job_stack.top();
  assert_msg(active_job.state == JsonDeserializeState::OBJECT_KEY,
             "unexpected object key in json object.");
  char c = istrm.peek();
  if (c == '"' || c == '\'') {
    active_job.active_key = __retrieve_quoted_string(istrm);
  } else {
    active_job.active_key.clear();
    while (istrm && !istrm.eof()) {
      c = istrm.peek();
      if (std::isspace(c) || c == ':' || c == ',' || c == '}') { break; }
      active_job.active_key += static_cast<char>(istrm.get());
    }
  }
  assert_msg(active_job.active_key.length(),
             "unexpected non-json characters in json object key.");
  active_job.state = JsonDeserializeState::OBJECT_COLON;
}

void
handle_comma(istream& istrm, const d_config_t& cfg,
             std::stack<des_job_state_t>& job_stack,
             JsonRecordPtr& root_record)
{
  assert_msg(istrm.peek() == ',', "expecting comma.");
  istrm.get();  /* consume the comma */
  assert_msg(!job_stack.empty(), "unexpected comma outside of "
                                 "array or object.");
  auto& active_job = job_stack.top();
  switch (active_job.state) {
  case JsonDeserializeState::ARRAY_COMMA:
    active_job.state = JsonDeserializeState::ARRAY_ENTRY;
    break;
  case JsonDeserializeState::OBJECT_COMMA:
    active_job.state = JsonDeserializeState::OBJECT_KEY;
    break;
  default:
    assert_msg(0, "unexpected state.");
  };
}

void
handle_colon(istream& istrm, const d_config_t& cfg,
             std::stack<des_job_state_t>& job_stack,
             JsonRecordPtr& root_record)
{
  assert_msg(istrm.peek() == ':', "expecting colon.");
  istrm.get();  /* consume the colon */
  assert_msg(!job_stack.empty(), "unexpected colon outside of "
                                 "object.");
  auto& active_job = job_stack.top();
  assert_msg(active_job.state == JsonDeserializeState::OBJECT_COLON,
             "unexpected colon in json object.");
  active_job.state = JsonDeserializeState::OBJECT_VALUE;
}

void
handle_data(istream& istrm, const d_config_t& cfg,
            std::stack<des_job_state_t>& job_stack,
            JsonRecordPtr& root_record)
{
  JsonRecordPtr ret = make_json_data(istrm, cfg);
  des_job_state_t* active_job = nullptr;
  if (!job_stack.empty()) { active_job = &(job_stack.top()); }
  if (active_job) {
    switch (active_job->state) {
    case JsonDeserializeState::ARRAY_ENTRY:
      static_cast<JsonArray*>(active_job->record)->push_back(std::move(ret));
      active_job->state = JsonDeserializeState::ARRAY_COMMA;
      break;
    case JsonDeserializeState::OBJECT_VALUE:
      static_cast<JsonObject*>(active_job->record)
        ->insert(active_job->active_key, std::move(ret));
      active_job->state = JsonDeserializeState::OBJECT_COMMA;
      break;
    default:
      assert_msg(0, "unexpected state.");
    }
  } else {
    root_record = std::move(ret);
  }
}

static const unordered_map<char, function<void(istream&, const d_config_t&,
                                               std::stack<des_job_state_t>&,
                                               JsonRecordPtr&)>>
__des_handlers = {
    { '{', handle_object_open },
    { '[', handle_array_open },
    { ']', handle_array_close },
    { '}', handle_object_close },
    { ':', handle_colon },
    { ',', handle_comma }
  };

JsonRecordPtr
make_json_record(istream& istrm, const d_config_t& cfg)
{
  __skip_no_parse(istrm);
  JsonRecordPtr ret;
  if (istrm.eof()) { return ret; }
  std::stack<des_job_state_t> job_stack;
  while (istrm && !istrm.eof()) {
    __skip_no_parse(istrm);
    if (istrm.eof()) { break; }
    char c = istrm.peek();
    auto it = __des_handlers.find(c);
    if (it != __des_handlers.end()) {
      it->second(istrm, cfg, job_stack, ret);
    } else {
      if (!job_stack.empty() &&
          job_stack.top().state == JsonDeserializeState::OBJECT_KEY)
      { handle_object_key(istrm, cfg, job_stack, ret); }
      else { handle_data(istrm, cfg, job_stack, ret); }
    }
  }
  return ret;
}

////////////////////////////////////////////////////////////////////////////////
// serialization functions

void
write_json_data_text(ostream& ostrm, const JsonData* data,
                     const s_config_t& cfg)
{
  const JsonDataImpl* data_impl = static_cast<const JsonDataImpl*>(data);
  switch (data_impl->_native_type) {
  case JsonDataImpl::NativeType::NONE:
    ostrm << "null";
    break;
  case JsonDataImpl::NativeType::BOOL:
    ostrm << (data_impl->_content.l ? "true" : "false");
    break;
  case JsonDataImpl::NativeType::INT:
    ostrm << static_cast<int64_t>(data_impl->_content.l);
    break;
  case JsonDataImpl::NativeType::FLOAT:
    ostrm << defaultfloat << setprecision(10) << data_impl->_content.d;
    break;
  case JsonDataImpl::NativeType::STRING:
    ostrm << '"' << __escape_string(*(data_impl->_content.s)) << '"';
    break;
  default:
    assert_msg(0, "corrupted JsonData native data type.");
  }
}

struct ser_job_state_t {
  const JsonRecord* record;
  variant<monostate, JsonObject::const_iterator, JsonArray::const_iterator> it;
  ser_job_state_t(const JsonRecord* r_) : record(r_), it(monostate()) {};
  ser_job_state_t() : record(nullptr), it(monostate()) {};
};

void
write_json_text(ostream& ostrm, const JsonRecord* record, const s_config_t& cfg)
{
  std::stack<ser_job_state_t> job_stack;
  job_stack.push({ record });
  while (!job_stack.empty()) {
    auto& active_job = job_stack.top();
    int curr_indent = cfg.global_indentation
                        + cfg.indentation_width * (job_stack.size() - 1);
    switch (active_job.record->type()) {
    case JsonRecord::Type::OBJECT:
      {
        const JsonObject& obj = active_job.record->as_object();
        if (holds_alternative<monostate>(active_job.it)) {
          active_job.it = obj.begin();
          ostrm << '{' << endl;
        }
        auto& it = get<JsonObject::const_iterator>(active_job.it);
        if (it == obj.end()) {
          ostrm << endl;
          __put_spaces(ostrm, curr_indent);
          ostrm << '}';
          job_stack.pop();
        } else {
          if (it != obj.begin()) { ostrm << ',' << endl; }
          __put_spaces(ostrm, curr_indent + cfg.indentation_width);
          ostrm << '"' << it->first << '"' << " : ";
          job_stack.push({ it->second.get() });
          ++ it;
        }
      }
      break;
    case JsonRecord::Type::ARRAY:
      {
        const JsonArray& arr = active_job.record->as_array();
        if (holds_alternative<monostate>(active_job.it)) {
          active_job.it = arr.begin();
          ostrm << '[' << endl;
        }
        auto& it = get<JsonArray::const_iterator>(active_job.it);
        if (it == arr.end()) {
          ostrm << endl;
          __put_spaces(ostrm, curr_indent);
          ostrm << ']';
          job_stack.pop();
        } else {
          if (it != arr.begin()) { ostrm << ',' << endl; }
          __put_spaces(ostrm, curr_indent + cfg.indentation_width);
          job_stack.push({ it->get() });
          ++ it;
        }
      }
      break;
    case JsonRecord::Type::DATA:
      write_json_data_text(ostrm, &active_job.record->as_data(), cfg);
      job_stack.pop();
      break;
    default:
      assert_msg(0, "corrupted json record type.");
    }
  }
}

void
write_json_text(ostream& ostrm, const JsonRecordPtr& record,
                const s_config_t& cfg)
{
  write_json_text(ostrm, record.get(), cfg);
}
void
write_json_text(ostream& ostrm, const JsonObjectPtr& record,
                const s_config_t& cfg)
{
  write_json_text(ostrm, record.get(), cfg);
}
void
write_json_text(ostream& ostrm, const JsonArrayPtr& record,
                const s_config_t& cfg)
{
  write_json_text(ostrm, record.get(), cfg);
}
void
write_json_text(ostream& ostrm, const JsonDataPtr& record,
                const s_config_t& cfg)
{
  write_json_data_text(ostrm, record.get(), cfg);
}

}
