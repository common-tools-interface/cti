/*********************************************************************************\
 * Frontend_impl.h - Common includes for derived frontend implementations.
 *
 * Copyright 2019-2023 Hewlett Packard Enterprise Development LP.
 * SPDX-License-Identifier: Linux-OpenIB
 ******************************************************************************/

#pragma once

#include <config.h>

#if HAVE_ALPS
#include "ALPS/Frontend.hpp"
#endif
#include "SLURM/Frontend.hpp"
#if HAVE_PALS
#include "PALS/Frontend.hpp"
#endif
#include "GenericSSH/Frontend.hpp"
#if HAVE_FLUX
#include "Flux/Frontend.hpp"
#endif
#include "Localhost/Frontend.hpp"
