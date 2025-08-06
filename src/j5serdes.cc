#include "j5serdes.h"
#include <deque>
#include <functional>
#include <sstream>
#include <stack>
#include <unordered_map>

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

class DestructionHelperIntf {
public:
  virtual ~DestructionHelperIntf() {};
  virtual void unlink_child_records(deque<DestructionHelperIntf*>&) = 0;
};

class JsonObjectImpl : public JsonObject, public DestructionHelperIntf {
public:
  JsonObjectImpl() {};
  virtual ~JsonObjectImpl();

  friend JsonObjectPtr make_json_object(istream&, const d_config_t&);

private:
  JsonRecordPtr        clone() const;
  void                 serialize(ostream&, const s_config_t&) const;

  pair<iterator, bool> insert(value_type&&);
  pair<iterator, bool> insert(string_view, JsonRecordPtr&&);
  pair<iterator, bool> insert(string_view, const JsonRecordPtr&);

  iterator             find(const string& key);
  const_iterator       find(const string& key) const;

  JsonRecord&          at(const string& key);
  const JsonRecord&    at(const string& key) const;

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

  void                 unlink_child_records(deque<DestructionHelperIntf*>&);

private:
  list<value_type> _data;
  unordered_map<string, list<value_type>::iterator> _map;
};

class JsonArrayImpl : public JsonArray, public DestructionHelperIntf {
public:
  JsonArrayImpl() {};
  virtual ~JsonArrayImpl();

  friend JsonArrayPtr make_json_array(istream&, const d_config_t&);

private:
  JsonRecordPtr     clone() const;
  void              serialize(ostream&, const s_config_t&) const;

  void              push_back(JsonRecordPtr&&);
  void              push_back(const JsonRecordPtr&);

  iterator          begin()       { return _data.begin(); };
  const_iterator    begin() const { return _data.begin(); };
  iterator          end()         { return _data.end(); };
  const_iterator    end() const   { return _data.end(); };

  JsonRecord&       at(size_t idx)       { return *_data.at(idx); };
  const JsonRecord& at(size_t idx) const { return *_data.at(idx); };

  void              clear()       { _data.clear(); };

  bool              empty() const { return _data.empty(); };
  size_t            size() const  { return _data.size();  };

  void              unlink_child_records(deque<DestructionHelperIntf*>&);

private:
  vector<JsonRecordPtr> _data;
};

class JsonDataImpl : public JsonData, public DestructionHelperIntf {
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

  void          unlink_child_records(deque<DestructionHelperIntf*>&) {};

private:
  uint64_t   _long_buf;
  string     _string_buf;
  NativeType _native_type;
};

////////////////////////////////////////////////////////////////////////////////

JsonObjectImpl::~JsonObjectImpl()
{
  deque<DestructionHelperIntf*> ptrs;
  unlink_child_records(ptrs);
  while (ptrs.size()) {
    auto ptr = ptrs.front();
    ptrs.pop_front();
    ptr->unlink_child_records(ptrs);
    delete ptr;
  }
}

void
__jrecord_to_dhelpter(JsonRecord* p, deque<DestructionHelperIntf*>& ptrs)
{
  DestructionHelperIntf* dptr = nullptr;
  switch (p->type()) {
  case JsonRecord::Type::OBJECT:
    dptr = static_cast<DestructionHelperIntf*>(static_cast<JsonObjectImpl*>(p));
    break;
  case JsonRecord::Type::ARRAY:
    dptr = static_cast<DestructionHelperIntf*>(static_cast<JsonArrayImpl*>(p));
    break;
  case JsonRecord::Type::DATA:
    dptr = static_cast<DestructionHelperIntf*>(static_cast<JsonDataImpl*>(p));
    break;
  default:
    break;
  };
  if (dptr) { ptrs.push_back(dptr); }
}

void
JsonObjectImpl::unlink_child_records(deque<DestructionHelperIntf*>& ptrs)
{
  for (auto& entry : _data) {
    auto ptr = entry.second.release();
    __jrecord_to_dhelpter(ptr, ptrs);
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

JsonRecord&
JsonObjectImpl::at(const string& key)
{
  return *((*(_map.at(key))).second);
}

const JsonRecord&
JsonObjectImpl::at(const string& key) const
{
  return *((*(_map.at(key))).second);
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

void
JsonArrayImpl::unlink_child_records(deque<DestructionHelperIntf*>& ptrs)
{
  for (auto& item : _data) {
    auto ptr = item.release();
    __jrecord_to_dhelpter(ptr, ptrs);
  }
  _data.clear();
}

JsonArrayImpl::~JsonArrayImpl()
{
  deque<DestructionHelperIntf*> ptrs;
  unlink_child_records(ptrs);
  while (ptrs.size()) {
    auto ptr = ptrs.front();
    ptrs.pop_front();
    ptr->unlink_child_records(ptrs);
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
    if (__des_handlers.count(c)) {
      __des_handlers.at(c)(istrm, cfg, job_stack, ret);
    } else {
      if (!job_stack.empty() &&
          job_stack.top().state == JsonDeserializeState::OBJECT_KEY)
      { handle_object_key(istrm, cfg, job_stack, ret); }
      else { handle_data(istrm, cfg, job_stack, ret); }
    }
  }
  return ret;
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
        assert_msg(token.size() > 1, "unexpected token `+' without a number.");
        assert_msg(token[1] != '-', "unexpected token `+-'.");
        token = token.substr(1);
      }
      if (token.find(".") != string::npos) {
        ret->_native_type = JsonDataImpl::NativeType::FLOAT;
        size_t after_pos = 0;
        double d = stod(token, &after_pos);
        assert_msg(after_pos == token.size(), "unexpected trailing characters "
                   "after floating point number.");
        ret->_long_buf = *(reinterpret_cast<const uint64_t*>(&d));
      } else {
        ret->_native_type = JsonDataImpl::NativeType::INT;
        size_t after_pos = 0;
        int64_t i = stoll(token, &after_pos, 0);
        assert_msg(after_pos == token.size(), "unexpected trailing characters "
                   "after integer number.");
        ret->_long_buf = *(reinterpret_cast<const uint64_t*>(&i));
      }
    }
  }
  return ret;
}

}
