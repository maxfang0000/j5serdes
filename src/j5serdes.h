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

struct d_config_t
{
  bool strict_json;
  d_config_t()
    : strict_json(false)
  {};
};

struct s_config_t
{
  int  global_indentation;
  int  indentation_width;
  bool strict_json;
  s_config_t()
    : global_indentation(0),
      indentation_width(2),
      strict_json(false)
  {};
};

JsonRecordPtr
make_json_record(std::istream&, const d_config_t& cfg = d_config_t());

JsonObjectPtr
make_json_object();
JsonObjectPtr
make_json_object(const JsonObject&);
JsonObjectPtr
make_json_object(JsonObject&&);

JsonArrayPtr
make_json_array();
JsonArrayPtr
make_json_array(const JsonArray&);
JsonArrayPtr
make_json_array(JsonArray&&);

JsonDataPtr
make_json_data();

template<typename T>
JsonDataPtr
make_json_data(T value);
template<>
JsonDataPtr
make_json_data<const char*>(const char*);

void
write_json_text(std::ostream&, const JsonRecord*,
                const s_config_t& cfg = s_config_t());

/* The template enables direct use with JsonRecordPtr or derived
 * unique pointers.
 */
template <typename T>
void write_json_text(std::ostream&, const std::unique_ptr<T>&,
                     const s_config_t& cfg = s_config_t());


class JsonRecord {
public:
  enum class Type : unsigned int { OBJECT = 0, ARRAY = 1, DATA = 2 };
  virtual ~JsonRecord() = default;
  virtual Type type() const = 0;
  virtual JsonRecordPtr clone() const = 0;
  virtual JsonArray& as_array();
  virtual JsonObject& as_object();
  virtual JsonData& as_data();
  virtual const JsonArray& as_array() const;
  virtual const JsonObject& as_object() const;
  virtual const JsonData& as_data() const;
};

class JsonObject : public JsonRecord {
public:
  virtual ~JsonObject() = default;
  virtual JsonObject& operator=(const JsonObject&) = 0;
  virtual JsonObject& operator=(JsonObject&&) = 0;
  JsonRecord::Type type() const { return JsonRecord::Type::OBJECT; };

  typedef std::pair<std::string, JsonRecordPtr> value_type;
  typedef std::list<value_type>::iterator       iterator;
  typedef std::list<value_type>::const_iterator const_iterator;

  virtual std::pair<iterator, bool> insert(value_type&&) = 0;
  virtual std::pair<iterator, bool> insert(std::string_view,
                                           JsonRecordPtr&&) = 0;
  virtual std::pair<iterator, bool> insert(std::string_view,
                                           const JsonRecordPtr&) = 0;

  virtual iterator                  find(const std::string& key) = 0;
  virtual const_iterator            find(const std::string& key) const = 0;

  virtual JsonRecordPtr&            at(const std::string& key) = 0;
  virtual const JsonRecord*         at(const std::string& key) const = 0;

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

  virtual JsonRecordPtr&            operator[](const std::string& key) = 0;
};

class JsonArray : public JsonRecord {
public:
  virtual ~JsonArray() = default;
  virtual JsonArray& operator=(const JsonArray&) = 0;
  virtual JsonArray& operator=(JsonArray&&) = 0;
  JsonRecord::Type type() const { return JsonRecord::Type::ARRAY; };

  typedef std::vector<JsonRecordPtr>::iterator       iterator;
  typedef std::vector<JsonRecordPtr>::const_iterator const_iterator;

  virtual void              push_back(JsonRecordPtr&&) = 0;
  virtual void              push_back(const JsonRecordPtr&) = 0;

  virtual iterator          begin() = 0;
  virtual const_iterator    begin() const = 0;
  virtual iterator          end() = 0;
  virtual const_iterator    end() const = 0;

  virtual JsonRecordPtr&    at(size_t) = 0;
  virtual const JsonRecord* at(size_t) const = 0;

  virtual void              clear() = 0;

  virtual bool              empty() const = 0;
  virtual size_t            size() const = 0;

  virtual JsonRecordPtr&    operator[](size_t) = 0;
  virtual const JsonRecord* operator[](size_t) const = 0;
};

class JsonData : public JsonRecord {
public:
  virtual ~JsonData() = default;
  JsonRecord::Type type() const { return JsonRecord::Type::DATA; };

  virtual std::string        as_string() const = 0;
  virtual bool               as_bool() const = 0;
  virtual double             as_double() const = 0;
  virtual long long          as_int() const = 0;
  virtual unsigned long long as_unsigned() const = 0;
};

}
