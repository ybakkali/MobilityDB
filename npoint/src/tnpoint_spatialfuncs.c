/*****************************************************************************
 *
 * This MobilityDB code is provided under The PostgreSQL License.
 *
 * Copyright (c) 2016-2021, Université libre de Bruxelles and MobilityDB
 * contributors
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
 * tnpoint_spatialfuncs.c
 * Geospatial functions for temporal network points.
 */

#include "tnpoint_spatialfuncs.h"

#include <assert.h>
#include <float.h>

#include "periodset.h"
#include "timeops.h"
#include "temporaltypes.h"
#include "oidcache.h"
#include "temporal_util.h"
#include "tpoint_spatialfuncs.h"
#include "tpoint_distance.h"
#include "tpoint_boxops.h"
#include "tnpoint.h"
#include "tnpoint_static.h"
#include "tnpoint_distance.h"
#include "tnpoint_tempspatialrels.h"

/*****************************************************************************
 * Parameter tests
 *****************************************************************************/

void
ensure_same_srid_tnpoint(const Temporal *temp1, const Temporal *temp2)
{
  if (tnpoint_srid_internal(temp1) != tnpoint_srid_internal(temp2))
    ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
      errmsg("The temporal network points must be in the same SRID")));
}

void
ensure_same_srid_tnpoint_stbox(const Temporal *temp, const STBOX *box)
{
  if (MOBDB_FLAGS_GET_X(box->flags) &&
    tnpoint_srid_internal(temp) != box->srid)
    ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
      errmsg("The temporal network point and the box must be in the same SRID")));
}

void
ensure_same_srid_tnpoint_gs(const Temporal *temp, const GSERIALIZED *gs)
{
  if (tnpoint_srid_internal(temp) != gserialized_get_srid(gs))
    ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
      errmsg("The temporal network point and the geometry must be in the same SRID")));
}

void
ensure_same_srid_tnpoint_npoint(const Temporal *temp, const npoint *np)
{
  if (tnpoint_srid_internal(temp) != npoint_srid_internal(np))
    ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
      errmsg("The temporal network point and the network point must be in the same SRID")));
}

void
ensure_same_rid_tnpointinst(const TInstant *inst1, const TInstant *inst2)
{
  if (tnpointinst_route(inst1) != tnpointinst_route(inst2))
    ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
      errmsg("All network points composing a temporal sequence must have same route identifier")));
}

/*****************************************************************************
 * Functions for spatial reference systems
 *****************************************************************************/

/* Spatial reference system identifier (SRID) of a temporal network point.
 * For temporal points of duration distinct from INSTANT the SRID is
 * obtained from the bounding box. */

int
tnpointinst_srid(const TInstant *inst)
{
  npoint *np = DatumGetNpoint(tinstant_value(inst));
  Datum line = route_geom(np->rid);
  GSERIALIZED *gs = (GSERIALIZED *) DatumGetPointer(line);
  int result = gserialized_get_srid(gs);
  pfree(DatumGetPointer(line));
  return result;
}

int
tnpoint_srid_internal(const Temporal *temp)
{
  int result;
  ensure_valid_tempsubtype(temp->subtype);
  if (temp->valuetypid != type_oid(T_NPOINT))
    elog(ERROR, "unknown npoint base type: %d", temp->valuetypid);
  if (temp->subtype == INSTANT)
    result = tnpointinst_srid((TInstant *) temp);
  else if (temp->subtype == INSTANTSET)
    result = tpointinstset_srid((TInstantSet *) temp);
  else if (temp->subtype == SEQUENCE)
    result = tpointseq_srid((TSequence *) temp);
  else /* temp->subtype == SEQUENCESET */
    result = tpointseqset_srid((TSequenceSet *) temp);
  return result;
}

PG_FUNCTION_INFO_V1(tnpoint_srid);

PGDLLEXPORT Datum
tnpoint_srid(PG_FUNCTION_ARGS)
{
  Temporal *temp = PG_GETARG_TEMPORAL(0);
  int result = tnpoint_srid_internal(temp);
  PG_FREE_IF_COPY(temp, 0);
  PG_RETURN_INT32(result);
}

/*****************************************************************************
 * Trajectory functions
 *****************************************************************************/

Datum
tnpointseq_trajectory1(const TInstant *inst1, const TInstant *inst2)
{
  npoint *np1 = DatumGetNpoint(tinstant_value(inst1));
  npoint *np2 = DatumGetNpoint(tinstant_value(inst2));
  assert(np1->rid == np2->rid);

  if (np1->pos == np2->pos)
    return npoint_as_geom_internal(np1);

  Datum line = route_geom(np1->rid);
  if ((np1->pos == 0 && np2->pos == 1) ||
    (np2->pos == 0 && np1->pos == 1))
    return line;

  Datum traj;
  if (np1->pos < np2->pos)
    traj = call_function3(LWGEOM_line_substring, line,
      Float8GetDatum(np1->pos), Float8GetDatum(np2->pos));
  else /* np1->pos < np2->pos */
  {
    Datum traj2 = call_function3(LWGEOM_line_substring, line,
      Float8GetDatum(np2->pos), Float8GetDatum(np1->pos));
    traj = call_function1(LWGEOM_reverse, traj2);
    pfree(DatumGetPointer(traj2));
  }
  pfree(DatumGetPointer(line));
  return traj;
}

/*****************************************************************************
 * Geometric positions functions
 * Return the geometric positions covered by the temporal npoint
 *****************************************************************************/

/*
 * NPoints functions
 * Return the network points covered by the moving object
 * Only the particular cases returning points are covered
 */

npoint **
tnpointi_npoints(const TInstantSet *ti, int *count)
{
  npoint **result = palloc(sizeof(npoint *) * ti->count);
  result[0] =  DatumGetNpoint(tinstant_value(tinstantset_inst_n(ti, 0)));
  int k = 1;
  for (int i = 1; i < ti->count; i++)
  {
    npoint *np = DatumGetNpoint(tinstant_value(tinstantset_inst_n(ti, i)));
    bool found = false;
    for (int j = 0; j < k; j++)
    {
      if (npoint_eq_internal(np, result[j]))
      {
        found = true;
        break;
      }
    }
    if (!found)
      result[k++] = np;
  }
  *count = k;
  return result;
}

npoint **
tnpointseq_step_npoints(const TSequence *seq, int *count)
{
  npoint **result = palloc(sizeof(npoint *) * seq->count);
  result[0] =  DatumGetNpoint(tinstant_value(tsequence_inst_n(seq, 0)));
  int k = 1;
  for (int i = 1; i < seq->count; i++)
  {
    npoint *np = DatumGetNpoint(tinstant_value(tsequence_inst_n(seq, i)));
    bool found = false;
    for (int j = 0; j < k; j++)
    {
      if (npoint_eq_internal(np, result[j]))
      {
        found = true;
        break;
      }
    }
    if (!found)
      result[k++] = np;
  }
  *count = k;
  return result;
}

npoint **
tnpoints_step_npoints(const TSequenceSet *ts, int *count)
{
  npoint **result = palloc(sizeof(npoint *) * ts->totalcount);
  const TSequence *seq = tsequenceset_seq_n(ts, 0);
  result[0] =  DatumGetNpoint(tinstant_value(tsequence_inst_n(seq, 0)));
  int l = 1;
  for (int i = 1; i < ts->count; i++)
  {
    seq = tsequenceset_seq_n(ts, i);
    for (int j = 1; j < seq->count; j++)
    {
      npoint *np = DatumGetNpoint(tinstant_value(tsequence_inst_n(seq, j)));
      bool found = false;
      for (int k = 0; k < l; k++)
      {
        if (npoint_eq_internal(np, result[k]))
        {
          found = true;
          break;
        }
      }
      if (!found)
        result[l++] = np;
    }
  }
  *count = l;
  return result;
}

Datum
tnpointinst_geom(const TInstant *inst)
{
  npoint *np = DatumGetNpoint(tinstant_value(inst));
  return npoint_as_geom_internal(np);
}

Datum
tnpointi_geom(const TInstantSet *ti)
{
  /* Instantaneous sequence */
  if (ti->count == 1)
    return tnpointinst_geom(tinstantset_inst_n(ti, 0));

  int count;
  /* The following function removes duplicate values */
  npoint **points = tnpointi_npoints(ti, &count);
  Datum result = npointarr_to_geom_internal(points, count);
  pfree(points);
  return result;
}

Datum
tnpointseq_geom(const TSequence *seq)
{
  /* Instantaneous sequence */
  if (seq->count == 1)
    return tnpointinst_geom(tsequence_inst_n(seq, 0));

  Datum result;
  if (MOBDB_FLAGS_GET_LINEAR(seq->flags))
  {
    nsegment *segment = tnpointseq_linear_positions(seq);
    result = nsegment_as_geom_internal(segment);
    pfree(segment);
  }
  else
  {
    int count;
    /* The following function removes duplicate values */
    npoint **points = tnpointseq_step_npoints(seq, &count);
    result = npointarr_to_geom_internal(points, count);
    pfree(points);
  }
  return result;
}

Datum
tnpoints_geom(const TSequenceSet *ts)
{
  /* Singleton sequence set */
  if (ts->count == 1)
    return tnpointseq_geom(tsequenceset_seq_n(ts, 0));

  int count;
  Datum result;
  if (MOBDB_FLAGS_GET_LINEAR(ts->flags))
  {
    nsegment **segments = tnpoints_positions(ts, &count);
    result = nsegmentarr_to_geom_internal(segments, count);
    for (int i = 0; i < count; i++)
      pfree(segments[i]);
    pfree(segments);
  }
  else
  {
    npoint **points = tnpoints_step_npoints(ts, &count);
    result = npointarr_to_geom_internal(points, count);
    pfree(points);
  }
  return result;
}

Datum
tnpoint_geom(const Temporal *temp)
{
  Datum result;
  ensure_valid_tempsubtype(temp->subtype);
  if (temp->subtype == INSTANT)
    result = tnpointinst_geom((TInstant *) temp);
  else if (temp->subtype == INSTANTSET)
    result = tnpointi_geom((TInstantSet *) temp);
  else if (temp->subtype == SEQUENCE)
    result = tnpointseq_geom((TSequence *) temp);
  else /* temp->subtype == SEQUENCESET */
    result = tnpoints_geom((TSequenceSet *) temp);
  return result;
}

PG_FUNCTION_INFO_V1(tnpoint_trajectory);

PGDLLEXPORT Datum
tnpoint_trajectory(PG_FUNCTION_ARGS)
{
  Temporal *temp = PG_GETARG_TEMPORAL(0);
  Datum result = tnpoint_geom(temp);
  PG_FREE_IF_COPY(temp, 0);
  PG_RETURN_DATUM(result);
}

/*****************************************************************************
 * Geographical equality for network points
 * Two network points may be have different rid but represent the same
 * geographical point at the intersection of the two rids
 *****************************************************************************/

bool
npoint_same_internal(const npoint *np1, const npoint *np2)
{
  /* Same route identifier */
  if (np1->rid == np2->rid)
    return fabs(np1->pos - np2->pos) < EPSILON;
  Datum point1 = npoint_as_geom_internal(np1);
  Datum point2 = npoint_as_geom_internal(np2);
  bool result = datum_eq(point1, point2, type_oid(T_GEOMETRY));
  pfree(DatumGetPointer(point1)); pfree(DatumGetPointer(point2));
  return result;
}

PG_FUNCTION_INFO_V1(npoint_same);

PGDLLEXPORT Datum
npoint_same(PG_FUNCTION_ARGS)
{
  npoint *np1 = PG_GETARG_NPOINT(0);
  npoint *np2 = PG_GETARG_NPOINT(1);
  PG_RETURN_BOOL(npoint_same_internal(np1, np2));
}

/*****************************************************************************
 * Length functions
 *****************************************************************************/

/* Length traversed by the temporal npoint */

static double
tnpointseq_length(const TSequence *seq)
{
  /* Instantaneous sequence */
  if (seq->count == 1)
    return 0;

  const TInstant *inst = tsequence_inst_n(seq, 0);
  npoint *np1 = DatumGetNpoint(tinstant_value(inst));
  double length = route_length(np1->rid);
  double fraction = 0;
  for (int i = 1; i < seq->count; i++)
  {
    inst = tsequence_inst_n(seq, i);
    npoint *np2 = DatumGetNpoint(tinstant_value(inst));
    fraction += fabs(np2->pos - np1->pos);
    np1 = np2;
  }
  return length * fraction;
}

static double
tnpoints_length(const TSequenceSet *ts)
{
  double result = 0;
  for (int i = 0; i < ts->count; i++)
  {
    const TSequence *seq = tsequenceset_seq_n(ts, i);
    result += tnpointseq_length(seq);
  }
  return result;
}

PG_FUNCTION_INFO_V1(tnpoint_length);

PGDLLEXPORT Datum
tnpoint_length(PG_FUNCTION_ARGS)
{
  Temporal *temp = PG_GETARG_TEMPORAL(0);
  double result = 0.0;
  ensure_valid_tempsubtype(temp->subtype);
  if (temp->subtype == INSTANT || temp->subtype == INSTANTSET ||
    (temp->subtype == SEQUENCE && ! MOBDB_FLAGS_GET_LINEAR(temp->flags)) ||
    (temp->subtype == SEQUENCESET && ! MOBDB_FLAGS_GET_LINEAR(temp->flags)))
    ;
  else if (temp->subtype == SEQUENCE)
    result = tnpointseq_length((TSequence *) temp);
  else /* temp->subtype == SEQUENCESET */
    result = tnpoints_length((TSequenceSet *) temp);
  PG_FREE_IF_COPY(temp, 0);
  PG_RETURN_FLOAT8(result);
}

/* Cumulative length traversed by the temporal npoint */

static TInstant *
tnpointinst_set_zero(const TInstant *inst)
{
  return tinstant_make(Float8GetDatum(0.0), inst->t, FLOAT8OID);
}

static TInstantSet *
tnpointi_set_zero(const TInstantSet *ti)
{
  TInstant **instants = palloc(sizeof(TInstant *) * ti->count);
  Datum zero = Float8GetDatum(0.0);
  for (int i = 0; i < ti->count; i++)
  {
    const TInstant *inst = tinstantset_inst_n(ti, i);
    instants[i] = tinstant_make(zero, inst->t, FLOAT8OID);
  }
  TInstantSet *result = tinstantset_make((const TInstant **) instants,
    ti->count, MERGE_NO);
  for (int i = 1; i < ti->count; i++)
    pfree(instants[i]);
  pfree(instants);
  return result;
}

static TSequence *
tnpointseq_cumulative_length(const TSequence *seq, double prevlength)
{
  const TInstant *inst1;
  TInstant *inst;
  /* Instantaneous sequence */
  if (seq->count == 1)
  {
    inst1 = tsequence_inst_n(seq, 0);
    inst = tinstant_make(Float8GetDatum(prevlength), inst1->t, FLOAT8OID);
    TSequence *result = tsequence_make((const TInstant **) &inst, 1,
      true, true, true, false);
    pfree(inst);
    return result;
  }

  TInstant **instants = palloc(sizeof(TInstant *) * seq->count);
  /* Stepwise interpolation */
  if (! MOBDB_FLAGS_GET_LINEAR(seq->flags))
  {
    Datum length = Float8GetDatum(0.0);
    for (int i = 0; i < seq->count; i++)
    {
      inst1 = tsequence_inst_n(seq, i);
      instants[i] = tinstant_make(length, inst1->t, FLOAT8OID);
    }
  }
  else
  /* Linear interpolation */
  {
    inst1 = tsequence_inst_n(seq, 0);
    npoint *np1 = DatumGetNpoint(tinstant_value(inst1));
    double rlength = route_length(np1->rid);
    double length = prevlength;
    instants[0] = tinstant_make(Float8GetDatum(length), inst1->t, FLOAT8OID);
    for (int i = 1; i < seq->count; i++)
    {
      const TInstant *inst2 = tsequence_inst_n(seq, i);
      npoint *np2 = DatumGetNpoint(tinstant_value(inst2));
      length += fabs(np2->pos - np1->pos) * rlength;
      instants[i] = tinstant_make(Float8GetDatum(length), inst2->t,
        FLOAT8OID);
      np1 = np2;
    }
  }
  TSequence *result = tsequence_make((const TInstant **) instants,
    seq->count, seq->period.lower_inc, seq->period.upper_inc,
    MOBDB_FLAGS_GET_LINEAR(seq->flags), false);

  for (int i = 1; i < seq->count; i++)
    pfree(instants[i]);
  pfree(instants);
  return result;
}

static TSequenceSet *
tnpoints_cumulative_length(const TSequenceSet *ts)
{
  TSequence **sequences = palloc(sizeof(TSequence *) * ts->count);
  double length = 0;
  for (int i = 0; i < ts->count; i++)
  {
    const TSequence *seq = tsequenceset_seq_n(ts, i);
    sequences[i] = tnpointseq_cumulative_length(seq, length);
    const TInstant *end = tsequence_inst_n(sequences[i], seq->count - 1);
    length += DatumGetFloat8(tinstant_value(end));
  }
  TSequenceSet *result = tsequenceset_make((const TSequence **) sequences,
    ts->count, false);

  for (int i = 1; i < ts->count; i++)
    pfree(sequences[i]);
  pfree(sequences);
  return result;
}

PG_FUNCTION_INFO_V1(tnpoint_cumulative_length);

PGDLLEXPORT Datum
tnpoint_cumulative_length(PG_FUNCTION_ARGS)
{
  Temporal *temp = PG_GETARG_TEMPORAL(0);
  Temporal *result;
  ensure_valid_tempsubtype(temp->subtype);
  if (temp->subtype == INSTANT)
    result = (Temporal *)tnpointinst_set_zero((TInstant *) temp);
  else if (temp->subtype == INSTANTSET)
    result = (Temporal *)tnpointi_set_zero((TInstantSet *) temp);
  else if (temp->subtype == SEQUENCE)
    result = (Temporal *)tnpointseq_cumulative_length((TSequence *) temp, 0);
  else /* temp->subtype == SEQUENCESET */
    result = (Temporal *)tnpoints_cumulative_length((TSequenceSet *) temp);
  PG_FREE_IF_COPY(temp, 0);
  PG_RETURN_POINTER(result);
}

/*****************************************************************************
 * Speed functions
 *****************************************************************************/

static TSequence *
tnpointseq_speed(const TSequence *seq)
{
  /* Instantaneous sequence */
  if (seq->count == 1)
    return NULL;

  TInstant **instants = palloc(sizeof(TInstant *) * seq->count);
  /* Stepwise interpolation */
  if (! MOBDB_FLAGS_GET_LINEAR(seq->flags))
  {
    Datum length = Float8GetDatum(0.0);
    for (int i = 0; i < seq->count; i++)
    {
      const TInstant *inst = tsequence_inst_n(seq, i);
      instants[i] = tinstant_make(length, inst->t, FLOAT8OID);
    }
  }
  else
  /* Linear interpolation */
  {
    const TInstant *inst1 = tsequence_inst_n(seq, 0);
    npoint *np1 = DatumGetNpoint(tinstant_value(inst1));
    double rlength = route_length(np1->rid);
    const TInstant *inst2 = NULL; /* make the compiler quiet */
    double speed = 0; /* make the compiler quiet */
    for (int i = 0; i < seq->count - 1; i++)
    {
      inst2 = tsequence_inst_n(seq, i + 1);
      npoint *np2 = DatumGetNpoint(tinstant_value(inst2));
      double length = fabs(np2->pos - np1->pos) * rlength;
      speed = length / (((double)(inst2->t) - (double)(inst1->t)) / 1000000);
      instants[i] = tinstant_make(Float8GetDatum(speed),
        inst1->t, FLOAT8OID);
      inst1 = inst2;
      np1 = np2;
    }
    instants[seq->count-1] = tinstant_make(Float8GetDatum(speed),
      inst2->t, FLOAT8OID);
  }
  /* The resulting sequence has stepwise interpolation */
  TSequence *result = tsequence_make_free(instants, seq->count,
    seq->period.lower_inc, seq->period.upper_inc, STEP, true);
  return result;
}

static TSequenceSet *
tnpoints_speed(const TSequenceSet *ts)
{
  TSequence **sequences = palloc(sizeof(TSequence *) * ts->count);
  int k = 0;
  for (int i = 0; i < ts->count; i++)
  {
    const TSequence *seq = tsequenceset_seq_n(ts, i);
    TSequence *seq1 = tnpointseq_speed(seq);
    if (seq1 != NULL)
      sequences[k++] = seq1;
  }
  if (k == 0)
  {
    pfree(sequences);
    return NULL;
  }
  /* The resulting sequence set has stepwise interpolation */
  TSequenceSet *result = tsequenceset_make_free(sequences, k, STEP);
  return result;
}

PG_FUNCTION_INFO_V1(tnpoint_speed);

PGDLLEXPORT Datum
tnpoint_speed(PG_FUNCTION_ARGS)
{
  Temporal *temp = PG_GETARG_TEMPORAL(0);
  Temporal *result;
  ensure_valid_tempsubtype(temp->subtype);
  if (temp->subtype == INSTANT)
    result = (Temporal *)tnpointinst_set_zero((TInstant *) temp);
  else if (temp->subtype == INSTANTSET)
    result = (Temporal *)tnpointi_set_zero((TInstantSet *) temp);
  else if (temp->subtype == SEQUENCE)
    result = (Temporal *)tnpointseq_speed((TSequence *) temp);
  else /* temp->subtype == SEQUENCESET */
    result = (Temporal *)tnpoints_speed((TSequenceSet *) temp);
  PG_FREE_IF_COPY(temp, 0);
  if (result == NULL)
    PG_RETURN_NULL();
  PG_RETURN_POINTER(result);
}

/*****************************************************************************
 * Time-weighed centroid for temporal geometry points
 *****************************************************************************/

PG_FUNCTION_INFO_V1(tnpoint_twcentroid);

PGDLLEXPORT Datum
tnpoint_twcentroid(PG_FUNCTION_ARGS)
{
  Temporal *temp = PG_GETARG_TEMPORAL(0);
  Temporal *tgeom = tnpoint_as_tgeompoint_internal(temp);
  Datum result = tgeompoint_twcentroid_internal(tgeom);
  pfree(tgeom);
  PG_FREE_IF_COPY(temp, 0);
  PG_RETURN_DATUM(result);
}

/*****************************************************************************
 * Temporal azimuth
 *****************************************************************************/

static TInstant **
tnpointseq_azimuth1(const TInstant *inst1, const TInstant *inst2,
  int *count)
{
  npoint *np1 = DatumGetNpoint(tinstant_value(inst1));
  npoint *np2 = DatumGetNpoint(tinstant_value(inst2));

  /* Constant segment */
  if (np1->pos == np2->pos)
  {
    *count = 0;
    return NULL;
  }

  /* Find all vertices in the segment */
  Datum traj = tnpointseq_trajectory1(inst1, inst2);
  int countVertices = DatumGetInt32(call_function1(
    LWGEOM_numpoints_linestring, traj));
  TInstant **result = palloc(sizeof(TInstant *) * countVertices);
  Datum vertex1 = call_function2(LWGEOM_pointn_linestring, traj,
    Int32GetDatum(1)); /* 1-based */
  Datum azimuth;
  TimestampTz time = inst1->t;
  for (int i = 0; i < countVertices - 1; i++)
  {
    Datum vertex2 = call_function2(LWGEOM_pointn_linestring, traj,
      Int32GetDatum(i + 2)); /* 1-based */
    double fraction = DatumGetFloat8(call_function2(
      LWGEOM_line_locate_point, traj, vertex2));
    azimuth = call_function2(LWGEOM_azimuth, vertex1, vertex2);
    result[i] = tinstant_make(azimuth, time, FLOAT8OID);
    pfree(DatumGetPointer(vertex1));
    vertex1 = vertex2;
    time =  inst1->t + (long) ((double) (inst2->t - inst1->t) * fraction);
  }
  pfree(DatumGetPointer(traj));
  pfree(DatumGetPointer(vertex1));
  *count = countVertices - 1;
  return result;
}

static int
tnpointseq_azimuth2(TSequence **result, const TSequence *seq)
{
  /* Instantaneous sequence */
  if (seq->count == 1)
    return 0;

  TInstant ***instants = palloc(sizeof(TInstant *) * (seq->count - 1));
  int *countinsts = palloc0(sizeof(int) * (seq->count - 1));
  int totalinsts = 0; /* number of created instants so far */
  int l = 0; /* number of created sequences */
  int m = 0; /* index of the segment from which to assemble instants */
  Datum last_value;
  const TInstant *inst1 = tsequence_inst_n(seq, 0);
  bool lower_inc = seq->period.lower_inc;
  for (int i = 0; i < seq->count - 1; i++)
  {
    const TInstant *inst2 = tsequence_inst_n(seq, i + 1);
    instants[i] = tnpointseq_azimuth1(inst1, inst2, &countinsts[i]);
    /* If constant segment */
    if (countinsts[i] == 0)
    {
      /* Assemble all instants created so far */
      if (totalinsts != 0)
      {
        TInstant **allinstants = palloc(sizeof(TInstant *) * (totalinsts + 1));
        int n = 0;
        for (int j = m; j < i; j++)
        {
          for (int k = 0; k < countinsts[j]; k++)
            allinstants[n++] = instants[j][k];
          if (instants[j] != NULL)
            pfree(instants[j]);
        }
        /* Add closing instant */
        last_value = tinstant_value(allinstants[n - 1]);
        allinstants[n++] = tinstant_make(last_value, inst1->t, FLOAT8OID);
        /* Resulting sequence has stepwise interpolation */
        result[l++] = tsequence_make_free(allinstants, n, lower_inc, true,
          STEP, true);
        /* Indicate that we have consommed all instants created so far */
        m = i;
        totalinsts = 0;
      }
    }
    else
    {
      totalinsts += countinsts[i];
    }
    inst1 = inst2;
    lower_inc = true;
  }
  if (totalinsts != 0)
  {
    /* Assemble all instants created so far */
    TInstant **allinstants = palloc(sizeof(TInstant *) * (totalinsts + 1));
    int n = 0;
    for (int j = m; j < seq->count - 1; j++)
    {
      for (int k = 0; k < countinsts[j]; k++)
        allinstants[n++] = instants[j][k];
      if (instants[j] != NULL)
        pfree(instants[j]);
    }
    /* Add closing instant */
    last_value = tinstant_value(allinstants[n - 1]);
    allinstants[n++] = tinstant_make(last_value, inst1->t, FLOAT8OID);
    /* Resulting sequence has stepwise interpolation */
    result[l++] = tsequence_make((const TInstant **) allinstants, n,
      lower_inc, true, false, true);
    for (int j = 0; j < n; j++)
      pfree(allinstants[j]);
    pfree(allinstants);
  }
  pfree(instants);
  pfree(countinsts);
  return l;
}

static TSequenceSet *
tnpointseq_azimuth(const TSequence *seq)
{
  TSequence **sequences = palloc(sizeof(TSequence *) * (seq->count - 1));
  int count = tnpointseq_azimuth2(sequences, seq);
  if (count == 0)
  {
    pfree(sequences);
    return NULL;
  }

  /* Resulting sequence set has stepwise interpolation */
  TSequenceSet *result = tsequenceset_make_free(sequences, count, true);
  return result;
}

static TSequenceSet *
tnpoints_azimuth(const TSequenceSet *ts)
{
  if (ts->count == 1)
    return tnpointseq_azimuth(tsequenceset_seq_n(ts, 0));

  TSequence **sequences = palloc(sizeof(TSequence *) * ts->totalcount);
  int k = 0;
  for (int i = 0; i < ts->count; i++)
  {
    const TSequence *seq = tsequenceset_seq_n(ts, i);
    int countstep = tnpointseq_azimuth2(&sequences[k], seq);
    k += countstep;
  }
  if (k == 0)
    return NULL;

  /* Resulting sequence set has stepwise interpolation */
  TSequenceSet *result = tsequenceset_make_free(sequences, k, STEP);
  return result;
}

PG_FUNCTION_INFO_V1(tnpoint_azimuth);

PGDLLEXPORT Datum
tnpoint_azimuth(PG_FUNCTION_ARGS)
{
  Temporal *temp = PG_GETARG_TEMPORAL(0);
  Temporal *result = NULL;
  ensure_valid_tempsubtype(temp->subtype);
  if (temp->subtype == INSTANT || temp->subtype == INSTANTSET ||
    (temp->subtype == SEQUENCE && ! MOBDB_FLAGS_GET_LINEAR(temp->flags)) ||
    (temp->subtype == SEQUENCESET && ! MOBDB_FLAGS_GET_LINEAR(temp->flags)))
    ;
  else if (temp->subtype == SEQUENCE)
    result = (Temporal *)tnpointseq_azimuth((TSequence *) temp);
  else /* temp->subtype == SEQUENCESET */
    result = (Temporal *)tnpoints_azimuth((TSequenceSet *) temp);
  PG_FREE_IF_COPY(temp, 0);
  if (result == NULL)
    PG_RETURN_NULL();
  PG_RETURN_POINTER(result);
}

/*****************************************************************************
 * Restriction functions
 *****************************************************************************/

/* Restrict a temporal npoint to a geometry */

PG_FUNCTION_INFO_V1(tnpoint_at_geometry);

PGDLLEXPORT Datum
tnpoint_at_geometry(PG_FUNCTION_ARGS)
{
  Temporal *temp = PG_GETARG_TEMPORAL(0);
  GSERIALIZED *gs = PG_GETARG_GSERIALIZED_P(1);
  ensure_same_srid_tnpoint_gs(temp, gs);
  if (gserialized_is_empty(gs))
  {
    PG_FREE_IF_COPY(temp, 0);
    PG_FREE_IF_COPY(gs, 1);
    PG_RETURN_NULL();
  }
  ensure_has_not_Z_gs(gs);

  Temporal *geomtemp = tnpoint_as_tgeompoint_internal(temp);
  Temporal *geomresult = tpoint_restrict_geometry_internal(geomtemp,
    PointerGetDatum(gs), REST_AT);
  Temporal *result = NULL;
  if (geomresult != NULL)
  {
    result = tgeompoint_as_tnpoint_internal(geomresult);
    pfree(geomresult);
  }
  pfree(geomtemp);
  PG_FREE_IF_COPY(temp, 0);
  PG_FREE_IF_COPY(gs, 1);
  if (result == NULL)
    PG_RETURN_NULL();
  PG_RETURN_POINTER(result);
}

/* Restrict a temporal point to the complement of a geometry */

PG_FUNCTION_INFO_V1(tnpoint_minus_geometry);

PGDLLEXPORT Datum
tnpoint_minus_geometry(PG_FUNCTION_ARGS)
{
  Temporal *temp = PG_GETARG_TEMPORAL(0);
  GSERIALIZED *gs = PG_GETARG_GSERIALIZED_P(1);
  ensure_same_srid_tnpoint_gs(temp, gs);
  if (gserialized_is_empty(gs))
  {
    Temporal* copy = temporal_copy(temp);
    PG_FREE_IF_COPY(temp, 0);
    PG_FREE_IF_COPY(gs, 1);
    PG_RETURN_POINTER(copy);
  }
  ensure_has_not_Z_gs(gs);

  Temporal *geomtemp = tnpoint_as_tgeompoint_internal(temp);
  Temporal *geomresult = tpoint_restrict_geometry_internal(geomtemp,
    PointerGetDatum(gs), REST_MINUS);
  Temporal *result = NULL;
  if (geomresult != NULL)
  {
    result = tgeompoint_as_tnpoint_internal(geomresult);
    pfree(geomresult);
  }
  pfree(geomtemp);
  PG_FREE_IF_COPY(temp, 0);
  PG_FREE_IF_COPY(gs, 1);
  if (result == NULL)
    PG_RETURN_NULL();
  PG_RETURN_POINTER(result);
}

/*****************************************************************************
 * Nearest approach instant
 *****************************************************************************/

PG_FUNCTION_INFO_V1(NAI_geometry_tnpoint);

PGDLLEXPORT Datum
NAI_geometry_tnpoint(PG_FUNCTION_ARGS)
{
  GSERIALIZED *gs = PG_GETARG_GSERIALIZED_P(0);
  Temporal *temp = PG_GETARG_TEMPORAL(1);
  if (gserialized_is_empty(gs))
  {
    PG_FREE_IF_COPY(gs, 0);
    PG_FREE_IF_COPY(temp, 1);
    PG_RETURN_NULL();
  }

  Temporal *geomtemp = tnpoint_as_tgeompoint_internal(temp);
  TInstant *geomresult = NAI_tpoint_geo_internal(fcinfo, geomtemp, gs);
  TInstant *result = tgeompointinst_as_tnpointinst(geomresult);
  pfree(geomtemp); pfree(geomresult);
  PG_FREE_IF_COPY(gs, 0);
  PG_FREE_IF_COPY(temp, 1);
  PG_RETURN_POINTER(result);
}

PG_FUNCTION_INFO_V1(NAI_npoint_tnpoint);

PGDLLEXPORT Datum
NAI_npoint_tnpoint(PG_FUNCTION_ARGS)
{
  npoint *np = PG_GETARG_NPOINT(0);
  Temporal *temp = PG_GETARG_TEMPORAL(1);
  Datum geom = npoint_as_geom_internal(np);
  GSERIALIZED *gs = (GSERIALIZED *)PG_DETOAST_DATUM(geom);
  Temporal *geomtemp = tnpoint_as_tgeompoint_internal(temp);
  TInstant *geomresult = NAI_tpoint_geo_internal(fcinfo, geomtemp, gs);
  TInstant *result = tgeompointinst_as_tnpointinst(geomresult);
  pfree(geomtemp); pfree(geomresult);
  POSTGIS_FREE_IF_COPY_P(gs, DatumGetPointer(geom));
  pfree(DatumGetPointer(geom));
  PG_FREE_IF_COPY(temp, 1);
  PG_RETURN_POINTER(result);
}

PG_FUNCTION_INFO_V1(NAI_tnpoint_geometry);

PGDLLEXPORT Datum
NAI_tnpoint_geometry(PG_FUNCTION_ARGS)
{
  Temporal *temp = PG_GETARG_TEMPORAL(0);
  GSERIALIZED *gs = PG_GETARG_GSERIALIZED_P(1);
  if (gserialized_is_empty(gs))
  {
    PG_FREE_IF_COPY(temp, 0);
    PG_FREE_IF_COPY(gs, 1);
    PG_RETURN_NULL();
  }

  Temporal *geomtemp = tnpoint_as_tgeompoint_internal(temp);
  TInstant *geomresult = NAI_tpoint_geo_internal(fcinfo, geomtemp, gs);
  TInstant *result = tgeompointinst_as_tnpointinst(geomresult);
  pfree(geomtemp); pfree(geomresult);
  PG_FREE_IF_COPY(temp, 0);
  PG_FREE_IF_COPY(gs, 1);
  PG_RETURN_POINTER(result);
}

PG_FUNCTION_INFO_V1(NAI_tnpoint_npoint);

PGDLLEXPORT Datum
NAI_tnpoint_npoint(PG_FUNCTION_ARGS)
{
  Temporal *temp = PG_GETARG_TEMPORAL(0);
  npoint *np = PG_GETARG_NPOINT(1);
  Datum geom = npoint_as_geom_internal(np);
  GSERIALIZED *gs = (GSERIALIZED *)PG_DETOAST_DATUM(geom);
  Temporal *geomtemp = tnpoint_as_tgeompoint_internal(temp);
  TInstant *geomresult = NAI_tpoint_geo_internal(fcinfo, geomtemp, gs);
  TInstant *result = tgeompointinst_as_tnpointinst(geomresult);
  pfree(geomtemp); pfree(geomresult);
  POSTGIS_FREE_IF_COPY_P(gs, DatumGetPointer(geom));
  pfree(DatumGetPointer(geom));
  PG_FREE_IF_COPY(temp, 0);
  PG_RETURN_POINTER(result);
}

PG_FUNCTION_INFO_V1(NAI_tnpoint_tnpoint);

PGDLLEXPORT Datum
NAI_tnpoint_tnpoint(PG_FUNCTION_ARGS)
{
  Temporal *temp1 = PG_GETARG_TEMPORAL(0);
  Temporal *temp2 = PG_GETARG_TEMPORAL(1);
  TInstant *result = NULL;
  Temporal *dist = distance_tnpoint_tnpoint_internal(temp1, temp2);
  if (dist != NULL)
  {
    const TInstant *min = temporal_min_instant((const Temporal *) dist);
    result = (TInstant *) temporal_restrict_timestamp_internal(temp1, min->t, REST_AT);
    pfree(dist);
    if (result == NULL)
    {
      if (temp1->subtype == SEQUENCE)
        result = tinstant_copy(tsequence_inst_at_timestamp_excl(
          (TSequence *) temp1, min->t));
      else /* temp->subtype == SEQUENCESET */
        result = tinstant_copy(tsequenceset_inst_at_timestamp_excl(
          (TSequenceSet *) temp1, min->t));
    }
  }
  PG_FREE_IF_COPY(temp1, 0);
  PG_FREE_IF_COPY(temp2, 1);
  if (result == NULL)
    PG_RETURN_NULL();
  PG_RETURN_POINTER(result);
}

/*****************************************************************************
 * Nearest approach distance
 *****************************************************************************/

PG_FUNCTION_INFO_V1(NAD_geometry_tnpoint);

PGDLLEXPORT Datum
NAD_geometry_tnpoint(PG_FUNCTION_ARGS)
{
  GSERIALIZED *gs = PG_GETARG_GSERIALIZED_P(0);
  Temporal *temp = PG_GETARG_TEMPORAL(1);
  if (gserialized_is_empty(gs))
  {
    PG_FREE_IF_COPY(gs, 0);
    PG_FREE_IF_COPY(temp, 1);
    PG_RETURN_NULL();
  }

  Datum traj = tnpoint_geom(temp);
  Datum result = call_function2(distance, traj, PointerGetDatum(gs));
  pfree(DatumGetPointer(traj));
  PG_FREE_IF_COPY(gs, 0);
  PG_FREE_IF_COPY(temp, 1);
  PG_RETURN_DATUM(result);
}

PG_FUNCTION_INFO_V1(NAD_npoint_tnpoint);

PGDLLEXPORT Datum
NAD_npoint_tnpoint(PG_FUNCTION_ARGS)
{
  npoint *np = PG_GETARG_NPOINT(0);
  Temporal *temp = PG_GETARG_TEMPORAL(1);
  Datum geom = npoint_as_geom_internal(np);
  GSERIALIZED *gs = (GSERIALIZED *)PG_DETOAST_DATUM(geom);
  Datum traj = tnpoint_geom(temp);
  Datum result = call_function2(distance, traj, PointerGetDatum(gs));
  pfree(DatumGetPointer(traj));
  POSTGIS_FREE_IF_COPY_P(gs, DatumGetPointer(geom));
  pfree(DatumGetPointer(geom));
  PG_FREE_IF_COPY(temp, 1);
  PG_RETURN_POINTER(result);
}

PG_FUNCTION_INFO_V1(NAD_tnpoint_geometry);

PGDLLEXPORT Datum
NAD_tnpoint_geometry(PG_FUNCTION_ARGS)
{
  Temporal *temp = PG_GETARG_TEMPORAL(0);
  GSERIALIZED *gs = PG_GETARG_GSERIALIZED_P(1);
  if (gserialized_is_empty(gs))
  {
    PG_FREE_IF_COPY(temp, 0);
    PG_FREE_IF_COPY(gs, 1);
    PG_RETURN_NULL();
  }

  Datum traj = tnpoint_geom(temp);
  Datum result = call_function2(distance, traj, PointerGetDatum(gs));
  pfree(DatumGetPointer(traj));
  PG_FREE_IF_COPY(temp, 0);
  PG_FREE_IF_COPY(gs, 1);
  PG_RETURN_DATUM(result);
}

PG_FUNCTION_INFO_V1(NAD_tnpoint_npoint);

PGDLLEXPORT Datum
NAD_tnpoint_npoint(PG_FUNCTION_ARGS)
{
  Temporal *temp = PG_GETARG_TEMPORAL(0);
  npoint *np = PG_GETARG_NPOINT(1);
  Datum geom = npoint_as_geom_internal(np);
  GSERIALIZED *gs = (GSERIALIZED *)PG_DETOAST_DATUM(geom);
  Datum traj = tnpoint_geom(temp);
  Datum result = call_function2(distance, traj, PointerGetDatum(gs));
  pfree(DatumGetPointer(traj));
  POSTGIS_FREE_IF_COPY_P(gs, DatumGetPointer(geom));
  pfree(DatumGetPointer(geom));
  PG_FREE_IF_COPY(temp, 0);
  PG_RETURN_POINTER(result);
}

PG_FUNCTION_INFO_V1(NAD_tnpoint_tnpoint);

PGDLLEXPORT Datum
NAD_tnpoint_tnpoint(PG_FUNCTION_ARGS)
{
  Temporal *temp1 = PG_GETARG_TEMPORAL(0);
  Temporal *temp2 = PG_GETARG_TEMPORAL(1);
  Temporal *dist = distance_tnpoint_tnpoint_internal(temp1, temp2);
  if (dist == NULL)
  {
    PG_FREE_IF_COPY(temp1, 0);
    PG_FREE_IF_COPY(temp2, 1);
    PG_RETURN_NULL();
  }

  Datum result = temporal_min_value_internal(dist);
  pfree(dist);
  PG_FREE_IF_COPY(temp1, 0);
  PG_FREE_IF_COPY(temp2, 1);
  PG_RETURN_DATUM(result);
}

/*****************************************************************************
 * ShortestLine
 *****************************************************************************/

PG_FUNCTION_INFO_V1(shortestline_geometry_tnpoint);

PGDLLEXPORT Datum
shortestline_geometry_tnpoint(PG_FUNCTION_ARGS)
{
  GSERIALIZED *gs = PG_GETARG_GSERIALIZED_P(0);
  Temporal *temp = PG_GETARG_TEMPORAL(1);
  if (gserialized_is_empty(gs))
  {
    PG_FREE_IF_COPY(gs, 0);
    PG_FREE_IF_COPY(temp, 1);
    PG_RETURN_NULL();
  }

  Datum traj = tnpoint_geom(temp);
  Datum result = call_function2(LWGEOM_shortestline2d, traj, PointerGetDatum(gs));
  pfree(DatumGetPointer(traj));
  PG_FREE_IF_COPY(gs, 0);
  PG_FREE_IF_COPY(temp, 1);
  PG_RETURN_DATUM(result);
}

PG_FUNCTION_INFO_V1(shortestline_npoint_tnpoint);

PGDLLEXPORT Datum
shortestline_npoint_tnpoint(PG_FUNCTION_ARGS)
{
  npoint *np = PG_GETARG_NPOINT(0);
  Temporal *temp = PG_GETARG_TEMPORAL(1);
  Datum geom = npoint_as_geom_internal(np);
  GSERIALIZED *gs = (GSERIALIZED *)PG_DETOAST_DATUM(geom);
  Datum traj = tnpoint_geom(temp);
  Datum result = call_function2(LWGEOM_shortestline2d, traj, PointerGetDatum(gs));
  pfree(DatumGetPointer(traj));
  POSTGIS_FREE_IF_COPY_P(gs, DatumGetPointer(geom));
  pfree(DatumGetPointer(geom));
  PG_FREE_IF_COPY(temp, 1);
  PG_RETURN_POINTER(result);
}

PG_FUNCTION_INFO_V1(shortestline_tnpoint_geometry);

PGDLLEXPORT Datum
shortestline_tnpoint_geometry(PG_FUNCTION_ARGS)
{
  Temporal *temp = PG_GETARG_TEMPORAL(0);
  GSERIALIZED *gs = PG_GETARG_GSERIALIZED_P(1);
  if (gserialized_is_empty(gs))
  {
    PG_FREE_IF_COPY(temp, 0);
    PG_FREE_IF_COPY(gs, 1);
    PG_RETURN_NULL();
  }

  Datum traj = tnpoint_geom(temp);
  Datum result = call_function2(LWGEOM_shortestline2d, traj, PointerGetDatum(gs));
  pfree(DatumGetPointer(traj));
  PG_FREE_IF_COPY(temp, 0);
  PG_FREE_IF_COPY(gs, 1);
  PG_RETURN_DATUM(result);
}

PG_FUNCTION_INFO_V1(shortestline_tnpoint_npoint);

PGDLLEXPORT Datum
shortestline_tnpoint_npoint(PG_FUNCTION_ARGS)
{
  Temporal *temp = PG_GETARG_TEMPORAL(0);
  npoint *np = PG_GETARG_NPOINT(1);
  Datum geom = npoint_as_geom_internal(np);
  GSERIALIZED *gs = (GSERIALIZED *)PG_DETOAST_DATUM(geom);
  Datum traj = tnpoint_geom(temp);
  Datum result = call_function2(LWGEOM_shortestline2d, traj, PointerGetDatum(gs));
  pfree(DatumGetPointer(traj));
  POSTGIS_FREE_IF_COPY_P(gs, DatumGetPointer(geom));
  pfree(DatumGetPointer(geom));
  PG_FREE_IF_COPY(temp, 0);
  PG_RETURN_POINTER(result);
}

/*****************************************************************************/

PG_FUNCTION_INFO_V1(shortestline_tnpoint_tnpoint);

PGDLLEXPORT Datum
shortestline_tnpoint_tnpoint(PG_FUNCTION_ARGS)
{
  Temporal *temp1 = PG_GETARG_TEMPORAL(0);
  Temporal *temp2 = PG_GETARG_TEMPORAL(1);
  Temporal *sync1, *sync2;
  /* Return NULL if the temporal points do not intersect in time */
  if (!intersection_temporal_temporal(temp1, temp2, SYNCHRONIZE, &sync1, &sync2))
  {
    PG_FREE_IF_COPY(temp1, 0);
    PG_FREE_IF_COPY(temp2, 1);
    PG_RETURN_NULL();
  }

  Temporal *geomsync1 = tnpoint_as_tgeompoint_internal(sync1);
  Temporal *geomsync2 = tnpoint_as_tgeompoint_internal(sync2);
  Datum result;
  bool found = shortestline_tpoint_tpoint_internal(geomsync1, geomsync2, &result);
  pfree(geomsync1); pfree(geomsync2);
  PG_FREE_IF_COPY(temp1, 0);
  PG_FREE_IF_COPY(temp2, 1);
  if (!found)
    PG_RETURN_NULL();
  PG_RETURN_DATUM(result);
}

/*****************************************************************************/
