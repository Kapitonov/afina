#include "gtest/gtest.h"
#include <afina/Executor.h>
#include <condition_variable>
#include <mutex>

using namespace Afina;
using namespace std;

TEST(ExecutorTest, ConstructDestroy) {
    {
        Executor executor{"", 1};
        ASSERT_TRUE(1);
    }
    ASSERT_TRUE(1);
}

TEST(ExecutorTest, SimpleTask) {
    Executor ex{"", 3};
    for (int i = 0; i < 50; i++) {
        ex.Execute([i](){
            std::cout << "Hello #" << i << std::endl;});
    }

    std::cout << "Starting to stop executor...\n";
    ex.Stop(true);
    ASSERT_TRUE(1);
}


