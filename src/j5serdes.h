#include <iostream>
#include <list>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace J5Serdes {

class JsonRecord;
class JsonObject;
class JsonArray;
class JsonData;

typedef std::unique_ptr<JsonRecord> JsonRecordPtr;
typedef std::unique_ptr<JsonObject> JsonObjectPtr;
typedef std::unique_ptr<JsonArray>  JsonArrayPtr;
typedef std::unique_ptr<JsonData>   JsonDataPtr;

struct s_config_t
{
  bool strict_json;
  s_config_t()
    : strict_json(false)
  {};
};

struct d_config_t
{
  int  global_indentation;
  int  indentation_width;
  int  maximum_width;
  bool strict_json;
  d_config_t()
    : global_indentation(0),
      indentation_width(2),
      maximum_width(-1),
      strict_json(false)
  {};
};

JsonObjectPtr
make_json_object();

JsonObjectPtr
make_json_object(std::istream&, const s_config_t& cfg = s_config_t());

JsonArrayPtr
make_json_array();

class JsonRecord {
public:
  enum class Type : unsigned int { OBJECT = 0, ARRAY = 1, DATA = 2 };
  JsonRecord() {};
  virtual ~JsonRecord() {};
  virtual Type type() const = 0;
};

JsonObject&
to_object(JsonRecord&);

JsonArray&
to_array(JsonRecord&);

class JsonObject : public JsonRecord {
public:
  JsonObject() {};
  virtual ~JsonObject() {};
  JsonRecord::Type type() const { return JsonRecord::Type::OBJECT; }

  typedef std::pair<const std::string, JsonRecordPtr> value_type;
  typedef std::list<value_type>::iterator             iterator;
  typedef std::list<value_type>::const_iterator       const_iterator;

  virtual std::pair<iterator, bool> insert(value_type&) = 0;
  virtual std::pair<iterator, bool> insert(value_type&&) = 0;

  virtual iterator                  find(const std::string& key) = 0;
  virtual const_iterator            find(const std::string& key) const = 0;

  virtual iterator                  erase(const_iterator) = 0;
  virtual size_t                    erase(const std::string& key) = 0;

  virtual iterator                  begin() = 0;
  virtual const_iterator            begin() const = 0;
  virtual iterator                  end() = 0;
  virtual const_iterator            end() const = 0;

  virtual void                      clear() = 0;

  virtual size_t                    count(const std::string& key) const = 0;
  virtual bool                      empty() const = 0;
  virtual size_t                    size() const = 0;
};

class JsonArray {
public:
  JsonArray() {};
  virtual ~JsonArray() {};
  JsonRecord::Type type() const { return JsonRecord::Type::ARRAY; }

  typedef std::vector<JsonRecordPtr>::iterator       iterator;
  typedef std::vector<JsonRecordPtr>::const_iterator const_iterator;

  virtual void              push_back(JsonRecordPtr&&) = 0;

  virtual iterator          begin() = 0;
  virtual const_iterator    begin() const = 0;
  virtual iterator          end() = 0;
  virtual const_iterator    end() const = 0;

  virtual void              clear() = 0;

  virtual bool              empty() const = 0;
  virtual size_t            size() const = 0;
};

class JsonData {
public:
  JsonData() {};
  virtual ~JsonData() {};
  JsonRecord::Type type() const { return JsonRecord::Type::DATA; }
};

}

