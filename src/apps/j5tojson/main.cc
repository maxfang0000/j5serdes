#include "j5serdes.h"
#include <cstdlib>
#include <iostream>
#include <fstream>

using namespace std;
using namespace J5Serdes;

int main(int argc, const char* argv[])
{
  if (argc < 2) {
    cerr << "Usage: " << argv[0] << " <input-file>" << endl;
    return 1;
  }

  ifstream ifstr(argv[1]);
  if (!ifstr) {
    cerr << "Failed to open input file: " << argv[1] << endl;
    return 1;
  }

  try {
    auto json = make_json_record(ifstr);
    json->serialize(cout);
    cout << endl;
  } catch (const exception& e) {
    cerr << "Error: " << e.what() << endl;
    return 1;
  }

  return 0;
}
