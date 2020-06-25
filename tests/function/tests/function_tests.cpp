/*
 * Copyright 2019 Cray Inc. All Rights Reserved.
 *
 * Unpublished Proprietary Information.
 * This unpublished work is protected to trade secret, copyright and other laws.
 * Except as permitted by contract or express written permission of Cray Inc.,
 * no part of this work or its content may be used, reproduced or disclosed
 * in any form.
 */

#include "cti_fe_function_test.hpp"
#include <string>

// defined in cti_fe_function_test.cpp
extern void setSysArguments(std::string argv);

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);

    if (argc < 2) {
        setSysArguments("");
    } else {
        setSysArguments(argv[1]);
    }

    return RUN_ALL_TESTS();
}
