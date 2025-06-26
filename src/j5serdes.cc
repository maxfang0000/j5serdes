#include "j5serdes.h"

namespace J5Serdes {

using namespace std;

////////////////////////////////////////////////////////////////////////////////

static void
__put_spaces(ostream& os, int n)
{
  for (int i=0; i<n; ++i) { os << ' '; }
}

////////////////////////////////////////////////////////////////////////////////

class JsonObjectImpl : public JsonObject {
public:
  JsonObjectImpl() {};
  virtual ~JsonObjectImpl() {};

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
    if (first) { first = false; }
    else       { strm << ',' << endl; }
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

private:
  JsonRecordPtr  clone() const;
  void           serialize(ostream&, const s_config_t&) const;

  void           push_back(JsonRecordPtr&&);
  void           push_back(const JsonRecordPtr&);

  iterator       begin()       { return _data.begin(); };
  const_iterator begin() const { return _data.begin(); };
  iterator       end()         { return _data.end(); };
  const_iterator end() const   { return _data.end(); };

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
      strm << '"' << _string_buf << '"';
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

}

