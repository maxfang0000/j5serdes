#include "j5serdes.h"

namespace J5Serdes {

using namespace std;

class JsonObjectImpl : public JsonObject {
public:
  JsonObjectImpl() {};
  virtual ~JsonObjectImpl() {};

private:
  pair<iterator, bool>      insert(value_type&);
  pair<iterator, bool>      insert(value_type&&);

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

pair<JsonObject::iterator, bool>
JsonObjectImpl::insert(value_type& v)
{
  if (_map.count(v.first)) { return { _data.end(), false }; }
  _data.push_back({ v.first, std::move(v.second) });
  auto it = prev(_data.end());
  _map.insert({ it->first, it });
  return { it, true };
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
  void           push_back(JsonRecordPtr&&);

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

void
JsonArrayImpl::push_back(JsonRecordPtr&& v)
{
  _data.push_back(std::move(v));
}

////////////////////////////////////////////////////////////////////////////////

class JsonDataImpl : public JsonData {
public:
  JsonDataImpl() {};
  virtual ~JsonDataImpl() {};

private:

private:
};

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

}

