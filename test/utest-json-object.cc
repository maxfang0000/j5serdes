#include "minitest.h"
#include "j5serdes.h"
#include <iostream>
#include <streambuf>
#include <istream>
#include <sstream>

using namespace J5Serdes;
using namespace std;

TEST(JsonObject, basic_ops)
{
  JsonRecordPtr two = make_json_data(std::string("two"));
  auto root = make_json_object();
  auto array = make_json_array();
  array->push_back(make_json_data(true));
  array->push_back(two);
  array->push_back(make_json_data("three"));
  root->insert({ "one"  , make_json_data(1.) });
  root->insert(  "two"  , two );
  root->insert(  "three", std::move(array) );
  cout << root->size() << endl;
  root->serialize(cout, s_config_t());
  cout << endl;
}

class string_view_streambuf : public streambuf {
public:
  string_view_streambuf(string_view sv)
  {
    char* begin = const_cast<char*>(sv.data());
    char* end   = begin + sv.size();
    setg(begin, begin, end);
  };
};
class string_view_istream : public istream {
public:
  string_view_istream(string_view sv)
    : istream(nullptr), buffer(sv)
  { rdbuf(&buffer); };
private:
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
  auto data = make_json_data(istrm, d_config_t());
  data->serialize(cout);
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
  auto data = make_json_data(istrm, d_config_t());
  data->serialize(cout);
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
  auto data = make_json_data(istrm, d_config_t());
  data->serialize(cout);
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
  auto array = make_json_array(istrm, d_config_t());
  array->serialize(cout);
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
  auto object = make_json_object(istrm, d_config_t());
  object->serialize(cout);
  cout << endl;
}
