#include "minitest.h"

TEST(main, first) {
  class A {
  public:
    A() : _a(0) {};
    virtual ~A() {};
    virtual int get() const { return _a; }
  private:
    int _a;
  };
  class B : public A {
  public:
    B() : _a(2) {};
    virtual ~B() {};
    int get() const { return _a; }
  private:
    int _a;
  };
  A* xx = new B;
  EXPECT_TRUE(xx->get() == 2);
  EXPECT_TRUE(dynamic_cast<B*>(xx));
  delete xx;
}

TEST(main, DISABLED_second) {
}

TEST(main, third) {
}

TEST(secondary, first) {
  EXPECT_FALSE("" == nullptr);
}

TEST(secondary, last) {
}

TEST(single, first) {
}

