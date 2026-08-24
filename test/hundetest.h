#ifndef __TEST_HUNDETEST_H
#define __TEST_HUNDETEST_H

#define some_test_fail() some(_) { TEST_FAIL(); }
#define none_test_fail() none() { TEST_FAIL(); }

#define success_test_fail() success(_) { TEST_FAIL(); }
#define error_test_fail() error(_code, _message) { TEST_FAIL(); }
#define error_test_ok() error(_code, _message) { TEST_PASS(); }

#endif
