#include "minitest.h"
#include "j5serdes.h"
#include <iostream>

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

