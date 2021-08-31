/*****************************************************************************
 *
 * This MobilityDB code is provided under The PostgreSQL License.
 *
 * Copyright (c) 2016-2021, Université libre de Bruxelles and MobilityDB
 * contributors
 *
 * MobilityDB includes portions of PostGIS version 3 source code released
 * under the GNU General Public License (GPLv2 or later).
 * Copyright (c) 2001-2021, PostGIS contributors
 *
 * Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose, without fee, and without a written 
 * agreement is hereby granted, provided that the above copyright notice and
 * this paragraph and the following two paragraphs appear in all copies.
 *
 * IN NO EVENT SHALL UNIVERSITE LIBRE DE BRUXELLES BE LIABLE TO ANY PARTY FOR
 * DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES, INCLUDING
 * LOST PROFITS, ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION,
 * EVEN IF UNIVERSITE LIBRE DE BRUXELLES HAS BEEN ADVISED OF THE POSSIBILITY 
 * OF SUCH DAMAGE.
 *
 * UNIVERSITE LIBRE DE BRUXELLES SPECIFICALLY DISCLAIMS ANY WARRANTIES, 
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE. THE SOFTWARE PROVIDED HEREUNDER IS ON
 * AN "AS IS" BASIS, AND UNIVERSITE LIBRE DE BRUXELLES HAS NO OBLIGATIONS TO 
 * PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS. 
 *
 *****************************************************************************/

/**
 * @file tnumber_distance.h
 * Distance functions for temporal numbers.
 */

#ifndef __TNUMBER_DISTANCE_H__
#define __TNUMBER_DISTANCE_H__

#define ACCEPT_USE_OF_DEPRECATED_PROJ_API_H 1

#include <postgres.h>
#include <catalog/pg_type.h>
#include <float.h>

#include "temporal.h"

/*****************************************************************************/

/* Distance functions */

extern Datum distance_base_tnumber(PG_FUNCTION_ARGS);
extern Datum distance_tnumber_base(PG_FUNCTION_ARGS);
extern Datum distance_tnumber_tnumber(PG_FUNCTION_ARGS);

/* Nearest approach distance */

extern Datum NAD_base_tnumber(PG_FUNCTION_ARGS);
extern Datum NAD_tnumber_base(PG_FUNCTION_ARGS);
extern Datum NAD_tbox_tbox(PG_FUNCTION_ARGS);
extern Datum NAD_tbox_tnumber(PG_FUNCTION_ARGS);
extern Datum NAD_tnumber_tbox(PG_FUNCTION_ARGS);
extern Datum NAD_tnumber_tnumber(PG_FUNCTION_ARGS);

extern double NAD_tbox_tbox_internal(const TBOX *box1, const TBOX *box2);

// NAI and shortestline functions are not yet implemented
// Are they useful ?

/*****************************************************************************/

#endif
