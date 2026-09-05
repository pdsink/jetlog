#include <gtest/gtest.h>

//
// The googletest package here is built without gtest_main, so the runner lives
// with the tests rather than in the library.
//
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);

    return RUN_ALL_TESTS();
}
