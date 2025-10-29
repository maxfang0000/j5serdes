#include "minitest.h"
#include "j5serdes.h"
#include <cstring>
#include <iostream>
#include <streambuf>
#include <istream>
#include <sstream>

using namespace J5Serdes;
using namespace std;

TEST(JsonObject, basic_ops)
{
  try {
    JsonRecordPtr two = make_json_data(std::string("two"));
    auto root = make_json_object();
    auto array = make_json_array();
    array->push_back(make_json_data(true));
    array->push_back(two);
    array->push_back(make_json_data("three"));
    root->insert({ "one"  , make_json_data(1.) });
    root->insert(  "two"  , two );
    root->insert(  "three", std::move(array) );
    ASSERT_TRUE(root->size() == 3);
    ASSERT_TRUE((*root)["three"]->as_array().size() == 3);
    write_json_text(cout, root);
    cout << endl;
    root->at("one") = make_json_data("ONE");
    (*root)["two"] = make_json_data(2.);
    (*root)["four"] = make_json_array();
    (*root)["four"]->as_array().push_back(make_json_data(1.0));
    (*root)["four"]->as_array().push_back(make_json_data(2.0));
    (*root)["four"]->as_array().push_back(make_json_data(3.0));
    (*root)["four"]->as_array().push_back(make_json_data(4.0));
    (*root)["four"]->as_array().push_back(make_json_data());
    auto object = make_json_object();
    object->insert({ "a", make_json_data(1.) });
    object->insert({ "b", make_json_array() });
    object->insert({ "c", make_json_object() });
    (*object)["c"]->as_object().insert({ "c1", make_json_data(1) });
    (*root)["five"] = object->clone();
    write_json_text(cout, root);
    cout << endl;
    write_json_text(cout, object);
    cout << endl;
    auto object2 = make_json_object();
    *object2 = *object;
    write_json_text(cout, object2);
    cout << endl;
    ASSERT_TRUE(object2->size() == object->size());
    auto object3 = make_json_object();
    *object3 = std::move(*object2);
    ASSERT_TRUE(object3->size() == object->size());
    ASSERT_TRUE(object2->size() == 0);
  } catch (const runtime_error& e) {
    cout << "error: " << e.what() << endl;
    ASSERT_TRUE(false);
  }
}

class string_view_istream : public istream {
public:
  string_view_istream(string_view sv) : istream(nullptr), buffer(sv)
  { rdbuf(&buffer); };
private:
  class string_view_streambuf : public streambuf {
  public:
    string_view_streambuf(string_view sv)
    {
      char* begin = const_cast<char*>(sv.data());
      char* end   = begin + sv.size();
      setg(begin, begin, end);
    };
  };
  string_view_streambuf buffer;
};

TEST(JsonObject, data_deserialize1)
{
  const char* json_str = R"(
    /* comment here */ "double quoted string with escape \t"
)";
  string_view sv(json_str);
  string_view_istream istrm(sv);
  cout << "original:" << endl << json_str << endl;
  auto data = make_json_record(istrm, d_config_t());
  cout << "parsed and serialized:" << endl;
  write_json_text(cout, data);
  cout << endl;
}

TEST(JsonObject, data_deserialize2)
{
  const char* json_str = R"(
    -3.141592653589793238462643
)";
  string_view sv(json_str);
  string_view_istream istrm(sv);
  cout << "original:" << endl << json_str << endl;
  auto data = make_json_record(istrm, d_config_t());
  cout << "parsed and serialized:" << endl;
  write_json_text(cout, data);
  cout << endl;
}

TEST(JsonObject, data_deserialize3)
{
  const char* json_str = R"(
    +0x7f
)";
  string_view sv(json_str);
  string_view_istream istrm(sv);
  cout << "original:" << endl << json_str << endl;
  auto data = make_json_record(istrm, d_config_t());
  cout << "parsed and serialized:" << endl;
  write_json_text(cout, data);
  cout << endl;
}

TEST(JsonObject, array_deserialize1)
{
  const char* json_str = R"(
    [ 1.00001, [ 2, 3, 4 ], '5', "6", 0x7, +8, 9, '0x"A"' ]
)";
  string_view sv(json_str);
  string_view_istream istrm(sv);
  cout << "original:" << endl << json_str << endl;
  auto array = make_json_record(istrm, d_config_t());
  cout << "parsed and serialized:" << endl;
  write_json_text(cout, array);
  cout << endl;
}

TEST(JsonObject, object_deserialize1)
{
  const char* json_str = R"(
    { "one": 1,
      "two": { item1 : 0.1, 'item2' : "b" },
      "three": [ '1', 2, "san" ] }
)";
  string_view sv(json_str);
  string_view_istream istrm(sv);
  cout << "original:" << endl << json_str << endl;
  auto object = make_json_record(istrm, d_config_t());
  cout << "parsed and serialized:" << endl;
  write_json_text(cout, object);
  cout << endl;
}

TEST(JsonObject, object_deserialize2)
{
  const char* json_str = R"(
    { "one": 1,
      'two': { item1 : 0.3125, 'item2' : "b" },
      three: [ '1', 2, "san" ], }
)";
  string_view sv(json_str);
  string_view_istream istrm(sv);
  cout << "original:" << endl << json_str << endl;
  try {
    auto record = make_json_record(istrm);
    ASSERT_TRUE(record->type() == JsonRecord::Type::OBJECT);
    auto& object = record->as_object();
    auto it3 = object.find("three");
    ASSERT_TRUE(it3 != object.end());
    ASSERT_TRUE(it3->second->type() == JsonRecord::Type::ARRAY);
    ASSERT_TRUE(object.at("two")->type() == JsonRecord::Type::OBJECT);
    auto& object2 = object.at("two")->as_object();
    auto& ent21 = object2.at("item1")->as_data();
    ASSERT_TRUE(ent21.type() == JsonRecord::Type::DATA);
    ASSERT_TRUE(ent21.as_double() == 0.3125);
    string str21 = ent21.as_string();
    ASSERT_TRUE(strncmp(str21.c_str(), "0.3125", 6) == 0);
    ASSERT_TRUE(ent21.as_bool() == true);
    ASSERT_TRUE(ent21.as_int() == 0);
    ASSERT_TRUE(ent21.as_unsigned() == 0);
    auto& array = it3->second->as_array();
    ASSERT_TRUE(array.size() == 3);
    ASSERT_TRUE(array[0]->type() == JsonRecord::Type::DATA);
    ASSERT_TRUE(array[1]->type() == JsonRecord::Type::DATA);
    cout << "parsed and serialized:" << endl;
    write_json_text(cout, record);
    cout << endl;
  } catch (const runtime_error& e) {
    cout << "error: " << e.what() << endl;
    ASSERT_TRUE(false);
  }
}

TEST(JsonObject, array_deserialize_deep_nesting)
{
  string json_str;
  const int depth = 65536;
  for (int i=0; i<depth; ++i) { json_str += "[\n"; }
  json_str += " 100 ";
  for (int i=0; i<depth; ++i) { json_str += "]\n"; }
  string_view sv(json_str);
  string_view_istream istrm(sv);
  auto array = make_json_record(istrm, d_config_t());
  //write_json_text(cout, array);
  //cout << endl;
}

TEST(JsonObject, object_deserialize_deep_nesting)
{
  string json_str;
  const int depth = 65536;
  for (int i=0; i<depth; ++i) {
    if (i & 1) { json_str += "[\n"; }
    else       { json_str += "{ object: \n"; }
  }
  json_str += " 100 ";
  for (int i=0; i<depth; ++i) {
    if (i & 1) { json_str += "}\n"; }
    else       { json_str += "]\n"; }
  }
  string_view sv(json_str);
  string_view_istream istrm(sv);
  auto object = make_json_record(istrm, d_config_t());
  //write_json_text(cout, object);
  //cout << endl;
}
