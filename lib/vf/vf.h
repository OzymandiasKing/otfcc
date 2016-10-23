#ifndef CARYLL_VF_VF_H
#define CARYLL_VF_VF_H

#include "support/util.h"

// According to OT 1.8 specification, computing a final coordinate from a variable is a inner product:
// <x, delta | VS_AVAR | style >
// We will treat the data representing a point coordinate or a metric as a Functional, while the VS-AVAR
// part as a Ket.

#include "functional.h"
#include "ket.h"

#endif
