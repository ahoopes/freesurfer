#define COMPILING_MRISURF_TOPOLOGY_FRIEND
#define COMPILING_MRISURF_METRIC_PROPERTIES_FRIEND

/*
 * @file utilities operating on Original
 *
 */
/*
 * surfaces Author: Bruce Fischl, extracted from mrisurf.c by Bevin Brett
 *
 * $ © copyright-2014,2018 The General Hospital Corporation (Boston, MA) "MGH"
 *
 * Terms and conditions for use, reproduction, distribution and contribution
 * are found in the 'FreeSurfer Software License Agreement' contained
 * in the file 'LICENSE' found in the FreeSurfer distribution, and here:
 *
 * https://surfer.nmr.mgh.harvard.edu/fswiki/FreeSurferSoftwareLicense
 *
 * Reporting: freesurfer@nmr.mgh.harvard.edu
 *
 */
#include "mrisurf_mri.h"
#include "mrisurf_timeStep.h"

MRI *MRISmapToSurface(MRI_SURFACE *mris_src, MRI_SURFACE *mris_dst, MRI *mri_src_features, MRI *mri_dst_features)
{
  int vno_src, vno_dst;
  
  VERTEX *vdst, *vsrc;

  MHT *mht = MHTcreateVertexTable(mris_src, CANONICAL_VERTICES);

  if (mri_dst_features == NULL) mri_dst_features = MRIalloc(mris_dst->nvertices, 1, 1, MRI_FLOAT);
  for (vno_dst = 0; vno_dst < mris_dst->nvertices; vno_dst++) {
    if (vno_dst == Gdiag_no) DiagBreak();
    vdst = &mris_dst->vertices[vno_dst];
    vsrc = MHTfindClosestVertexSet(mht, mris_src, vdst, CANONICAL_VERTICES);
    if (vsrc == NULL) ErrorExit(ERROR_UNSUPPORTED, "could not find v %d", vno_dst);
    vno_src = vsrc - &mris_src->vertices[0];
    if (vno_src == Gdiag_no || vno_dst == Gdiag_no) {
      printf("v %d --> v %d\n", vno_src, vno_dst);
      DiagBreak();
    }
    MRIsetVoxVal(mri_dst_features, vno_dst, 0, 0, 0, MRIgetVoxVal(mri_src_features, vno_src, 0, 0, 0));
  }
  MHTfree(&mht);
  return (mri_dst_features);
}


#define MAX_TOTAL_MOVEMENT 5.0
#define MAX_MOVEMENT .2
#define DELTA_M (MAX_MOVEMENT / 2.0)

#define DEBUG_V 33100

#define MAX_REDUCTIONS 2
#define REDUCTION_PCT 0.5

int MRISpositionSurfaces(MRI_SURFACE *mris, MRI **mri_flash, int nvolumes, INTEGRATION_PARMS *parms)
{
  /*  char   *cp ;*/
  int niterations, n, write_iterations, nreductions = 0, ripped = 0, increased = 0;
  double pial_sse, sse, wm_sse, delta_t = 0.0, dt, l_intensity, base_dt, last_sse, rms, mle_sse, last_mle_sse,
                                pct_sse_decrease, l_repulse, l_surf_repulse, last_wm_sse, last_pial_sse;
  MHT *mht = NULL;
  struct timeb then;
  int msec;
  MRI *mri_brain = mri_flash[0];

  base_dt = parms->dt;
  if (IS_QUADRANGULAR(mris)) {
    MRISremoveTriangleLinks(mris);
  }
  TimerStart(&then);
  parms->mri_smooth = parms->mri_brain = mri_brain;
  niterations = parms->niterations;
  write_iterations = parms->write_iterations;
  if (Gdiag & DIAG_WRITE) {
    char fname[STRLEN];

    if (!parms->fp) {
      sprintf(fname, "%s.%s.out", mris->hemisphere == RIGHT_HEMISPHERE ? "rh" : "lh", parms->base_name);
      if (!parms->start_t) {
        INTEGRATION_PARMS_openFp(parms, fname, "w");
      }
      else {
        INTEGRATION_PARMS_openFp(parms, fname, "a");
      }
      if (!parms->fp) ErrorExit(ERROR_NOFILE, "%s: could not open log file %s", Progname, fname);
    }
    mrisLogIntegrationParms(parms->fp, mris, parms);
  }
  if (Gdiag & DIAG_SHOW) {
    mrisLogIntegrationParms(stdout, mris, parms);
  }

  mrisClearMomentum(mris);
  MRIScomputeMetricProperties(mris);
  MRISstoreMetricProperties(mris);

  MRIScomputeNormals(mris);
  MRISclearD(mris);

  MRISrestoreVertexPositions(mris, ORIGINAL_VERTICES);
  MRIScomputeMetricProperties(mris);
  wm_sse = MRIScomputeSSEExternal(mris, parms, &mle_sse);
  MRISrestoreVertexPositions(mris, PIAL_VERTICES);
  MRIScomputeMetricProperties(mris);

  pial_sse = MRIScomputeSSE(mris, parms);
  sse = last_sse = wm_sse + pial_sse;
  last_mle_sse = mle_sse;
#if 0
  rms = (*gMRISexternalRMS)(mris, parms) ;
#else
  rms = sqrt(mle_sse / (float)mris->nvertices);
#endif

  if (Gdiag & DIAG_SHOW)
    fprintf(stdout,
            "%3.3d: dt: %2.4f, sse=%2.2f, pial sse=%2.2f, "
            "wm sse=%2.2f, rms=%2.2f\n",
            0,
            0.0f,
            (float)sse / (float)mris->nvertices,
            (float)pial_sse / (float)mris->nvertices,
            (float)wm_sse / (float)mris->nvertices,
            (float)rms);
  /*  */
  if (Gdiag & DIAG_WRITE) {
    fprintf(parms->fp,
            "%3.3d: dt: %2.4f, sse=%2.2f, pial sse=%2.2f, "
            "wm sse=%2.2f, rms=%2.2f\n",
            0,
            0.0f,
            (float)sse / (float)mris->nvertices,
            (float)pial_sse / (float)mris->nvertices,
            (float)wm_sse / (float)mris->nvertices,
            rms);
    fflush(parms->fp);
  }

  /* write out initial surface */
  if ((parms->write_iterations > 0) && (Gdiag & DIAG_WRITE) && !parms->start_t) {
    mrisWriteSnapshots(mris, parms, 0);
  }

  dt = parms->dt;
  l_intensity = parms->l_intensity;
  mris->noscale = TRUE;
  l_repulse = parms->l_repulse;
  l_surf_repulse = parms->l_surf_repulse;
  for (n = parms->start_t; n < parms->start_t + niterations; n++) {

    /* compute and apply wm derivatative */
    MRISclearGradient(mris);
    mrisClearExtraGradient(mris);
    if (!increased) {
      if (gMRISexternalClearSSEStatus) {
        (*gMRISexternalClearSSEStatus)(mris);
      }
    }

    MRISrestoreVertexPositions(mris, ORIGINAL_VERTICES); /* make wm positions current */
    MRISsaveVertexPositions(mris, WHITE_VERTICES);
    MRIScomputeMetricProperties(mris);
    if (gMRISexternalGradient)
      mle_sse = (*gMRISexternalGradient)(mris, parms); /* this computes the
                                                          external sse for both
                                                          wm and pial */
    if (increased && gMRISexternalReduceSSEIncreasedGradients) {
      printf("decreasing gradient at vertices with delta SSE>0 by %2.2f\n", 0.5 / increased);
      (*gMRISexternalReduceSSEIncreasedGradients)(mris, 0.5 / increased);
      if (gMRISexternalClearSSEStatus) {
        (*gMRISexternalClearSSEStatus)(mris);
      }
    }

    parms->l_repulse = l_repulse; /* use self-repulsion for wm surface */
    parms->l_surf_repulse = 0;    /* don't repel wm surface
                                     outwards from itself */
    mrisComputePositioningGradients(mris, parms);
    if (!FZERO(parms->l_link)) {
      mrisComputeLinkTerm(mris, parms->l_link, 0);
    }
    if (!(parms->flags & IPFLAG_NO_SELF_INT_TEST)) {
      MHTfree(&mht);
      mht = MHTcreateFaceTable(mris);
    }
    last_wm_sse = MRIScomputeSSEExternal(mris, parms, &last_mle_sse);

    delta_t = mrisAsynchronousTimeStepNew(mris, 0, dt, mht, MAX_ASYNCH_NEW_MM);
    MRISsaveVertexPositions(mris, ORIGINAL_VERTICES);
    if (gMRISexternalTimestep) {
      (*gMRISexternalTimestep)(mris, parms);
    }
    wm_sse = MRIScomputeSSE(mris, parms); /* needs update orig to
                                             compute sse - will compute
                                             external sse later */

    /* store current wm positions in WHITE vertices,
       and pial in INFLATED vertices for undo */
    MRISclearGradient(mris);
    MRISrestoreVertexPositions(mris, PIAL_VERTICES);  /* make pial positions
                                                         current */
    MRISsaveVertexPositions(mris, INFLATED_VERTICES); /* pial->inflated */
    MRIScomputeMetricProperties(mris);
    MRISrestoreExtraGradients(mris);        /* put pial deltas into v->d[xyz] */
    parms->l_repulse = 0;                   /* don't use self-repulsion for pial surface */
    parms->l_surf_repulse = l_surf_repulse; /* repel pial surface
                                               out from wm */

    mrisComputePositioningGradients(mris, parms);
    if (!FZERO(parms->l_link)) {
      mrisComputeLinkTerm(mris, parms->l_link, 1);
    }
    if (!(parms->flags & IPFLAG_NO_SELF_INT_TEST)) {
      MHTfree(&mht);
      mht = MHTcreateFaceTable(mris);
    }
    last_pial_sse = MRIScomputeSSE(mris, parms);

    delta_t += mrisAsynchronousTimeStepNew(mris, 0, dt, mht, MAX_ASYNCH_NEW_MM);
    MRISsaveVertexPositions(mris, PIAL_VERTICES);
    if (gMRISexternalTimestep) {
      (*gMRISexternalTimestep)(mris, parms);
    }
    delta_t /= 2;
    
    pial_sse = MRIScomputeSSEExternal(mris, parms, &mle_sse); /* needs update pial
                                                                 to compute sse.
                                                                 mle_sse includes
                                                                 wm and pial */
    printf("MLE sse %2.3f --> %2.3f, delta = %2.3f\n", last_mle_sse, mle_sse, mle_sse - last_mle_sse);
    last_sse = last_wm_sse + last_pial_sse + last_mle_sse;
    sse = wm_sse + pial_sse; /* pial sse includes current mle_sse */

    pct_sse_decrease = 1 - sse / last_sse;
    pct_sse_decrease = 1 - mle_sse / last_mle_sse; /* only terminate if surfaces have
                                                      asymptoted to desired positions */
    if (pct_sse_decrease < parms->tol)             /* error didn't decrease much */
    {
      nreductions++;
      dt *= .5;

      if (pct_sse_decrease < 0) /* error increased - reject time step */
      {
        increased++;
        printf(
            "error increased by %2.3f%% - time step "
            "reduction #%d: dt=%2.3f, undoing step...\n",
            -100.0f * pct_sse_decrease,
            nreductions,
            dt);
        MRISrestoreVertexPositions(mris, WHITE_VERTICES);
        MRISsaveVertexPositions(mris, ORIGINAL_VERTICES);
        MRISrestoreVertexPositions(mris, INFLATED_VERTICES);
        MRISsaveVertexPositions(mris, PIAL_VERTICES);
        if (gMRISexternalTimestep) {
          (*gMRISexternalTimestep)(mris, parms);
        }
        sse = last_sse;
        mle_sse = last_mle_sse;
        /*        nreductions = MAX_REDUCTIONS+1 ;*/
        if (ripped) {
          break;
        }
        n--; /* don't count this as a time step */
      }
      else {
        printf(
            "error decreased by %2.4f%% - "
            "%dth time step reduction: dt=%2.3f\n",
            100.0f * pct_sse_decrease,
            nreductions,
            dt);
        increased = 0;
      }
      if ((nreductions > MAX_REDUCTIONS)) {
#if 0
        if (ripped == 0)
        {
          nreductions = 0 ;
          dt = parms->dt ;
          ripped = 1 ;
          nreductions = 0 ;
          printf("****** ripping vertices that have asymptoted *****\n") ;
          if (gMRISexternalRipVertices)
          {
            (*gMRISexternalRipVertices)(mris, parms) ;
          }
          continue ;
        }
#endif
        n++;
        break;
      }
    }
    else {
      last_mle_sse = mle_sse;
      increased = 0;
    }

    if (parms->flags & IPFLAG_ADD_VERTICES) {
      float max_len;

      MRISrestoreVertexPositions(mris, PIAL_VERTICES);
      MRIScomputeMetricProperties(mris);
      for (max_len = 1.5 * 8; max_len > 1; max_len /= 2)
        while (MRISdivideLongEdges(mris, max_len) > 0) {
        }

      MRISrestoreVertexPositions(mris, ORIGINAL_VERTICES);
      MRIScomputeMetricProperties(mris);
      for (max_len = 1.5 * 8; max_len > 1; max_len /= 2)
        while (MRISdivideLongEdges(mris, max_len) > 0) {
        }

      if (gMRISexternalTimestep) {
        (*gMRISexternalTimestep)(mris, parms);
      }
    }

    /* recompute sse after external timestep, since outward and inward
       distances will have changed */
    MRISrestoreVertexPositions(mris, ORIGINAL_VERTICES);
    MRIScomputeMetricProperties(mris);
#if 0
    wm_sse = MRIScomputeSSE(mris, parms) ;
    MRISrestoreVertexPositions(mris, PIAL_VERTICES) ;
    MRIScomputeMetricProperties(mris) ;
    pial_sse = MRIScomputeSSE(mris, parms) ;
    sse = last_sse = wm_sse + pial_sse ;
#endif
#if 0
    rms = (*gMRISexternalRMS)(mris, parms) ;
#else
    rms = sqrt(mle_sse / (float)mris->nvertices);
#endif
    if (Gdiag & DIAG_SHOW)
      printf("%3.3d: dt: %2.4f, sse=%2.2f, pial sse=%2.2f, wm sse=%2.2f, rms=%2.2f, (%2.2f%%)\n",
             n + 1,
             (float)delta_t,
             (float)sse / (float)mris->nvertices,
             (float)pial_sse / (float)mris->nvertices,
             (float)wm_sse / (float)mris->nvertices,
             (float)rms,
             100.0f * pct_sse_decrease);

    if (Gdiag & DIAG_WRITE) {
      fprintf(parms->fp,
              "%3.3d: dt: %2.4f, sse=%2.2f, pial sse=%2.2f, "
              "wm sse=%2.2f, rms=%2.2f (%2.2f%%)\n",
              n + 1,
              (float)delta_t,
              (float)sse / (float)mris->nvertices,
              (float)pial_sse / (float)mris->nvertices,
              (float)wm_sse / (float)mris->nvertices,
              (float)rms,
              100.0f * pct_sse_decrease);

      fflush(parms->fp);
    }
    if ((parms->write_iterations > 0) && !((n + 1) % write_iterations) && (Gdiag & DIAG_WRITE)) {
      mrisWriteSnapshots(mris, parms, n + 1);
    }

    if ((Gdiag & DIAG_SHOW) && !((n + 1) % 5) && DIAG_VERBOSE_ON) {
      MRISprintTessellationStats(mris, stderr);
    }
    if ((nreductions > MAX_REDUCTIONS) && ripped) {
      n++; /* count this step */
      break;
    }
  }

  MRISunrip(mris);

  parms->start_t = n;
  parms->dt = base_dt;
  if (Gdiag & DIAG_SHOW) {
    msec = TimerStop(&then);
    fprintf(stdout, "positioning took %2.1f minutes\n", (float)msec / (60 * 1000.0f));
  }
  if (Gdiag & DIAG_WRITE) {
    INTEGRATION_PARMS_closeFp(parms);
  }

  /*  MHTcheckSurface(mris, mht) ;*/
  if (!(parms->flags & IPFLAG_NO_SELF_INT_TEST)) {
    MHTfree(&mht);
  }
  return (NO_ERROR);
}

// #POS
int MRISpositionSurface(MRI_SURFACE *mris, MRI *mri_brain, MRI *mri_smooth, INTEGRATION_PARMS *parms)
{
  int avgs, niterations, n, write_iterations, nreductions = 0, done;
  double sse, delta_t = 0.0, rms, dt, l_intensity, base_dt, last_sse, last_rms, max_mm;
  MHT *mht = NULL, *mht_v_orig = NULL, *mht_v_current = NULL, *mht_f_current = NULL, *mht_pial = NULL;
  struct timeb then;
  int msec;

  printf("Entering MRISpositionSurface()\n");
  max_mm = MIN(MAX_ASYNCH_MM, MIN(mri_smooth->xsize, MIN(mri_smooth->ysize, mri_smooth->zsize)) / 2);
  printf("  max_mm = %g\n",max_mm);
  printf("  MAX_REDUCTIONS = %d, REDUCTION_PCT = %g\n",MAX_REDUCTIONS,REDUCTION_PCT);
  printf("  parms->check_tol = %d \n",parms->check_tol);

  if (!FZERO(parms->l_surf_repulse)) {
    mht_v_orig = MHTcreateVertexTable(mris, ORIGINAL_VERTICES);
  }

  if (parms->l_osurf_repulse > 0)  // repel inwards from outer surface
  {
    mht_pial = MHTcreateVertexTable_Resolution(mris, PIAL_VERTICES, 3.0);
  }
  base_dt = parms->dt;
  if (IS_QUADRANGULAR(mris)) {
    MRISremoveTriangleLinks(mris);
  }
  TimerStart(&then);
  parms->mri_brain = mri_brain;
  parms->mri_smooth = mri_smooth;
  niterations = parms->niterations;
  write_iterations = parms->write_iterations;

  if (Gdiag & DIAG_WRITE) {
    char fname[STRLEN];

    if (!parms->fp) {
      sprintf(fname,
              "%s.%s.out",
              mris->hemisphere == RIGHT_HEMISPHERE ? "rh" : mris->hemisphere == BOTH_HEMISPHERES ? "both" : "lh",
              parms->base_name);
      if (!parms->start_t) {
        INTEGRATION_PARMS_openFp(parms, fname, "w");
      }
      else {
        INTEGRATION_PARMS_openFp(parms, fname, "a");
      }
      if (!parms->fp) ErrorExit(ERROR_NOFILE, "%s: could not open log file %s", Progname, fname);
    }
    mrisLogIntegrationParms(parms->fp, mris, parms);
  }
  if (Gdiag & DIAG_SHOW) {
    mrisLogIntegrationParms(stdout, mris, parms);
  }

  mrisClearMomentum(mris); // v->od{xyz}=0 for unripped
  MRIScomputeMetricProperties(mris);
  MRISstoreMetricProperties(mris);

  MRIScomputeNormals(mris);
  MRISclearD(mris);  // v->d=0 for unripped

  MRISclearCurvature(mris); /* v->curv=0 for unripped, curvature will be used to calculate sulc */


  /* write out initial surface */
  if ((parms->write_iterations > 0) && (Gdiag & DIAG_WRITE) && !parms->start_t) {
    mrisWriteSnapshot(mris, parms, 0);
  }

  avgs = parms->n_averages;

  // Compute initial RMS based on  which cost is non-zero
  if (!FZERO(parms->l_histo)) {
    last_rms = rms = mrisComputeHistoNegativeLikelihood(mris, parms);
  }
  else if (!FZERO(parms->l_map)) {
    int nvox;
    MRISsaveVertexPositions(mris, PIAL_VERTICES);
    if (parms->mri_volume_fractions) MRIfree(&parms->mri_volume_fractions);
    if (parms->mri_dtrans) MRIfree(&parms->mri_dtrans);
    parms->mri_volume_fractions = MRIcomputeLaminarVolumeFractions(mris, parms->resolution, parms->mri_brain, NULL);

    rms = mrisComputeNegativeLogPosterior(mris, parms, &nvox);
    //    last_rms = rms = 1-exp(-rms/nvox) ;
    last_rms = rms = sqrt(rms / nvox);
  }
  else if (!FZERO(parms->l_map2d)) {
    int nvox;
    MRISsaveVertexPositions(mris, PIAL_VERTICES);
    if (parms->mri_volume_fractions) MRIfree(&parms->mri_volume_fractions);
    if (parms->mri_dtrans) MRIfree(&parms->mri_dtrans);
    if ((getenv("READ_VOLS") != NULL)) {
      parms->mri_volume_fractions = MRIread("map2d.vfrac.0000.mgz");
      parms->mri_dtrans = MRIread("dtrans.mgz");
    }
    else
      parms->mri_volume_fractions = MRIcomputeLaminarVolumeFractions(mris, parms->resolution, parms->mri_brain, NULL);

    rms = mrisComputeNegativeLogPosterior2D(mris, parms, &nvox);
    //    last_rms = rms = 1-exp(-rms/nvox) ;
    last_rms = rms = sqrt(rms / nvox);
  }
  else if (!FZERO(parms->l_location)) {
    // Computes the RMS of the distance error (v->{xyz} - v->targ{xyz})
    last_rms = rms = mrisRmsDistanceError(mris);
  }
  else {
    // Intensity RMS (see more notes below)
    last_rms = rms = mrisRmsValError(mris, mri_brain);
  }

  last_sse = sse = MRIScomputeSSE(mris, parms);
  if (DZERO(parms->l_histo) == 0) {
    last_rms = rms = mrisComputeHistoNegativeLikelihood(mris, parms);
  }
  else if (DZERO(parms->l_intensity) && gMRISexternalRMS != NULL && parms->l_external > 0) {
    last_rms = rms = (*gMRISexternalRMS)(mris, parms);
  }
  
  if (Gdiag & DIAG_SHOW) fprintf(stdout, "%3.3d: dt: %2.4f, sse=%2.1f, rms=%2.3f\n", 0, 0.0f, (float)sse, (float)rms);

  if (Gdiag & DIAG_WRITE) {
    fprintf(parms->fp, "%3.3d: dt: %2.4f, sse=%2.1f, rms=%2.3f\n", 0, 0.0f, (float)sse, (float)rms);
    fflush(parms->fp);
  }

  // Loop over iterations ==========================================
  // It may not reach the total number of iterations because, on each
  // iteration, it decides whether the step size needs to be
  // reduced. Only a certain number (MAX_REDUCTIONS+1) of reductions
  // are allowed after which it will break from the iteration loop.
  // This can make the number of iterations parameter much less
  // important than it first appears.
  dt = parms->dt;
  l_intensity = parms->l_intensity;
  for (n = parms->start_t; n < parms->start_t + niterations; n++) {

    parms->t = n;
    if (!FZERO(parms->l_repulse)) {
      MHTfree(&mht_v_current);
      mht_v_current = MHTcreateVertexTable(mris, CURRENT_VERTICES);
      MHTfree(&mht_f_current); mht_f_current = MHTcreateFaceTable(mris);
    }
    if (!(parms->flags & IPFLAG_NO_SELF_INT_TEST)) {
      MHTfree(&mht); mht = MHTcreateFaceTable(mris);
    }
    MRISclearGradient(mris);

    // Compute the gradient direction
    mrisComputeTargetLocationTerm(mris, parms->l_location, parms);
    mrisComputeIntensityTerm(mris, l_intensity, mri_brain, mri_smooth, parms->sigma, parms);
    mrisComputeShrinkwrapTerm(mris, mri_brain, parms->l_shrinkwrap);
    mrisComputeExpandwrapTerm(mris, mri_brain, parms->l_expandwrap);
    mrisComputeIntensityGradientTerm(mris, parms->l_grad, mri_brain, mri_smooth);
    mrisComputeSurfaceRepulsionTerm(mris, parms->l_surf_repulse, mht_v_orig);
    mrisComputeHistoTerm(mris, parms);
    mrisComputePosteriorTerm(mris, parms);
    mrisComputePosterior2DTerm(mris, parms);
    if (parms->l_osurf_repulse > 0)
      mrisComputeWhichSurfaceRepulsionTerm(mris, -parms->l_osurf_repulse, mht_pial, PIAL_VERTICES, .1);
    if (gMRISexternalGradient) {
      (*gMRISexternalGradient)(mris, parms);
    }
    /*mrisMarkSulcalVertices(mris, parms) ;*/
    mrisComputeLaplacianTerm(mris, parms->l_lap);
    mrisAverageSignedGradients(mris, avgs);
    /*mrisUpdateSulcalGradients(mris, parms) ;*/
    /* smoothness terms */
    mrisComputeSpringTerm(mris, parms->l_spring);
    mrisComputeNormalizedSpringTerm(mris, parms->l_spring_norm);
    mrisComputeRepulsiveTerm(mris, parms->l_repulse, mht_v_current, mht_f_current);
    mrisComputeThicknessSmoothnessTerm(mris, parms->l_tsmooth, parms);
    mrisComputeThicknessMinimizationTerm(mris, parms->l_thick_min, parms);
    mrisComputeThicknessParallelTerm(mris, parms->l_thick_parallel, parms);
    mrisComputeNormalSpringTerm(mris, parms->l_nspring);
    mrisComputeQuadraticCurvatureTerm(mris, parms->l_curv);
    /*mrisComputeAverageNormalTerm(mris, avgs, parms->l_nspring) ;*/
    /*mrisComputeCurvatureTerm(mris, parms->l_curv) ;*/
    mrisComputeNonlinearSpringTerm(mris, parms->l_nlspring, parms);
    mrisComputeTangentialSpringTerm(mris, parms->l_tspring);
    mrisComputeNonlinearTangentialSpringTerm(mris, parms->l_nltspring, parms->min_dist);
    mrisComputeMaxSpringTerm(mris, parms->l_max_spring);
    mrisComputeAngleAreaTerms(mris, parms);

    // This do loop will move the vertices along the direction
    // computed above. It may adjust the step size for the next iter.
    // If the RMS increased, then it will redo this iter with a
    // smaller step. Only a certain number (MAX_REDUCTIONS+1) of
    // reductions are allowed after which it will break from the
    // iteration loop. The way it is set up, it always forces the RMS
    // (intensity or distgance error, etc) to drop regardless of what
    // factors are included in SSE and gradient calculations. Given
    // that the gradient includes other terms (eg, repulse, curv, tang
    // and norm spring), the direction may not always result in a
    // decrease of in RMS. Some terms (eg, curv and nspring) don't
    // even have functions that compute the SSE. Annectotally, the
    // intensity SSE is an order of mag > than the other SSEs.
    do { // do loops alway execute at least once
      // save vertex positions in case we have to reject this step
      MRISsaveVertexPositions(mris, TMP2_VERTICES);

      mrisScaleTimeStepByCurvature(mris);

      MRISclearMarks(mris); //v->marked=0

      // Take a step by changing the v->{x,y,z} of all vertices
      delta_t = mrisAsynchronousTimeStep(mris, parms->momentum, dt, mht, max_mm);
      parms->t = n + 1;                                           // for diags

      if (Gdiag_no >= 0 && mris->vertices[Gdiag_no].marked == 0)  // diag vertex was cropped
        DiagBreak();

      if (parms->smooth_intersections) {
        MRISerodeMarked(mris, 4);
        if (Gdiag_no >= 0 && mris->vertices[Gdiag_no].marked == 0)  // diag vertex was cropped
          DiagBreak();
        MRISsoapBubbleVertexPositions(mris, 500);
      }

      if (parms->uncompress)
        MRISremoveCompressedRegions(mris, .2);

      if (gMRISexternalTimestep) {
        (*gMRISexternalTimestep)(mris, parms);
      }
      if (!(parms->flags & IPFLAG_NO_SELF_INT_TEST)) {
        MHTcheckFaces(mris, mht);
      }

      MRIScomputeMetricProperties(mris);

      // Compute RMS using one of various methods
      if (!FZERO(parms->l_histo)) {
        rms = mrisComputeHistoNegativeLikelihood(mris, parms);
      }
      else if (!FZERO(parms->l_map)) {
        int nvox;
        MRISsaveVertexPositions(mris, PIAL_VERTICES);
        if (parms->mri_volume_fractions) MRIfree(&parms->mri_volume_fractions);
        parms->mri_volume_fractions = MRIcomputeLaminarVolumeFractions(mris, parms->resolution, parms->mri_brain, NULL);
        rms = mrisComputeNegativeLogPosterior(mris, parms, &nvox);
        rms = sqrt(rms / nvox);
      }
      else if (!FZERO(parms->l_map2d)) {
        int nvox;
        MRISsaveVertexPositions(mris, PIAL_VERTICES);
        if (parms->mri_volume_fractions) MRIfree(&parms->mri_volume_fractions);
        if (parms->mri_dtrans) MRIfree(&parms->mri_dtrans);
        parms->mri_volume_fractions = MRIcomputeLaminarVolumeFractions(mris, parms->resolution, parms->mri_brain, NULL);
        rms = mrisComputeNegativeLogPosterior2D(mris, parms, &nvox);
        rms = sqrt(rms / nvox);
      }
      else if (!FZERO(parms->l_location)) {
	// Computes the RMS of the distance error (v->{xyz} - v->targ{xyz})
        rms = mrisRmsDistanceError(mris);
      }
      else if (DZERO(parms->l_intensity) && gMRISexternalRMS != NULL && parms->l_external > 0) {
        rms = (*gMRISexternalRMS)(mris, parms);
      }
      else {
	// This RMS is only for the intensity cost. This is different than SSE below in 
	// that SSE is not normalized for the number of vertices, includes all the costs
	// that have non-zero weight each of which are weighted by their weights. 
	// RMS = sqrt(SSE/nvert) when all weights are zero except for l_intensity
	// and l_intensity=1. Even then it will only be equal when nvert is the number
	// of unripped vertices. 
        rms = mrisRmsValError(mris, mri_brain);
      }
      
      if (debugNonDeterminism) {
        fprintf(stdout, "%s:%d stdout ",__FILE__,__LINE__);
        mris_print_hash(stdout, mris, "Input to MRIScomputeSSE ", "\n");
      }

      // Comute SSE. This differs from RMS in that RMS may only have a
      // contribution from intensity where as SSE has a contribution
      // from any component with a non-zero weight, and the components
      // are weighted. The SSE is summed over the number of vertices
      // which makes it resolution dependent. However, it is evaluated
      // in as a ratio, so the number of verts divides out. 
      sse = MRIScomputeSSE(mris, parms);

      done = 1; // assume done with this step unless there is an increase in RMS (below)

      // This next section is doing a couple of things:
      // A. It is determining whether it to reduce the step size on the next iteration.
      // B. It will force a rerun this iteration with the smaller step if the RMS increased.
      // C. It will eventually cause a break from the iteration loop if the maximum number 
      //    of reductions is hit (ie, an alternative stopping criteria)
      // Note: the RMS may refer to the itensity or location criteria or something else 
      // depending on above. It always applies regardless of the weight applied to the cost.
      // (as long as it is nonzero).
      // There are three criteria for reducing the step size:
      //   1. RMS *fraction* reduced by less than tolerance (requires parms->check_tol=1 which is
      //      NOT the case by default for white surface placement).
      //   2. SSE *percent* reduced by less than tolerance (requires l_location=0 which is
      //      the case by default for white surface placement). It would seems unlikely
      //      that #2 would ever be met given that the tol is often 10e-4, but it does happen
      //   3. RMS *value* reduced by less than .05 (requires parms->check_tol=0 && location=0
      //      which is the case by default for white surface placement). This is probably 
      //      the factor that dictates when a reduction occurs. 
      // #2 and #3 generally apply during intensity optimization
      // #1 generally applies during distance optimization
      if (((parms->check_tol && ((last_rms - rms) / last_rms < parms->tol))) ||
          ((FZERO(parms->l_location) && (100 * (last_sse - sse) / last_sse < parms->tol))) ||
          ((parms->check_tol == 0) && FZERO(parms->l_location) && (rms > last_rms - 0.05)) ) {
        nreductions++;
        parms->dt *= REDUCTION_PCT; // hidden parameter, generally 0.5 (not a percent)
        dt = parms->dt;
        mrisClearMomentum(mris);

	int aa, bb, cc; // These indicate which reason the reduction took place
	aa = ((parms->check_tol && ((last_rms - rms) / last_rms < parms->tol)));
	bb = ((FZERO(parms->l_location) && (100 * (last_sse - sse) / last_sse < parms->tol)));
	cc = ((parms->check_tol == 0) && FZERO(parms->l_location) && (rms > last_rms - 0.05));
        printf("rms = %5.4f/%5.4f, sse=%2.1f/%2.1f, time step reduction %d of %d to %2.3f  %d %d %d\n",
	       rms, last_rms, sse, last_sse, nreductions, MAX_REDUCTIONS+1, dt,aa,bb,cc);

        if ((FZERO(parms->l_location)) && (rms > last_rms)){
	  /* error increased - reject step */
	  printf("   RMS increased, rejecting step\n");
          MRISrestoreVertexPositions(mris, TMP2_VERTICES);
          MRIScomputeMetricProperties(mris);
          /* if error increased and we've only reduced the time step a
          few times, try taking a smaller step (done=0). */
          done = (nreductions > MAX_REDUCTIONS);
        }
      }

      if (Gdiag_no >= 0 && DIAG_VERBOSE_ON) 
        MRISprintVertexStats(mris, Gdiag_no, Gstdout, CURRENT_VERTICES);

    } while (!done); // do loop

    mrisTrackTotalDistanceNew(mris); /* computes signed deformation amount */

    if (Gdiag & DIAG_SHOW){
      printf("%3.3d: dt: %2.4f, sse=%2.1f, rms=%2.3f (%2.3f%%)\n",
	     n + 1, (float)delta_t,(float)sse,(float)rms,100 * (last_rms - rms) / last_rms);
      fflush(stdout);
    }

    if (Gdiag & DIAG_WRITE) {
      fprintf(parms->fp,"%3.3d: dt: %2.4f, sse=%2.1f, rms=%2.3f (%2.3f%%)\n",
              n + 1,(float)delta_t,(float)sse, (float)rms,100 * (last_rms - rms) / last_rms);
      fflush(parms->fp);
    }

    if ((parms->write_iterations > 0) && !((n + 1) % write_iterations) && (Gdiag & DIAG_WRITE)) {
      mrisWriteSnapshot(mris, parms, n + 1);
    }

    if ((Gdiag & DIAG_SHOW) && !((n + 1) % 5) && DIAG_VERBOSE_ON) {
      MRISprintTessellationStats(mris, stderr);
    }
    if (Gdiag_no >= 0) {
      double xv, yv, zv;
      VERTEX *v;
      v = &mris->vertices[Gdiag_no];
      MRISvertexToVoxel(mris, v, mri_brain, &xv, &yv, &zv);
      printf("v %d: (%2.1f, %2.1f, %2.1f), vox = (%2.0f, %2.0f %2.0f)\n", Gdiag_no, v->x, v->y, v->z, xv, yv, zv);
    }
    if(nreductions > MAX_REDUCTIONS) {
      printf("  maximum number of reductions reached, breaking from loop\n");fflush(stdout);
      n++; /* count this step */
      break;
    }
    last_sse = sse;
    last_rms = rms;
  } // end loop over iterations

  parms->start_t = n;
  parms->dt = base_dt;
  if (Gdiag & DIAG_SHOW) {
    msec = TimerStop(&then);
    fprintf(stdout, "positioning took %2.1f minutes\n", (float)msec / (60 * 1000.0f));
  }
  if (Gdiag & DIAG_WRITE) {
    INTEGRATION_PARMS_closeFp(parms);
  }

  /*  MHTcheckSurface(mris, mht) ;*/
  if (!(parms->flags & IPFLAG_NO_SELF_INT_TEST) && mht) {
    MHTfree(&mht);
  }
  if (mht_v_current) {
    MHTfree(&mht_v_current);
  }
  if (mht_f_current) {
    MHTfree(&mht_f_current);
  }
  if (mht_v_orig) {
    MHTfree(&mht_v_orig);
  }

  return (NO_ERROR);
}

int MRISpositionSurface_mef(
    MRI_SURFACE *mris, MRI *mri_30, MRI *mri_5, INTEGRATION_PARMS *parms, float weight30, float weight5)
{
  /*  char   *cp ;*/
  int avgs, niterations, n, write_iterations, nreductions = 0, done;
  double delta_t = 0.0, rms, dt, l_intensity, base_dt, last_rms, max_mm, sse, last_sse, delta_rms;
  MHT *mht = NULL, *mht_v_orig = NULL, *mht_v_current = NULL, *mht_f_current = NULL;
  struct timeb then;
  int msec;

  max_mm = MIN(MAX_ASYNCH_MM, MIN(mri_30->xsize, MIN(mri_30->ysize, mri_30->zsize)) / 2);

  // note that the following is for pial surface avoid intersection with white
  if (!FZERO(parms->l_surf_repulse)) {
    mht_v_orig = MHTcreateVertexTable(mris, ORIGINAL_VERTICES);
  }

  base_dt = parms->dt;
  if (IS_QUADRANGULAR(mris)) {
    MRISremoveTriangleLinks(mris);
  }
  TimerStart(&then);
  // the following are used in mrisComputeIntensityError() and computeSSE()
  parms->mri_brain = NULL;          // mri_30 ;
  parms->mri_smooth = NULL;         // mri_5 ;
  niterations = parms->niterations; /* should be different for white and pial;
                                       yeah 25 for white and 30 for pial */
  write_iterations = parms->write_iterations;
  if (Gdiag & DIAG_WRITE) {
    char fname[STRLEN];

    if (!parms->fp) {
      sprintf(fname, "%s.%s.out", mris->hemisphere == RIGHT_HEMISPHERE ? "rh" : "lh", parms->base_name);
      if (!parms->start_t) {
        INTEGRATION_PARMS_openFp(parms, fname, "w");
      }
      else {
        INTEGRATION_PARMS_openFp(parms, fname, "a");
      }
      if (!parms->fp) ErrorExit(ERROR_NOFILE, "%s: could not open log file %s", Progname, fname);
    }
    mrisLogIntegrationParms(parms->fp, mris, parms);
  }
  if (Gdiag & DIAG_SHOW) {
    mrisLogIntegrationParms(stdout, mris, parms);
  }

  mrisClearMomentum(mris);
  MRIScomputeMetricProperties(mris);
  MRISstoreMetricProperties(mris);

  MRIScomputeNormals(mris);
  MRISclearD(mris);

  MRISclearCurvature(mris); /* curvature will be used to calculate sulc */

  /* write out initial surface */
  if ((parms->write_iterations > 0) && (Gdiag & DIAG_WRITE) && !parms->start_t) {
    mrisWriteSnapshot(mris, parms, 0);
  }

  avgs = parms->n_averages;
  last_rms = rms = mrisRmsValError_mef(mris, mri_30, mri_5, weight30, weight5);
  last_sse = sse = mrisComputeSSE_MEF(mris, parms, mri_30, mri_5, weight30, weight5, mht_v_orig);
  // this computation results were never used

  if (Gdiag & DIAG_SHOW) fprintf(stdout, "%3.3d: dt: %2.4f, rms=%2.2f\n", 0, 0.0f, (float)rms);

  if (Gdiag & DIAG_WRITE) {
    fprintf(parms->fp, "%3.3d: dt: %2.4f, rms=%2.2f\n", 0, 0.0f, (float)rms);
    fflush(parms->fp);
  }

  dt = parms->dt;
  l_intensity = parms->l_intensity;
  for (n = parms->start_t; n < parms->start_t + niterations; n++) {
    if (!FZERO(parms->l_repulse)) {
      MHTfree(&mht_v_current);
      mht_v_current = MHTcreateVertexTable(mris, CURRENT_VERTICES);
      MHTfree(&mht_f_current); mht_f_current = MHTcreateFaceTable(mris);
    }
    if (!(parms->flags & IPFLAG_NO_SELF_INT_TEST)) {
      MHTfree(&mht); MHTcreateFaceTable(mris);
    }
    MRISclearGradient(mris);
    mrisComputeIntensityTerm_mef(mris, l_intensity, mri_30, mri_5, parms->sigma, weight30, weight5, parms);
    // the following term is not used for white, but used for pial!
    mrisComputeSurfaceRepulsionTerm(mris, parms->l_surf_repulse, mht_v_orig);
#if 1
    mrisAverageSignedGradients(mris, avgs);
#else
    mrisAverageWeightedGradients(mris, avgs);
#endif
    /*                mrisUpdateSulcalGradients(mris, parms) ;*/

    /* smoothness terms */
    mrisComputeSpringTerm(mris, parms->l_spring);
    mrisComputeLaplacianTerm(mris, parms->l_lap);
    mrisComputeNormalizedSpringTerm(mris, parms->l_spring_norm);
    mrisComputeRepulsiveTerm(mris, parms->l_repulse, mht_v_current, mht_f_current);
    mrisComputeThicknessSmoothnessTerm(mris, parms->l_tsmooth, parms);
    mrisComputeThicknessMinimizationTerm(mris, parms->l_thick_min, parms);
    mrisComputeThicknessParallelTerm(mris, parms->l_thick_parallel, parms);
    mrisComputeNormalSpringTerm(mris, parms->l_nspring);
    mrisComputeQuadraticCurvatureTerm(mris, parms->l_curv);
    mrisComputeNonlinearSpringTerm(mris, parms->l_nlspring, parms);
    mrisComputeTangentialSpringTerm(mris, parms->l_tspring);
    mrisComputeNonlinearTangentialSpringTerm(mris, parms->l_nltspring, parms->min_dist);

    do {
      MRISsaveVertexPositions(mris, WHITE_VERTICES);
      delta_t = mrisAsynchronousTimeStep(mris, parms->momentum, dt, mht, max_mm);
      if (!(parms->flags & IPFLAG_NO_SELF_INT_TEST)) {
        MHTcheckFaces(mris, mht);
      }
      MRIScomputeMetricProperties(mris);
      rms = mrisRmsValError_mef(mris, mri_30, mri_5, weight30, weight5);
      sse = mrisComputeSSE_MEF(mris, parms, mri_30, mri_5, weight30, weight5, mht_v_orig);
      done = 1;
      if (parms->check_tol) {
        delta_rms = parms->tol * last_rms;
      }
      else {
        delta_rms = 0.05;  // don't worry about energy functional decreasing, just continue
      }
      if (parms->check_tol && (rms > last_rms - delta_rms))  // error increased - reduce step size
      {
        nreductions++;
        parms->dt *= REDUCTION_PCT;
        dt = parms->dt;
        fprintf(stdout,
                "rms = %2.2f, time step reduction %d of %d to %2.3f...\n",
                rms,
                nreductions,
                MAX_REDUCTIONS + 1,
                dt);
        mrisClearMomentum(mris);
        if (rms > last_rms) /* error increased - reject step */
        {
          MRISrestoreVertexPositions(mris, WHITE_VERTICES);
          MRIScomputeMetricProperties(mris);

          /* if error increased and we've only reduced the time
             step a few times, try taking a smaller step (done=0).
          */
          done = (nreductions > MAX_REDUCTIONS);
        }
      }
    } while (!done);
    last_sse = sse;
    last_rms = rms;

    mrisTrackTotalDistanceNew(mris); /* computes signed
                                        deformation amount */
    parms->rms = rms = mrisRmsValError_mef(mris, mri_30, mri_5, weight30, weight5);
    //  sse = MRIScomputeSSE(mris, parms) ;
    if (Gdiag & DIAG_SHOW) fprintf(stdout, "%3.3d: dt: %2.4f, rms=%2.2f\n", n + 1, (float)delta_t, (float)rms);

    if (Gdiag & DIAG_WRITE) {
      fprintf(parms->fp, "%3.3d: dt: %2.4f, rms=%2.2f\n", n + 1, (float)delta_t, (float)rms);
      fflush(parms->fp);
    }

    if ((parms->write_iterations > 0) && !((n + 1) % write_iterations) && (Gdiag & DIAG_WRITE)) {
      mrisWriteSnapshot(mris, parms, n + 1);
    }

    if ((Gdiag & DIAG_SHOW) && !((n + 1) % 5) && DIAG_VERBOSE_ON) {
      MRISprintTessellationStats(mris, stderr);
    }
    if (nreductions > MAX_REDUCTIONS) {
      n++; /* count this step */
      break;
    }

    if (gMRISexternalTimestep) {
      (*gMRISexternalTimestep)(mris, parms);
    }
  }

  parms->start_t = n;
  parms->dt = base_dt;
  if (Gdiag & DIAG_SHOW) {
    msec = TimerStop(&then);
    fprintf(stdout, "positioning took %2.1f minutes\n", (float)msec / (60 * 1000.0f));
  }
  if (Gdiag & DIAG_WRITE) {
    INTEGRATION_PARMS_closeFp(parms);
  }

  /*  MHTcheckSurface(mris, mht) ;*/
  if (!(parms->flags & IPFLAG_NO_SELF_INT_TEST)) {
    MHTfree(&mht);
  }
  if (mht_v_current) {
    MHTfree(&mht_v_current);
  }
  if (mht_f_current) {
    MHTfree(&mht_f_current);
  }
  if (mht_v_orig) {
    MHTfree(&mht_v_orig);
  }
  return (NO_ERROR);
}

int MRISmoveSurface(MRI_SURFACE *mris, MRI *mri_brain, MRI *mri_smooth, INTEGRATION_PARMS *parms)
{
  /*  char   *cp ;*/
  double sse_before, sse_after, rms_before, rms_after;
  MHT *mht = NULL;
  int vno;
  VERTEX *v;

  if (IS_QUADRANGULAR(mris)) {
    MRISremoveTriangleLinks(mris);
  }
  parms->mri_brain = mri_brain;
  parms->mri_smooth = mri_smooth;
  if (Gdiag & DIAG_WRITE) {
    char fname[STRLEN];

    if (!parms->fp) {
      sprintf(fname, "%s.%s.out", mris->hemisphere == RIGHT_HEMISPHERE ? "rh" : "lh", parms->base_name);
      if (!parms->start_t) {
        INTEGRATION_PARMS_openFp(parms, fname, "w");
      }
      else {
        INTEGRATION_PARMS_openFp(parms, fname, "a");
      }
      if (!parms->fp) ErrorExit(ERROR_NOFILE, "%s: could not open log file %s", Progname, fname);
    }
  }

  MRIScomputeMetricProperties(mris);
  MRISstoreMetricProperties(mris);

  MRIScomputeNormals(mris);

  rms_before = mrisRmsValError(mris, mri_brain);
  sse_before = MRIScomputeSSE(mris, parms);
  if (Gdiag & DIAG_SHOW)
    fprintf(stdout, "before expansion, sse = %2.3f, rms = %2.3f\n", (float)sse_before, (float)rms_before);

  if (Gdiag & DIAG_WRITE) {
    /* write out initial surface */
    if (parms->write_iterations > 0) {
      fprintf(stdout, "writing out pre expansion surface.\n");
      MRISwrite(mris, "pre");
    }
    fprintf(parms->fp, "before expansion, sse = %2.1f, rms = %2.1f\n", (float)sse_before, (float)rms_before);
    fflush(parms->fp);
  }

  if (!(parms->flags & IPFLAG_NO_SELF_INT_TEST)) {
    MHTfree(&mht); mht = MHTcreateFaceTable(mris);
  }

  for (vno = 0; vno < mris->nvertices; vno++) {
    v = &mris->vertices[vno];
    if (v->ripflag) {
      continue;
    }
    if (vno == Gdiag_no) {
      DiagBreak();
    }
    v->dx = v->nx * v->d;
    v->dy = v->ny * v->d;
    v->dz = v->nz * v->d;
  }
  mrisAsynchronousTimeStep(mris, 0.0, 1.0, mht, 3.0f);
  MRIScomputeMetricProperties(mris);
  rms_after = mrisRmsValError(mris, mri_brain);
  sse_after = MRIScomputeSSE(mris, parms);
  if (Gdiag & DIAG_SHOW)
    fprintf(stdout, "after expansion, sse = %2.1f, rms = %2.1f\n", (float)sse_after, (float)rms_after);

  if (Gdiag & DIAG_WRITE) {
    if (parms->write_iterations > 0) {
      fprintf(stdout, "writing post expansion surface...\n");
      MRISwrite(mris, "post");
    }
    fprintf(parms->fp, "after expansion, sse = %2.3f, rms = %2.3f\n", (float)sse_after, (float)rms_after);
    fflush(parms->fp);
  }

  if (Gdiag & DIAG_WRITE) {
    INTEGRATION_PARMS_closeFp(parms);
  }

  /*  MHTcheckSurface(mris, mht) ;*/
  if (!(parms->flags & IPFLAG_NO_SELF_INT_TEST)) {
    MHTfree(&mht);
  }
  return (NO_ERROR);
}
/*-----------------------------------------------------
  Parameters:

  Returns value:

  Description
  ------------------------------------------------------*/
MRI *MRISwriteSurfaceIntoVolume(MRI_SURFACE *mris, MRI *mri_template, MRI *mri)
{
  int fno;

  if (!mri) {
    mri = MRIalloc(256 * 2, 256 * 2, 256 * 2, MRI_BITMAP);  // assumes the volume is 512^3
    MRIcopyHeader(mri_template, mri);
    MRIsetResolution(mri, 0.5, 0.5, 0.5);  // resolution to 0.5 mm
    mri->xstart = mri_template->xstart;
    mri->xend = mri_template->xend;
    mri->ystart = mri_template->ystart;
    mri->yend = mri_template->yend;
    mri->zstart = mri_template->zstart;
    mri->zend = mri_template->zend;
  }
  else {
    MRIclear(mri);
  }

  for (fno = 0; fno < mris->nfaces; fno++) {
    mrisFillFace(mris, mri, fno);
  }

  return (mri);
}
/*-----------------------------------------------------
  Parameters:

  Returns value:

  Description
  Fill in a triangle to prevent self-intersection at SAMPLE_DIST
  intervals (should be 1/2 resolution of mri volume).

  V0    b     V2
  o----------o
  |        /
  |      /
  a |    /
  |  /
  |/
  o
  V1      b        V2
  ------------------------------------------------------*/
int mrisFillFace(MRI_SURFACE *mris, MRI *mri, int fno) { return (mrisHatchFace(mris, mri, fno, 1)); }
/*-----------------------------------------------------
  Parameters:

  Returns value:

  Description
  Description
  each face has 2 triangles defined by it:

  V0    b     V2
  o----------o
  |        /
  |      /
  a |    /
  |  /
  |/
  o
  V1      b        V2
  ------------------------------------------------------*/
#define SAMPLE_DIST 0.25

int mrisHatchFace(MRI_SURFACE *mris, MRI *mri, int fno, int on)
{
  double x, y, z, xa, ya, za, xc, yc, zc, t0, t1, adx, ady, adz, dx, dy, dz, cdx, cdy, cdz, alen, clen, delta_t0,
      delta_t1, len;
  int xv, yv, zv, i;
  VERTEX *v0, *v1, *v2;
  FACE *face;

  face = &mris->faces[fno];
  if (face->ripflag) {
    return (NO_ERROR);
  }

  for (i = 0; i < 1; i++) {
    switch (i) {
      default:
      case 0:
        v0 = &mris->vertices[face->v[0]];
        v1 = &mris->vertices[face->v[1]];
        v2 = &mris->vertices[face->v[2]];
        break;
      case 1:
        v0 = &mris->vertices[face->v[1]];
        v1 = &mris->vertices[face->v[2]];
        v2 = &mris->vertices[face->v[0]];
        break;
      case 2:
        v0 = &mris->vertices[face->v[2]];
        v1 = &mris->vertices[face->v[0]];
        v2 = &mris->vertices[face->v[1]];
        break;
    }

    v0 = &mris->vertices[face->v[0]];
    v1 = &mris->vertices[face->v[1]];
    v2 = &mris->vertices[face->v[2]];
    adx = v1->x - v0->x;
    ady = v1->y - v0->y;
    adz = v1->z - v0->z;
    alen = sqrt(SQR(adx) + SQR(ady) + SQR(adz));
    cdx = v2->x - v0->x;
    cdy = v2->y - v0->y;
    cdz = v2->z - v0->z;
    clen = sqrt(SQR(cdx) + SQR(cdy) + SQR(cdz));

    /*
      sample along legs of the triangle making sure the maximum spacing
      between samples (along the longer leg) is SAMPLE_DIST.
    */

    /*
      move along v0->v1 and v3->v2 lines and
      draw in crossing line to fill face
      t0 parameterizes lines from v0->v1 and v0->v2
    */
    if (FZERO(alen) && FZERO(clen)) {
      delta_t0 = 0.99;
    }
    else
      delta_t0 = (alen > clen) ? (SAMPLE_DIST / alen) : (SAMPLE_DIST / clen);
    if (FZERO(delta_t0))
      ErrorReturn(ERROR_BADPARM, (ERROR_BADPARM, "mrisFillFace: face %d has infinite leg (%d, %d)\n", fno, alen, clen));

    if (delta_t0 >= 1.0) {
      delta_t0 = 0.99;
    }

    /* delta_t0 is % of alen or clen (whichever is bigger) of SAMPLE_DIST */
    for (t0 = 0; t0 <= 1.0f; t0 += delta_t0) {
      /* compute points (xa,ya,za) and (xc,yc,zc)
         on the a and c lines resp. */
      xa = v0->x + t0 * adx;
      ya = v0->y + t0 * ady;
      za = v0->z + t0 * adz;
      xc = v0->x + t0 * cdx;
      yc = v0->y + t0 * cdy;
      zc = v0->z + t0 * cdz;
      dx = xc - xa;
      dy = yc - ya;
      dz = zc - za;
      len = sqrt(SQR(dx) + SQR(dy) + SQR(dz));
      if (FZERO(len)) {
        delta_t1 = 0.99;
      }
      else {
        delta_t1 = SAMPLE_DIST / len; /* sample at SAMPLE_DIST intervals */
        if (delta_t1 >= 1.0f) {
          delta_t1 = 0.99;
        }
      }

      /* now draw a line from (xa,ya,za) to (xc, yc, zc) */
      for (t1 = 0; t1 <= 1.0f; t1 += delta_t1) {
        /* compute a point on the line connecting a and c */
        x = xa + t1 * dx;
        y = ya + t1 * dy;
        z = za + t1 * dz;
        // MRIworldToVoxel(mri, x,y,z,&x,&y,&z);/* volume coordinate */
        MRISsurfaceRASToVoxel(mris, mri, x, y, z, &x, &y, &z); /* volume coordinate */
        xv = nint(x);
        yv = nint(y);
        zv = nint(z); /* voxel coordinate */
        if (on) {
          MRIset_bit(mri, xv, yv, zv); /* mark it filled */
        }
        else {
          MRIclear_bit(mri, xv, yv, zv); /* mark it empty */
        }
      }
      /* compute last point on line */
      t1 = 1.0f;
      x = xa + t1 * dx;
      y = ya + t1 * dy;
      z = za + t1 * dz;
      MRISsurfaceRASToVoxelCached(mris, mri, x, y, z, &x, &y, &z);  // volume coordinate
      xv = nint(x);
      yv = nint(y);
      zv = nint(z); /* voxel coordinate */
      if (on) {
        MRIset_bit(mri, xv, yv, zv); /* mark it filled */
      }
      else {
        MRIclear_bit(mri, xv, yv, zv); /* mark it empty */
      }
    }

    /* compute last line on the a and c lines resp. */
    t0 = 1.0f;
    xa = v0->x + t0 * adx;
    ya = v0->y + t0 * ady;
    za = v0->z + t0 * adz;
    xc = v0->x + t0 * cdx;
    yc = v0->y + t0 * cdy;
    zc = v0->z + t0 * cdz;
    dx = xc - xa;
    dy = yc - ya;
    dz = zc - za;
    len = sqrt(SQR(dx) + SQR(dy) + SQR(dz));
    if (FZERO(len)) {
      delta_t1 = 0.99;
    }
    else {
      delta_t1 = SAMPLE_DIST / len; /* sample at SAMPLE_DIST intervals */
      if (delta_t1 >= 1.0f) {
        delta_t1 = 0.99;
      }
    }

    /* now draw a line from (xa,ya,za) to (xc, yc, zc) */
    for (t1 = 0; t1 <= 1.0f; t1 += delta_t1) {
      /* compute a point on the line connecting a and c */
      x = xa + t1 * dx;
      y = ya + t1 * dy;
      z = za + t1 * dz;
      // MRIworldToVoxel(mri, x, y, z, &x, &y, &z);/* volume coordinate */
      MRISsurfaceRASToVoxelCached(mris, mri, x, y, z, &x, &y, &z);  // volume coordinate
      xv = nint(x);
      yv = nint(y);
      zv = nint(z); /* voxel coordinate */
      if (on) {
        MRIset_bit(mri, xv, yv, zv); /* mark it filled */
      }
      else {
        MRIclear_bit(mri, xv, yv, zv); /* mark it empty */
      }
    }
    /* compute last point on line */
    t1 = 1.0f;
    x = xa + t1 * dx;
    y = ya + t1 * dy;
    z = za + t1 * dz;
    MRISsurfaceRASToVoxelCached(mris, mri, x, y, z, &x, &y, &z);  // volume coordinate
    xv = nint(x);
    yv = nint(y);
    zv = nint(z); /* voxel coordinate */
    if (on) {
      MRIset_bit(mri, xv, yv, zv); /* mark it filled */
    }
    else {
      MRIclear_bit(mri, xv, yv, zv); /* mark it empty */
    }
  }

  return (NO_ERROR);
}

#define MAX_CSF 55.0f
#define STEP_SIZE 0.1
int MRIScomputeWhiteSurfaceValues(MRI_SURFACE *mris, MRI *mri_brain, MRI *mri_smooth)
{
  double val, x, y, z, min_val, xw, yw, zw, mag, max_mag, xw1, yw1, zw1, previous_val, next_val;
  int total_vertices, vno, nmissing = 0;
  float mean_white, dist, nx, ny, nz;
  VERTEX *v;

  /* first compute intensity of local gray/white boundary */
  mean_white = 0.0f;

  MRISclearMarks(mris); /* for soap bubble smoothing later */
  for (total_vertices = vno = 0; vno < mris->nvertices; vno++) {
    v = &mris->vertices[vno];
    if (v->ripflag) {
      continue;
    }
    if (vno == Gdiag_no) {
      DiagBreak();
    }

    /*
    search outwards and inwards and find the local gradient maximum
    at a location with a reasonable MR intensity value. This will
    be the location of the edge.
    */

    /* search in the normal direction to find the min value */
    min_val = -10.0f;
    mag = 5.0f;
    max_mag = 0.0f;
    nx = v->nx;
    ny = v->ny;
    nz = v->nz;
    x = v->x;
    y = v->y;
    z = v->z;
    MRISsurfaceRASToVoxelCached(mris, mri_brain, x, y, z, &xw, &yw, &zw);
    x = v->x + nx;
    y = v->y + ny;
    z = v->z + nz;
    MRISsurfaceRASToVoxelCached(mris, mri_brain, x, y, z, &xw1, &yw1, &zw1);
    nx = xw1 - xw;
    ny = yw1 - yw;
    nz = zw1 - zw;
    for (dist = -3.0f; dist < 10.0f; dist += STEP_SIZE) {
      x = v->x + v->nx * (dist - 1);
      y = v->y + v->ny * (dist - 1);
      z = v->z + v->nz * (dist - 1);
      MRISsurfaceRASToVoxelCached(mris, mri_brain, x, y, z, &xw, &yw, &zw);
      MRIsampleVolume(mri_brain, xw, yw, zw, &previous_val);
      if (previous_val < 120 && previous_val > 95) /* in right range */
      {
        x = v->x + v->nx * dist;
        y = v->y + v->ny * dist;
        z = v->z + v->nz * dist;
        MRISsurfaceRASToVoxelCached(mris, mri_brain, x, y, z, &xw, &yw, &zw);

        /* see if we are at a local maximum in the gradient magnitude */
        MRIsampleVolumeDerivative(mri_smooth, xw, yw, zw, nx, ny, nz, &mag);
        MRIsampleVolume(mri_brain, xw, yw, zw, &val);

        /* if gradient is big and pointing towards wm */
        if ((previous_val > val) && (fabs(mag) > max_mag)) {
          x = v->x + v->nx * (dist + 1);
          y = v->y + v->ny * (dist + 1);
          z = v->z + v->nz * (dist + 1);
          // MRIworldToVoxel(mri_brain, x, y, z, &xw, &yw, &zw) ;
          MRISsurfaceRASToVoxelCached(mris, mri_brain, x, y, z, &xw, &yw, &zw);
          MRIsampleVolume(mri_brain, xw, yw, zw, &next_val);
          if (next_val > 60 && next_val < 95) {
            max_mag = fabs(mag);
            min_val = val;
          }
        }
      }
    }

    if (min_val > 0) {
      v->val = min_val;
      v->mean = max_mag;
      mean_white += min_val;
      total_vertices++;
      v->marked = 1;
    }
    else {
      nmissing++;
    }
    if (vno == Gdiag_no) fprintf(stdout, "v %d, target value = %2.1f, mag = %2.1f\n", Gdiag_no, v->val, v->mean);
  }
  mean_white /= (float)total_vertices;
  MRISsoapBubbleVals(mris, 100);
  MRISclearMarks(mris);

  /*  MRISaverageVals(mris, 3) ;*/
  fprintf(stdout, "mean white matter surface=%2.1f, %d missing vertices\n", mean_white, nmissing);
  return (NO_ERROR);
}


#define MAX_SAMPLES 1000
  
// BEVIN mris_make_surfaces 2
//  
static int MRIScomputeBorderValues_old(
    MRI_SURFACE *       mris,
    MRI         * const mri_brain,
    MRI         * const mri_smooth,
    double        const inside_hi,
    double        const border_hi,
    double        const border_low,
    double        const outside_low,
    double        const outside_hi,
    double        const sigma,
    float         const max_thickness,
    FILE        * const log_fp,
    int           const which,
    MRI *         const mri_mask,
    double        const thresh,
    int           const flags,
    MRI *         const mri_aseg);

static int MRIScomputeBorderValues_new(
    MRI_SURFACE *       mris,
    MRI         * const mri_brain,
    MRI         * const mri_smooth,
    double        const inside_hi,
    double        const border_hi,
    double        const border_low,
    double        const outside_low,
    double        const outside_hi,
    double        const sigma,
    float         const max_thickness,
    FILE        * const log_fp,
    int           const which,
    MRI *         const mri_mask,
    double        const thresh,
    int           const flags,
    MRI *         const mri_aseg);
    
int MRIScomputeBorderValues(
    MRI_SURFACE *       mris,
    MRI         * const mri_brain,
    MRI         * const mri_smooth,
    double        const inside_hi,
    double        const border_hi,
    double        const border_low,
    double        const outside_low,
    double        const outside_hi,
    double        const sigma,
    float         const max_thickness,
    FILE        * const log_fp,
    int           const which,
    MRI *         const mri_mask,
    double        const thresh,
    int           const flags,
    MRI *         const mri_aseg)
{
  int result;
  if (1) {
    result = 
      MRIScomputeBorderValues_new(
        mris,mri_brain,mri_smooth,inside_hi,border_hi,border_low,outside_low,outside_hi,
        sigma,max_thickness,log_fp,which,mri_mask,thresh,flags,mri_aseg);
  } else {
    result = 
      MRIScomputeBorderValues_old(
        mris,mri_brain,mri_smooth,inside_hi,border_hi,border_low,outside_low,outside_hi,
        sigma,max_thickness,log_fp,which,mri_mask,thresh,flags,mri_aseg);
  }
  return result;
}

/*!
  \fn int MRIScomputeBorderValues_new()
  \brief Computes the distance along the normal to the point of the maximum
  gradient of the intensity as well as the intensity at this point. The
  intensity will be used as a target intensity when placing the surface.
  There are lots of rules employed to make sure that the point is not
  in a crazy place (mostly that the value must be between border_low
  and border_hi). 
  \param mris surface (could be white or pial)
    v->{x,y,z} is the current vertex coordinate
    v->{nx,ny,nz} is the normal to the current vertex
    v->orig{x,y,z} is a reference (see max_thickness)
  \param mri_brain - T1 weighted input volume (mri_T1)
  \param mri_smooth - not apparently used for anything (mri_smooth)
  \param sigma sets the range of smoothing to [sigma 10*sigma]
  \param max_thickness - (eg, 10mm) not really a thickness but a
    threshold in mm that limits how far away a target point can be
    respect to orig{xyz}
  \param which = GRAY_WHITE or GRAY_CSF, gray/white surface or pial 
  \param thresh eg, 0. Mask value must be > thresh to be in the mask
  \param flags IPFLAG_FIND_FIRST_WM_PEAK (with hires, generally not set)
  \param mri_aseg - use to check whether a voxel is in the contralat hemi
  Note: STEP_SIZE (all caps) is #defined. It controls the step size when searching
   through the normal after having found the distance range.
  Hidden Parameter: 1mm 
  The step size of the in/out search is determined by mri_brain->xsize/2
  It does not appear that the annot is used in this function or its children

  When placing the white surface (second variables are from mris_make_surfaces):
  inside_hi eg,  120 (MAX_WHITE)
  border_hi eg,  115 (max_border_white, MeanWM+1WMSTD)
  border_low eg,  77 (min_border_white, MeanGM)
  outside_low eg, 68 (min_gray_at_white_border, MeanGM-1GMSTD)
  outside_hi eg, 115 (outside_hi (often same as border_hi), used?)

  When placing the pial surface  (second variables are from mris_make_surfaces):
  inside_hi = max_gray (eg, 99.05)
  border_hi = max_gray_at_csf_border = meanGM-1stdGM (eg, 65.89)
  border_low = min_gray_at_csf_border = meanGM-V*stdGM (V=3) (eg, 45.7)
  outside_low = min_csf = meanGM - MAX(0.5,(V-1)*stdGM) (eg, 10)
  outside_hi  = (max_csf+max_gray_at_csf_border)/2 (eg, 60.8)

  The outputs are set in each vertex structure:
      v->val2 = current_sigma; // smoothing level along gradient used to find the target
      v->val  = max_mag_val; // target intensity
      v->d = max_mag_dist;   // dist to target intensity along normal
      v->mean = max_mag;     // derive at target intensity
      v->marked = 1;         // vertex has good data
      Skips all ripped vertices
#BV
*/
static int MRIScomputeBorderValues_new(
    MRI_SURFACE *       mris,
    MRI         * const mri_brain,
    MRI         * const mri_smooth,
    double        const inside_hi,
    double        const border_hi,
    double        const border_low,
    double        const outside_low,
    double        const outside_hi,
    double        const sigma,
    float         const max_thickness,
    FILE        * const log_fp,
    int           const which,
    MRI *         const mri_mask,
    double        const thresh,
    int           const flags,
    MRI *         const mri_aseg) 
{
  float const step_size = mri_brain->xsize/2;

  printf("Entering MRIScomputeBorderValues_new(): \n");
  printf("  inside_hi   = %g\n",inside_hi);
  printf("  border_hi   = %g\n",border_hi);
  printf("  border_low  = %g\n",border_low);
  printf("  outside_low = %g\n",outside_low);
  printf("  outside_hi  = %g\n",outside_hi);
  printf("  sigma = %g\n",sigma);
  printf("  max_thickness = %g\n",max_thickness);
  printf("  step_size=%g\n",step_size);
  printf("  STEP_SIZE=%g\n",STEP_SIZE);
  printf("  which = %d\n",which);
  printf("  thresh = %g\n",thresh);
  printf("  flags = %d\n",flags);
  printf("  BorderValsHiRes=%d\n",BorderValsHiRes);

  MRI *mri_tmp;
  if (mri_brain->type == MRI_UCHAR) {
    mri_tmp = MRIreplaceValues(mri_brain, NULL, 255, 0);
  } else {
    mri_tmp = MRIcopy(mri_brain, NULL);
  }

  MRISclearMarks(mris); /* for soap bubble smoothing later */

  // Various double sums which are not used to compute future results
  // so the instability is not important
  //
  double mean_dist = 0, mean_in = 0, mean_out = 0, mean_border = 0;

  // Various counters that are used after the parallel loop
  //
  int total_vertices = 0;
  int ngrad_max = 0, ngrad = 0, nmin = 0;
  int nmissing  = 0, nout  = 0, nin  = 0, nfound = 0, nalways_missing = 0, num_changed = 0;
  
  // Prepare to map all the surface points to voxels
  //
  MRIS_SurfRAS2VoxelMap* sras2v_map = 
    MRIS_makeRAS2VoxelMap(mri_brain, mris);
  
  int vno;

  // Loop over all the vertices
  ROMP_PF_begin
#ifdef HAVE_OPENMP
  #pragma omp parallel for if_ROMP(assume_reproducible) \
    reduction(+:mean_dist,mean_in,mean_out,mean_border) \
    reduction(+:total_vertices,ngrad_max,ngrad,nmin,nmissing,nout,nin,nfound,nalways_missing,num_changed)
#endif
  for (vno = 0; vno < mris->nvertices; vno++) {
    ROMP_PFLB_begin
    
    VERTEX_TOPOLOGY const * const vt = &mris->vertices_topology[vno];
    VERTEX                * const v  = &mris->vertices         [vno];
    double next_val = 0;    
    
    if (v->ripflag) {
      ROMP_PF_continue;
    }

    if (vno == Gdiag_no)
      DiagBreak();

    // Note: xyz are in mm, xw,yw,zw are in voxels

    // Calculate the unit-length normal to the vertex in VOXEL space
    float nx,ny,nz;
    {
      double x,y,z;
      double xw, yw, zw;
      double xw1, yw1, zw1;
      x = v->x;
      y = v->y;
      z = v->z;
      MRIS_useRAS2VoxelMap(sras2v_map, mri_brain,x, y, z, &xw, &yw, &zw);
      x = v->x + v->nx;
      y = v->y + v->ny;
      z = v->z + v->nz;
      MRIS_useRAS2VoxelMap(sras2v_map, mri_brain,x, y, z, &xw1, &yw1, &zw1);
    
      // Note: these nx,ny,nz are in VOXEL space whereas v->{nx,ny,nz} are in TKR mm space
      nx = xw1 - xw;
      ny = yw1 - yw;
      nz = zw1 - zw;
      float dist = sqrt(SQR(nx) + SQR(ny) + SQR(nz));
      if (FZERO(dist)) ROMP_PF_continue;  // WAS "dist = 1;" BUT THAT MAKES NO SENSE
      nx /= dist;
      ny /= dist;
      nz /= dist;
    }
    
    /*
      find the distance in the directions parallel and anti-parallel to
      the surface normal in which the gradient is pointing 'inwards'.
      The border will then be constrained to be within that region.
    */
    double inward_dist  =  1.0; // does nothing, reset below
    double outward_dist = -1.0; // does nothing, reset below
    // sigma is the amount of smoothing when computing the intensity derivative
    double current_sigma; 
    for (current_sigma = sigma; current_sigma <= 10 * sigma; current_sigma *= 2) {
    
      // search inwards, starting at 0 and going to -max "thickness"
      double mag = -1.0;
      float dist;
      for (dist = 0; dist > -max_thickness; dist -= step_size) {

        // dx dy dz is the direction and distance vertex has moved
        // v->nx etc. is the unit-length vertex normal
        // so this is the maximum possible distance this can be from origx...
        double dx = v->x - v->origx;
        double dy = v->y - v->origy;
        double dz = v->z - v->origz;
	// Note clear what orig_dist is computing
        double orig_dist = fabs(dx * v->nx + dy * v->ny + dz * v->nz);
        double val;

        if (fabs(dist) + orig_dist > max_thickness) {
          // too far from the orig
          break;
        }

        double xw, yw, zw;
        double const x = v->x + v->nx * dist;
        double const y = v->y + v->ny * dist;
        double const z = v->z + v->nz * dist;
        MRIS_useRAS2VoxelMap(sras2v_map, mri_brain,x, y, z, &xw, &yw, &zw);

	// Compute derivative of the intensity along the normal. The
	// normal (nx,ny,nz) always points outward. mri_tmp is mri_brain.
	// It may be a copy if mri_brain is UCHAR. nx,ny,nz are in voxel space
        MRIsampleVolumeDerivativeScale(mri_tmp, xw, yw, zw, nx, ny, nz, &mag, current_sigma);   // expensive
        if (mag >= 0.0) {
          // In a T1, this should decrease, so break if it increases
          break;
        }
        
        MRIsampleVolume(mri_brain, xw, yw, zw, &val);
        if (val > border_hi) {
          // More intense than the expected range of WM. border_hi is
          // 1std above the mean of WM.
          break;
        }
        if (mri_mask) {
          MRIsampleVolume(mri_mask, xw, yw, zw, &val);
          if (val > thresh) {
            //Out side of mask, so break
            break;
          }
        }

      } // end loop over inward search

      // This will be +step_size/2 if it did not make it even one step. Otherwise it will
      // be some negative value
      inward_dist = dist + step_size / 2;

      // This if() used to have a "DIAG_VERBOSE_ON &&". This made the
      // behavior non-deterministic for hires volumes because
      // "next_val" was used downstream but not set here. This existed
      // in v6. Also, next_val needs to be defined globally withing
      // the scope of the vertex loop. There are several places below
      // (now commented out) where it is redefined.
      if(BorderValsHiRes==1  && mag >= 0.0){
	// This code is supposed to refine inward_dist for hires
	// volumes. This is similar to the code above except using a
	// step that is half the size. But it just looks at the value
	// and not the gradient, so it is not clear how this is
	// supposed to work. And why only the inward loop?
        for (dist = inward_dist; dist > -max_thickness; dist -= step_size / 2) {
          double x,y,z;
          double xw, yw, zw;
          double val;
          //double next_val; // define above with looop scope

	  // Sample brain at this distance
          x = v->x + v->nx * dist;
          y = v->y + v->ny * dist;
          z = v->z + v->nz * dist;
          MRIS_useRAS2VoxelMap(sras2v_map, mri_brain,x, y, z, &xw, &yw, &zw);
          MRIsampleVolume(mri_brain, xw, yw, zw, &val);

	  // Sample brain at this distance + stepsize/2
          x = v->x + v->nx * (dist + step_size / 2);
          y = v->y + v->ny * (dist + step_size / 2);
          z = v->z + v->nz * (dist + step_size / 2);
          MRIS_useRAS2VoxelMap(sras2v_map, mri_brain,x, y, z, &xw, &yw, &zw);
          MRIsampleVolume(mri_brain, xw, yw, zw, &next_val);
          
          if (next_val < val)  
	    // There is a decrease in the value, so must be at max inward max
	    // Again, this does not make sense if not sampling the gradient. 
            break; // break from distance loop
        } // end loop over distance
        inward_dist = dist;
      } // end if(BorderValsHiRes)

      // search outwards
      for (dist = 0; dist < max_thickness; dist += step_size) {
        double val;
        double dx = v->x - v->origx;
        double dy = v->y - v->origy;
        double dz = v->z - v->origz;
        double orig_dist = fabs(dx * v->nx + dy * v->ny + dz * v->nz);
        if (fabs(dist) + orig_dist > max_thickness) {
          break;
        }

        double xw, yw, zw;
        double const x = v->x + v->nx * dist;
        double const y = v->y + v->ny * dist;
        double const z = v->z + v->nz * dist;
        MRIS_useRAS2VoxelMap(sras2v_map, mri_brain,x, y, z, &xw, &yw, &zw);
        MRIsampleVolumeDerivativeScale(mri_tmp, xw, yw, zw, nx, ny, nz, &mag, current_sigma);

        if (mag >= 0.0)
          break;

        MRIsampleVolume(mri_brain, xw, yw, zw, &val);
        if (val < border_low)
	  // Less intense than GM. border_low is the global mean (or mode) of GM
          break; 
        if (mri_mask) {
          MRIsampleVolume(mri_mask, xw, yw, zw, &val);
          if (val > thresh) {
            break;
          }
        }
      } // end loop over outward search

      // This will be +step_size/2 if it did not make it even one step. Otherwise it will
      // be some positive value
      outward_dist = dist - step_size / 2;
      
      // Are the bounds found?
      if (!isfinite(outward_dist))
        DiagBreak();

      if (inward_dist <= 0 || outward_dist >= 0) {
	// Either the inward or the outward was able to take at least
	// one step so we have defined a range along the normal.
        break;
      }

    } // current_sigma

    if (inward_dist > 0 && outward_dist < 0) {
      // Neither the inward noor the outward was able to take a single step
      // across all the sigmas
      current_sigma = sigma; // reset sigma to the input value
    }

    FILE *fp = NULL;
    if (vno == Gdiag_no) {
      char fname[STRLEN];
      sprintf(fname, "v%d.%2.0f.log", Gdiag_no, sigma * 100);
      fp = fopen(fname, "w");
      fprintf(stdout,"v %d: inward dist %2.2f, outward dist %2.2f, sigma %2.1f\n",
              vno,inward_dist,outward_dist,current_sigma);
    }

    // At this point, we have a sigma and a distance range (inward and outward)
    v->val2 = current_sigma;

    /* Search along the normal within the distance range determined
      above to find the gradient maximum at a location with a
      reasonable MR intensity value. This will be the target intensity
      value when placing the surface.  */
    double max_mag_val     = -10.0f;
    double max_mag         = 0.0f;
    double min_val         = 10000.0;
    double min_val_dist    = 0.0f;
    int    local_max_found = 0;
    double max_mag_dist    = 0.0f;
    float sample_dists[MAX_SAMPLES], sample_mri[MAX_SAMPLES];
    int   numberOfSamples = 0;
    float dist;    
    for (dist = inward_dist; dist <= outward_dist; dist += STEP_SIZE) {
      // There must be a value in this distance range where 
      // val >= border_low && val < border_hi
      // val >= MeanGM && val < MeanWM+1StdWM

      // Get an intensity at dist outward along the normal
      double val;
      {
        double const x = v->x + v->nx * dist;
        double const y = v->y + v->ny * dist;
        double const z = v->z + v->nz * dist;
        double xw, yw, zw;
        MRIS_useRAS2VoxelMap(sras2v_map, mri_brain,x, y, z, &xw, &yw, &zw);
        MRIsampleVolume(mri_brain, xw, yw, zw, &val);
      }

      // These are only used with hires or IPFLAG_FIND_FIRST_WM_PEAK
      sample_dists[numberOfSamples] = dist;
      sample_mri[numberOfSamples]   = val;
      numberOfSamples++;

      // Get an intensity at dist inward along the normal
      double previous_val;
      {
        double const x = v->x + v->nx * (dist - STEP_SIZE);
        double const y = v->y + v->ny * (dist - STEP_SIZE);
        double const z = v->z + v->nz * (dist - STEP_SIZE);
        double xw,yw,zw;
        MRIS_useRAS2VoxelMap(sras2v_map, mri_brain,x, y, z, &xw, &yw, &zw);
        MRIsampleVolume(mri_brain, xw, yw, zw, &previous_val);
      }

      if (previous_val < inside_hi && previous_val >= border_low) {
	// inside_hi=120, boder_low=MeanGray
	/* the "previous" point intensity was inside the acceptable intensity range */
      
        double xw, yw, zw;
        double x,y,z;
        double val;
        double next_mag;
        double previous_mag;
        double mag;
        
        /* see if we are at a local maximum in the gradient magnitude */

	// Sample the intensity at dist along the normal
        x = v->x + v->nx * dist;
        y = v->y + v->ny * dist;
        z = v->z + v->nz * dist;
        MRIS_useRAS2VoxelMap(sras2v_map, mri_brain,x, y, z, &xw, &yw, &zw);
        MRIsampleVolume(mri_brain, xw, yw, zw, &val);

        if(val < min_val) {
	  // Keep track of the minimum intensity along the normal
          min_val = val; /* used if no gradient max is found */
          min_val_dist = dist;
        }

	// Sample the intensity gradient at dist + STEP_SIZE along the normal
        x = v->x + v->nx * (dist + STEP_SIZE);
        y = v->y + v->ny * (dist + STEP_SIZE);
        z = v->z + v->nz * (dist + STEP_SIZE);
        MRIS_useRAS2VoxelMap(sras2v_map, mri_brain,x, y, z, &xw, &yw, &zw);
        MRIsampleVolumeDerivativeScale(mri_tmp, xw, yw, zw, nx, ny, nz, &next_mag, sigma);

	// Sample the intensity gradient at dist - STEP_SIZE along the normal
        x = v->x + v->nx * (dist - STEP_SIZE);
        y = v->y + v->ny * (dist - STEP_SIZE);
        z = v->z + v->nz * (dist - STEP_SIZE);
        MRIS_useRAS2VoxelMap(sras2v_map, mri_brain,x, y, z, &xw, &yw, &zw);
        MRIsampleVolumeDerivativeScale(mri_tmp, xw, yw, zw, nx, ny, nz, &previous_mag, sigma);

	// Sample the intensity gradient at dist along the normal. Use xw,yw,zw below
        x = v->x + v->nx * dist;
        y = v->y + v->ny * dist;
        z = v->z + v->nz * dist;
        MRIS_useRAS2VoxelMap(sras2v_map, mri_brain,x, y, z, &xw, &yw, &zw);
        MRIsampleVolumeDerivativeScale(mri_tmp, xw, yw, zw, nx, ny, nz, &mag, sigma);
        
        // Only for "hires" volumes or if IP flag is set - if
	// intensities are increasing don't keep going.  This could be
	// done earlier, before the gradient is computed, to save some
	// time.
        if ((which == GRAY_WHITE) &&  
	    (BorderValsHiRes==1 || flags & IPFLAG_FIND_FIRST_WM_PEAK) &&  
	    (val > previous_val ) && (next_val > val) ) { 
	  // This if() did not have "&& (next_val > val)" which was in the "orignial"
	  // ie, v6 and before
          break; // out of distance loop
        }
 
        if((mri_aseg != NULL) && (MRIindexNotInVolume(mri_aseg, xw, yw, zw) == 0)) {
	  // Check whether the aseg label is in the opposite hemisphere
          int const label = MRIgetVoxVal(mri_aseg, nint(xw), nint(yw), nint(zw), 0);
          if (vno == Gdiag_no)
            printf("v %d: label distance %2.2f = %s @ (%d %d %d)\n",
                   vno, dist, cma_label_to_name(label), nint(xw), nint(yw),nint(zw));
          if((mris->hemisphere == LEFT_HEMISPHERE  && IS_RH_CLASS(label)) ||
              (mris->hemisphere == RIGHT_HEMISPHERE && IS_LH_CLASS(label))) {
            if(vno == Gdiag_no){
              printf("v %d: terminating search at distance %2.2f due to presence of contra tissue (%s)\n",
                     vno, dist, cma_label_to_name(label));
	    }
            break; // out of distance loop
          }
        }
        
        if(which == GRAY_CSF) {
          /* This is used for placing the pial surface.  Sample the
            next val we would process.  If it is too low, then we have
            definitely reached the border, and the current gradient
            should be considered a local max. Don't want to do this
            for gray/white, as the gray/white gradient often continues
            seemlessly into the gray/csf.
          */
          double xw,yw,zw;
          
          double const x = v->x + v->nx * (dist + STEP_SIZE);
          double const y = v->y + v->ny * (dist + STEP_SIZE);
          double const z = v->z + v->nz * (dist + STEP_SIZE);
          MRIS_useRAS2VoxelMap(sras2v_map, mri_brain,x, y, z, &xw, &yw, &zw);
          
          //double next_val; // define with loop scope
          MRIsampleVolume(mri_brain, xw, yw, zw, &next_val);
	  // border_hi = max_gray_at_csf_border = meanGM-1stdGM (eg, 65.89)
          if (next_val < border_low)
            next_mag = 0;
        }

        if (vno == Gdiag_no) fprintf(fp, "%2.3f  %2.3f  %2.3f  %2.3f  %2.3f\n", dist, val, mag, previous_mag, next_mag);

        if ((fabs(mag) > fabs(previous_mag)) && (fabs(mag) > fabs(next_mag)) && 
	    (val <= border_hi) && (val >= border_low)) {
	  // Gradient maximum has been found if the grad at this
	  // distance is greater than the grad at dist-STEP and
	  // dist+STEP and the inensity is between BorderHi
	  // (MeanWM+1WMSTD) and BorderLow (MeanGM).  Below determines
	  // whether the gradient is the local maximum.  
	  /* double next_val; // define with loop scope*/
          double xw,yw,zw;
	  // Sample the volume at dist + 1mm (1mm is a hidden parameter)
          double const x = v->x + v->nx * (dist + 1);
          double const y = v->y + v->ny * (dist + 1);
          double const z = v->z + v->nz * (dist + 1);
          MRIS_useRAS2VoxelMap(sras2v_map, mri_brain,x, y, z, &xw, &yw, &zw);
          MRIsampleVolume(mri_brain, xw, yw, zw, &next_val);
          /*If a gradmax has not been found yet (or this one is
            greater than the current max) and the "next_val" is in the
            right range, set the gradmax to that at this point.*/
	  // Not clear what the logic is for the 1mm criteria. It effectively
	  // requires that the max gradient be at least 1mm away from the
	  // edge that marks the allowable intensity range. 
	  /* This looks inefficient in that the above could be skipped
            if a local max had been found already (?).*/
          if ((next_val >= outside_low) &&
              (next_val <= border_hi  ) &&
              (next_val <= outside_hi ) &&
              (!local_max_found || (max_mag < fabs(mag)))) {
	    // outside_low=MeanGM-1GMSTD
	    // border_hi=MeanWM+1WMSTD
	    // outside_hi=MeanWM+1WMSTD
            // beware, this is non-deterministic! if the max mag has equal fabs(), any could be chosen
            local_max_found = 1;
            max_mag_dist = dist; 
            max_mag      = fabs(mag);
            max_mag_val  = val;         
          }
        }
        else {
          /* If no local max found yet, just used largest gradient if
            the intensity is in the right range. This basically keeps
            track of the max grad until a local max has been found.
          */
          if ((local_max_found == 0) && (fabs(mag) > max_mag) && (val <= border_hi) && (val >= border_low)) {
  	    // Sample the volume at dist + 1mm (1mm is a hidden parameter); same code as above
            double xw,yw,zw;
            // double next_val;  // define with loop scope
            double const x = v->x + v->nx * (dist + 1);
            double const y = v->y + v->ny * (dist + 1);
            double const z = v->z + v->nz * (dist + 1);
            MRIS_useRAS2VoxelMap(sras2v_map, mri_brain,x, y, z, &xw, &yw, &zw);
            MRIsampleVolume(mri_brain, xw, yw, zw, &next_val);
            if (next_val >= outside_low && next_val <= border_hi && next_val < outside_hi) {
              max_mag_dist = dist;
              max_mag = fabs(mag);
              max_mag_val = val;
            }
          }
        }
      }

    } // for dist

    if (vno == Gdiag_no) {
      fclose(fp);
      fp = NULL;
    }

    // Doesn't apply to standard stream - only highres or if user
    // specifies IPFLAG_FIND_FIRST_WM_PEAK. Not clear what effect this will have
    if (BorderValsHiRes || flags & IPFLAG_FIND_FIRST_WM_PEAK) {
      // whalf Hidden parameter. Units of STEP_SIZE (I think)
      int const whalf = 7; // This was a #define

      if(vno == Gdiag_no) 
	DiagBreak();

      {
        int n;
        for (n = 0; n < vt->vnum; n++)
          if (vt->v[n] == Gdiag_no) {
            DiagBreak();
          }
      }
      
      // Find max in the samples range, and also compute 1st
      // derivative and put it in dm array Generally expect this
      // derivative to be negative
      float max_mri = 0;
      float dm[MAX_SAMPLES];
      {
        int i;
        for (i = 0; i < numberOfSamples; i++) {
          if (sample_mri[i] > max_mri) max_mri = sample_mri[i];
          if (i < numberOfSamples - 1 && i > 0) {
            dm[i] = sample_mri[i + 1] - sample_mri[i - 1];
          } else {
            dm[i] = 0;
          }
        }
      }
      
      // compute second derivative if IPFLAG_FIND_FIRST_WM_PEAK
      float dm2[MAX_SAMPLES];
      if (flags & IPFLAG_FIND_FIRST_WM_PEAK) {
        int i;
        for (i = 0; i < numberOfSamples; i++) {
          if (i < numberOfSamples - 1 && i > 0)
            dm2[i] = dm[i + 1] - dm[i - 1];
          else
            dm2[i] = 0;
        }
        if (vno == Gdiag_no) {
          char fname[STRLEN];
          sprintf(fname, "v%d.%2.0f.dm.log", Gdiag_no, sigma * 100);
          fp = fopen(fname, "w");
          for (i = 0; i < numberOfSamples; i++) fprintf(fp, "%f %f\n", dm[i], dm2[i]);
          fclose(fp);
          fp = NULL;
          DiagBreak();
        }
      }

      // If max_mri > 1.15*max_mag_val. How often does this happen? 
      if (max_mag_val > 0 && max_mri / 1.15 > max_mag_val) {
	// Hidden parameter 1.15
	// Not sure what's going on here.
        float peak = 0.0f, outside = 1.0f; // Hidden parameter 1.0
      
        int i;
        for (i = 0; i < numberOfSamples; i++) {
          if (i == Gdiag_no2) DiagBreak();
          
          // Find a local maxima, where the slope changes from positive to zero or negative
	  // Skip samples that have a positive 1st deriv 
          if (dm[i] > 0) continue;

          peak    = dm[i];
          outside = 0.0;
          int num = 0;
          
	  // Compute an average 1st deriv within a range of this point
          int const lo = MAX(0, i - whalf);          
          int const hi = MIN(i + whalf + 1, numberOfSamples);
          int i1;
	  // Find the first i1 where dm is less than (ie, more
	  // negative than) peak.  i1 will never get past one
	  // increment until the max gradient is reached. After that,
	  // it should go to its maximum making "outside" be the mean
	  // of the derivative values past the peak derivative
          for (i1 = lo; i1 < hi; i1++) {
            outside += dm[i1];
            num++;
            if (dm[i1] < peak) break;  // not a local maxima in the negative direction
                // If i1 <  i then dm[i1] is positive, and peak is negative, so this test is false
                // If i1 == i then dm[i] == dm[i1], so this test fails
                // It i1 >  i then this is searching for the first following slope that is even steeper down
          }
          outside /= num;
	  // This only happens if it gets past the peak
          if ((peak < 0) && (i1 > i + whalf))  // found a local maximum that is not a flat region of 0
            break;
        }

	// If the peak derivative is greater than 1.5*the mean of the derivatives past the peak
	// Hidden parameter 1.5
        if (i < numberOfSamples - whalf && peak / outside > 1.5)  // it was a local max - set the target to here
        {
          if (vno == Gdiag_no)
            printf("v %d: resetting target to local max at %2.2f: I=%d, peak=%2.2f, outside=%2.2f, ratio=%2.2f\n",
                   vno,
                   sample_dists[i],
                   (int)sample_mri[i],
                   peak,
                   outside,
                   peak / outside);
          max_mag_val  = sample_mri[i];
          max_mag      = fabs(dm[i]);
          max_mag_dist = sample_dists[i];
        }
        else if (flags & IPFLAG_FIND_FIRST_WM_PEAK)  // not a local max in 1st derivative - try second */
        {
          for (i = 0; i < numberOfSamples; i++) {
            if (i == Gdiag_no2) DiagBreak();

            if (dm2[i] >= 0) continue;

            peak = dm2[i];
            int num = 0;
            outside = 0.0;
            
            int const lo = MAX(0, i - whalf);          
            int const hi = MIN(i + whalf + 1, numberOfSamples);
            int i1;
            for (i1 = lo; i1 < hi; i1++) {
              outside += dm2[i1];
              num++;
              if (dm2[i1] < dm2[i]) break;  // not a local maxima in the negative direction
            }
            outside /= num;
            double val          = sample_mri[i];
            double next_val     = sample_mri[i + 1];
            double previous_val = sample_mri[i - 1];
            // make sure it is in feasible range
            if ((previous_val > inside_hi) || (previous_val < border_low) || (next_val > outside_hi) ||
                (next_val < outside_low) || (val > border_hi) || (val < border_low))
              continue;

            if ((peak < 0) && (i1 > i + whalf) && sample_mri[i1])  // found a local maximum that is not a flat region of 0
              break;
          }

          if (i < numberOfSamples - whalf && peak / outside > 1.5)  // it was a local max - set the target to here
          {
            if (vno == Gdiag_no)
              printf(
                  "!!!!!!!!! v %d: resetting target to local max at in second derivative %2.2f: I=%d, peak=%2.2f, "
                  "outside=%2.2f, ratio=%2.2f\n",
                  vno,
                  sample_dists[i],
                  (int)sample_mri[i],
                  peak,
                  outside,
                  peak / outside);

            max_mag = dm[i];
            
            int i1;
            for (i1 = i + 1; i1 < numberOfSamples; i1++)  // search forward for largest (negative) derivative
              if (max_mag > dm[i1])           // previous one was largest negative one
                break;
            
            if (i1 < numberOfSamples) i = i1 - 1;
            
            max_mag_val  = sample_mri[i];
            max_mag      = fabs(dm[i]);
            max_mag_dist = sample_dists[i];
          }
        }
        
        if (vno == Gdiag_no) DiagBreak();
      } // end if (max_mag_val > 0 && max_mri / 1.15 > max_mag_val) 

    } // end if (BorderValsHiRes || flags & IPFLAG_FIND_FIRST_WM_PEAK)

    if (which == GRAY_CSF && local_max_found == 0 && max_mag_dist > 0) {
      /* When placing the pial surface and the local max is not found,
         check to make sure it's not ringing near the gray white
         boundary, by seeing if there is uniform stuff in the 2mm outside that
         could be gray matter.*/
      int allgray = 1;
      float outlen; // hidden param below 2mm
      for (outlen = max_mag_dist; outlen < max_mag_dist + 2; outlen += STEP_SIZE) {
        double const x = v->x + v->nx * outlen;
        double const y = v->y + v->ny * outlen;
        double const z = v->z + v->nz * outlen;
        double xw,yw,zw;
        MRIS_useRAS2VoxelMap(sras2v_map, mri_brain,x, y, z, &xw, &yw, &zw);
        double val;
        MRIsampleVolume(mri_brain, xw, yw, zw, &val);
        if ((val < outside_hi /*border_low*/) || (val > border_hi)) {
	  // if it gets here, then it is not all gray
	  // border_hi < val < outside_hi
	  // outside_hi = (max_csf+max_gray_at_csf_border)/2 (eg, 60.8)
	  // border_hi = max_gray_at_csf_border = meanGM-1stdGM (eg, 65.89)
          allgray = 0;
          break;
        }
      }
      
      if (allgray) {
        if (Gdiag_no == vno)
          printf("v %d: exterior gray matter detected, "
              "ignoring large gradient at %2.3f (I=%2.1f)\n",
              vno,max_mag_dist, max_mag_val);
        max_mag_val = -10; /* don't worry about largest gradient */
        max_mag_dist = 0;
        num_changed++;
      }
    }

    if (max_mag_val > 0) /* found the border value */
    {
      if (local_max_found) {
        ngrad_max++;
      }
      else {
        ngrad++;
      }
      if (max_mag_dist > 0) {
        nout++;
        nfound++;
        mean_out += max_mag_dist;
      }
      else {
        nin++;
        nfound++;
        mean_in += -max_mag_dist;
      }

      if (max_mag_val < border_low) {
        max_mag_val = border_low;
      }

      mean_dist += max_mag_dist;
      mean_border += max_mag_val;
      total_vertices++;

      // Set vertex values
      v->val  = max_mag_val; // target intensity
      v->d = max_mag_dist;   // dist to target intensity
      v->mean = max_mag;     // derive at target intensity
      v->marked = 1;
    }
    else /* couldn't find the border value */
    {
      if (min_val < 1000) {
	// Could find some points within the acceptible intensity range
	// so use the point with the minimum inensity.
        nmin++;
        v->d = min_val_dist;
        if (min_val < border_low) {
          min_val = border_low;
        }
        v->val = min_val;
        v->marked = 1;
        mean_border += min_val;
        total_vertices++;
      }
      else {
	// Could NOT find some points within the acceptible intensity range.
        /* don't overwrite old target intensity if it was there */
        /*        v->val = -1.0f ;*/
        v->d = 0;
        if (v->val < 0) {
          nalways_missing++;
          v->marked = 0;
        }
        else {
          v->marked = 1;
        }
        nmissing++;
      }
    }

    if (vno == Gdiag_no)
      fprintf(stdout,
              "v %d, target value = %2.1f, mag = %2.1f, dist = %2.2f, %s\n",
              Gdiag_no,
              v->val,
              v->mean,
              v->d,
              local_max_found ? "local max" : max_mag_val > 0 ? "grad" : "min");
  
    ROMP_PFLB_end
  } // end loop over vertices
  ROMP_PF_end
  
  mean_dist   /= (float)(total_vertices - nmissing);
  mean_border /= (float)total_vertices;

  if (nin > 0) {
    mean_in   /= (float)nin;
  }
  if (nout > 0) {
    mean_out /= (float)nout;
  }

  /*  MRISaverageVals(mris, 3) ;*/
  
  // This is an extremely hacky way to print to both stdout and log_fp
  FILE* fp = stdout;
  int pass;
  for (pass = 0; fp && (pass < 2); pass++, fp = log_fp) {
    fprintf(fp,
      "NONREPRODUCIBLE NUMBERS NOT USED FOR ANYTHING mean border=%2.1f, %d (%d) missing vertices, mean dist %2.1f "
      "[%2.1f (%%%2.1f)->%2.1f (%%%2.1f))]\n",
      mean_border,
      nmissing,
      nalways_missing,
      mean_dist,
      mean_in,
      100.0f * (float)nin  / (float)nfound,
      mean_out,
      100.0f * (float)nout / (float)nfound);
    fprintf(fp,
      "NONREPRODUCIBLE NUMBERS NOT USED FOR ANYTHING %%%2.0f local maxima, %%%2.0f large gradients "
      "and %%%2.0f min vals, %d gradients ignored\n",
      100.0f * (float)ngrad_max / (float)mris->nvertices,
      100.0f * (float)ngrad     / (float)mris->nvertices,
      100.0f * (float)nmin      / (float)mris->nvertices,
      num_changed);
  }

#if 0
  MRISsoapBubbleVals(mris, 100) ;
#endif

  MRIS_freeRAS2VoxelMap(&sras2v_map);

  return (NO_ERROR);
}


static int MRIScomputeBorderValues_old(
    MRI_SURFACE *       mris,
    MRI         * const mri_brain,
    MRI         * const mri_smooth,
    double        const inside_hi,
    double        const border_hi,
    double        const border_low,
    double        const outside_low,
    double        const outside_hi,
    double        const sigma,
    float         const max_thickness,
    FILE        * const log_fp,
    int           const which,
    MRI *         const mri_mask,
    double        const thresh,
    int           const flags,
    MRI *         const mri_aseg)
{
  float const step_size = mri_brain->xsize / 2;
    
  MRI *mri_tmp;
  if (mri_brain->type == MRI_UCHAR) {
    mri_tmp = MRIreplaceValues(mri_brain, NULL, 255, 0);
  }
  else {
    mri_tmp = MRIcopy(mri_brain, NULL);
  }


  float mean_dist = 0, mean_in = 0, mean_out = 0, mean_border = 0;

  int total_vertices = 0;
  
  double max_mag_dist = 0.0f;

  int ngrad_max = 0, ngrad = 0, nmin = 0;
  int nmissing  = 0, nout  = 0, nin  = 0, nfound = 0, nalways_missing = 0, num_changed = 0;
  
  FILE *fp = NULL;


  /* first compute intensity of local gray/white boundary */

  MRISclearMarks(mris); /* for soap bubble smoothing later */

  int vno;

  ROMP_PF_begin
#ifdef HAVE_OPENMP
  #pragma omp parallel for if_ROMP(serial)
#endif
  for (vno = 0; vno < mris->nvertices; vno++) {
    ROMP_PFLB_begin
    
    VERTEX_TOPOLOGY const * const vt = &mris->vertices_topology[vno];
    VERTEX                * const v  = &mris->vertices         [vno];
    if (v->ripflag) {
      ROMP_PF_continue;
    }
    if (vno == Gdiag_no) {
      DiagBreak();
    }

    float nx,ny,nz;
    {
      double x,y,z;
      
      double xw, yw, zw;
      x = v->x;
      y = v->y;
      z = v->z;
      MRISsurfaceRASToVoxelCached(mris, mri_brain, x, y, z, &xw, &yw, &zw);
      
      double xw1, yw1, zw1;
      x = v->x + v->nx;
      y = v->y + v->ny;
      z = v->z + v->nz;
      MRISsurfaceRASToVoxelCached(mris, mri_brain, x, y, z, &xw1, &yw1, &zw1);
    
      nx = xw1 - xw;
      ny = yw1 - yw;
      nz = zw1 - zw;

      float dist = sqrt(SQR(nx) + SQR(ny) + SQR(nz));
      if (FZERO(dist)) dist = 1;
      nx /= dist;
      ny /= dist;
      nz /= dist;
    }
    
    /*
      find the distance in the directions parallel and anti-parallel to
      the surface normal in which the gradient is pointing 'inwards'.
      The border will then be constrained to be within that region.
    */
    double inward_dist  =  1.0;
    double outward_dist = -1.0;

    double current_sigma;
    for (current_sigma = sigma; current_sigma <= 10 * sigma; current_sigma *= 2) {
    
      double mag = -1.0;
      float dist;
      for (dist = 0; dist > -max_thickness; dist -= step_size) {

        double dx = v->x - v->origx;
        double dy = v->y - v->origy;
        double dz = v->z - v->origz;
        double orig_dist = fabs(dx * v->nx + dy * v->ny + dz * v->nz);

        if (fabs(dist) + orig_dist > max_thickness) {
          break;
        }

        double xw, yw, zw;
        double const x = v->x + v->nx * dist;
        double const y = v->y + v->ny * dist;
        double const z = v->z + v->nz * dist;
        MRISsurfaceRASToVoxelCached(mris, mri_brain, x, y, z, &xw, &yw, &zw);
        
        MRIsampleVolumeDerivativeScale(mri_tmp, xw, yw, zw, nx, ny, nz, &mag, current_sigma);
        if (mag >= 0.0) {
          break;
        }
        
        double val;
        MRIsampleVolume(mri_brain, xw, yw, zw, &val);
        if (val > border_hi) {
          break;
        }
        if (mri_mask) {
          MRIsampleVolume(mri_mask, xw, yw, zw, &val);
          if (val > thresh) {
            break;
          }
        }
      }

      inward_dist = dist + step_size / 2;

      if (DIAG_VERBOSE_ON && mri_brain->xsize < .95 && mag >= 0.0)  // refine inward_dist for hires volumes
      {
        for (dist = inward_dist; dist > -max_thickness; dist -= step_size / 2) {
          double x,y,z;
          
          double xw, yw, zw;

          x = v->x + v->nx * dist;
          y = v->y + v->ny * dist;
          z = v->z + v->nz * dist;
          MRISsurfaceRASToVoxelCached(mris, mri_brain, x, y, z, &xw, &yw, &zw);
          
          double val;
          MRIsampleVolume(mri_brain, xw, yw, zw, &val);

          x = v->x + v->nx * (dist + step_size / 2);
          y = v->y + v->ny * (dist + step_size / 2);
          z = v->z + v->nz * (dist + step_size / 2);
          MRISsurfaceRASToVoxelCached(mris, mri_brain, x, y, z, &xw, &yw, &zw);
          
          double next_val;
          MRIsampleVolume(mri_brain, xw, yw, zw, &next_val);
          
          if (next_val < val)  // found max inwards intensity
          {
            break;
          }
        }
        inward_dist = dist;
      }

      for (dist = 0; dist < max_thickness; dist += step_size) {
        double dx = v->x - v->origx;
        double dy = v->y - v->origy;
        double dz = v->z - v->origz;
        double orig_dist = fabs(dx * v->nx + dy * v->ny + dz * v->nz);
        if (fabs(dist) + orig_dist > max_thickness) {
          break;
        }

        double xw, yw, zw;
        double const x = v->x + v->nx * dist;
        double const y = v->y + v->ny * dist;
        double const z = v->z + v->nz * dist;
        MRISsurfaceRASToVoxelCached(mris, mri_brain, x, y, z, &xw, &yw, &zw);
        MRIsampleVolumeDerivativeScale(mri_tmp, xw, yw, zw, nx, ny, nz, &mag, current_sigma);
        if (mag >= 0.0) {
          break;
        }

        double val;
        MRIsampleVolume(mri_brain, xw, yw, zw, &val);
        if (val < border_low) {
          break;
        }
        if (mri_mask) {
          MRIsampleVolume(mri_mask, xw, yw, zw, &val);
          if (val > thresh) {
            break;
          }
        }
      }

      outward_dist = dist - step_size / 2;
      
      if (!isfinite(outward_dist)) {
        DiagBreak();
      }
      if (inward_dist <= 0 || outward_dist >= 0) {
        break;
      }
    }

    if (inward_dist > 0 && outward_dist < 0) {
      current_sigma = sigma; /* couldn't find anything */
    }

    if (vno == Gdiag_no) {
      char fname[STRLEN];
      sprintf(fname, "v%d.%2.0f.log", Gdiag_no, sigma * 100);
      fp = fopen(fname, "w");
      fprintf(stdout,
              "v %d: inward dist %2.2f, outward dist %2.2f, sigma %2.1f\n",
              vno,
              inward_dist,
              outward_dist,
              current_sigma);
    }

    v->val2 = current_sigma;
    /*
      search outwards and inwards and find the local gradient maximum
      at a location with a reasonable MR intensity value. This will
      be the location of the edge.
    */

    /* search in the normal direction to find the min value */
    double max_mag_val = -10.0f;
    double max_mag = 0.0f;
    double min_val = 10000.0;
    double min_val_dist = 0.0f;
    int    local_max_found = 0;
    
    float dists[MAX_SAMPLES], mri[MAX_SAMPLES];
    int   numberOfSamples = 0;

    float dist;    
    for (dist = inward_dist; dist <= outward_dist; dist += STEP_SIZE) {

      double val;
      {
        double const x = v->x + v->nx * dist;
        double const y = v->y + v->ny * dist;
        double const z = v->z + v->nz * dist;

        double xw, yw, zw;
        MRISsurfaceRASToVoxelCached(mris, mri_brain, x, y, z, &xw, &yw, &zw);
      
        MRIsampleVolume(mri_brain, xw, yw, zw, &val);
      }
      
      dists[numberOfSamples] = dist;
      mri[numberOfSamples]   = val;
      numberOfSamples++;

      double previous_val;

      {
        double const x = v->x + v->nx * (dist - STEP_SIZE);
        double const y = v->y + v->ny * (dist - STEP_SIZE);
        double const z = v->z + v->nz * (dist - STEP_SIZE);
        double xw,yw,zw;
        MRISsurfaceRASToVoxelCached(mris, mri_brain, x, y, z, &xw, &yw, &zw);
        MRIsampleVolume(mri_brain, xw, yw, zw, &previous_val);
      }

      /* the previous point was inside the surface */
      if (previous_val < inside_hi && previous_val >= border_low) {
      
        double xw, yw, zw;
        double x,y,z;
        double val;
        
        /* see if we are at a local maximum in the gradient magnitude */
        x = v->x + v->nx * dist;
        y = v->y + v->ny * dist;
        z = v->z + v->nz * dist;
        MRISsurfaceRASToVoxelCached(mris, mri_brain, x, y, z, &xw, &yw, &zw);
        MRIsampleVolume(mri_brain, xw, yw, zw, &val);

        x = v->x + v->nx * (dist + STEP_SIZE);
        y = v->y + v->ny * (dist + STEP_SIZE);
        z = v->z + v->nz * (dist + STEP_SIZE);
        MRISsurfaceRASToVoxelCached(mris, mri_brain, x, y, z, &xw, &yw, &zw);
        
        double next_mag;
        MRIsampleVolumeDerivativeScale(mri_tmp, xw, yw, zw, nx, ny, nz, &next_mag, sigma);

        x = v->x + v->nx * (dist - STEP_SIZE);
        y = v->y + v->ny * (dist - STEP_SIZE);
        z = v->z + v->nz * (dist - STEP_SIZE);
        MRISsurfaceRASToVoxelCached(mris, mri_brain, x, y, z, &xw, &yw, &zw);
        
        double previous_mag;
        MRIsampleVolumeDerivativeScale(mri_tmp, xw, yw, zw, nx, ny, nz, &previous_mag, sigma);

        if (val < min_val) {
          min_val = val; /* used if no gradient max is found */
          min_val_dist = dist;
        }

        /* if gradient is big and val is in right range */
        x = v->x + v->nx * dist;
        y = v->y + v->ny * dist;
        z = v->z + v->nz * dist;
        MRISsurfaceRASToVoxelCached(mris, mri_brain, x, y, z, &xw, &yw, &zw);

        double mag;
        MRIsampleVolumeDerivativeScale(mri_tmp, xw, yw, zw, nx, ny, nz, &mag, sigma);
        
        // only for hires volumes - if intensities are increasing don't keep going - in gm
        if ((which == GRAY_WHITE)
        &&  (mri_brain->xsize < .95 || flags & IPFLAG_FIND_FIRST_WM_PEAK) 
        &&  (val > previous_val )
        /* && (next_val > val) */   // NOTE - the old code has this uncommented, but fails to init next_val on many of the paths leading to it!
            ) { 
          break;
        }
        
        if ((mri_aseg != NULL) && (MRIindexNotInVolume(mri_aseg, xw, yw, zw) == 0)) {

          int const label = MRIgetVoxVal(mri_aseg, nint(xw), nint(yw), nint(zw), 0);

          if (vno == Gdiag_no)
            printf("v %d: label distance %2.2f = %s @ (%d %d %d)\n",
                   vno,
                   dist,
                   cma_label_to_name(label),
                   nint(xw),
                   nint(yw),
                   nint(zw));
                   
          if ((mris->hemisphere == LEFT_HEMISPHERE  && IS_RH_CLASS(label)) ||
              (mris->hemisphere == RIGHT_HEMISPHERE && IS_LH_CLASS(label))
             ) {
            if (vno == Gdiag_no)
              printf("v %d: terminating search at distance %2.2f due to presence of contra tissue (%s)\n",
                     vno,
                     dist,
                     cma_label_to_name(label));
            break;
          }
        }
        
        if (which == GRAY_CSF) {
          /*
            sample the next val we would process.
            If it is too low, then we
            have definitely reached the border,
            and the current gradient
            should be considered a local max.

            Don't want to do this for gray/white,
            as the gray/white gradient
            often continues seemlessly into the gray/csf.
          */
          double xw,yw,zw;
          
          double const x = v->x + v->nx * (dist + STEP_SIZE);
          double const y = v->y + v->ny * (dist + STEP_SIZE);
          double const z = v->z + v->nz * (dist + STEP_SIZE);
          MRISsurfaceRASToVoxelCached(mris, mri_brain, x, y, z, &xw, &yw, &zw);
          
          double next_val;
          MRIsampleVolume(mri_brain, xw, yw, zw, &next_val);
          if (next_val < border_low) {
            next_mag = 0;
          }
        }

        if (vno == Gdiag_no) fprintf(fp, "%2.3f  %2.3f  %2.3f  %2.3f  %2.3f\n", dist, val, mag, previous_mag, next_mag);

        /*
          if no local max has been found, or this one
          has a greater magnitude,
          and it is in the right intensity range....
        */
        if (
            /* (!local_max_found || (fabs(mag) > max_mag)) && */
            (fabs(mag) > fabs(previous_mag)) && (fabs(mag) > fabs(next_mag)) && (val <= border_hi) &&
            (val >= border_low)) {
            
          double xw,yw,zw;
          double const x = v->x + v->nx * (dist + 1);
          double const y = v->y + v->ny * (dist + 1);
          double const z = v->z + v->nz * (dist + 1);
          MRISsurfaceRASToVoxelCached(mris, mri_brain, x, y, z, &xw, &yw, &zw);
          
          double next_val;
          MRIsampleVolume(mri_brain, xw, yw, zw, &next_val);
          /*
            if next val is in the right range, and the intensity at
            this local max is less than the one at the previous local
            max, assume it is the correct one.
          */
          if ((next_val >= outside_low) &&
              (next_val <= border_hi  ) &&
              (next_val <= outside_hi ) &&
#if 0
              (!local_max_found || (val < max_mag_val)))
#else
              (!local_max_found || (max_mag < fabs(mag))))
#endif
          {                             // beware, this is non-deterministic! if the max mag has equal fabs(), any could be chosen
            local_max_found = 1;
            max_mag_dist = dist;
            max_mag      = fabs(mag);
            max_mag_val  = val;         
          }
        }
        else {
          /*
            if no local max found yet, just used largest gradient
            if the intensity is in the right range.
          */
          if ((local_max_found == 0) && (fabs(mag) > max_mag) && (val <= border_hi) && (val >= border_low)) {
            double const x = v->x + v->nx * (dist + 1);
            double const y = v->y + v->ny * (dist + 1);
            double const z = v->z + v->nz * (dist + 1);
            double xw,yw,zw;
            MRISsurfaceRASToVoxelCached(mris, mri_brain, x, y, z, &xw, &yw, &zw);
            
            double next_val;
            MRIsampleVolume(mri_brain, xw, yw, zw, &next_val);
            
            if (next_val >= outside_low && next_val <= border_hi && next_val < outside_hi) {
              max_mag_dist = dist;
              max_mag = fabs(mag);
              max_mag_val = val;
            }
          }
        }
      }
    }

    if (vno == Gdiag_no) fclose(fp);

    // doesn't apply to standard stream - only highres or if user specifies
    if (mri_brain->xsize < .95 || flags & IPFLAG_FIND_FIRST_WM_PEAK) {
      int const whalf = 7;

      if (vno == Gdiag_no) 
	DiagBreak();

      {
        int n;
        for (n = 0; n < vt->vnum; n++)
          if (vt->v[n] == Gdiag_no) {
            DiagBreak();
          }
      }
      
      // find max in range, and also compute derivative and put it in dm array
      float max_mri = 0;
      float dm[MAX_SAMPLES];
      {
        int i;
        for (i = 0; i < numberOfSamples; i++) {
          if (mri[i] > max_mri) max_mri = mri[i];
          if (i < numberOfSamples - 1 && i > 0) {
            dm[i] = mri[i + 1] - mri[i - 1];
          } else {
            dm[i] = 0;
          }
        }
      }
      
      // compute second derivative
      float dm2[MAX_SAMPLES];
      if (flags & IPFLAG_FIND_FIRST_WM_PEAK) {
        int i;
        for (i = 0; i < numberOfSamples; i++) {
          if (i < numberOfSamples - 1 && i > 0)
            dm2[i] = dm[i + 1] - dm[i - 1];
          else
            dm2[i] = 0;
        }
        if (vno == Gdiag_no) {
          char fname[STRLEN];
          sprintf(fname, "v%d.%2.0f.dm.log", Gdiag_no, sigma * 100);
          fp = fopen(fname, "w");
          for (i = 0; i < numberOfSamples; i++) fprintf(fp, "%f %f\n", dm[i], dm2[i]);
          fclose(fp);
          DiagBreak();
        }
      }

      if (max_mag_val > 0 && max_mri / 1.15 > max_mag_val) {

        float peak = 0.0f, outside = 1.0f;
      
        int i;
        for (i = 0; i < numberOfSamples; i++) {
          if (i == Gdiag_no2) DiagBreak();
          
          // Find a local maxima, where the slope changes from positive to zero or negative
          if (dm[i] > 0) continue;

          peak    = dm[i];
          outside = 0.0;
          int num = 0;
          
          int const lo = MAX(0, i - whalf);          
          int const hi = MIN(i + whalf + 1, numberOfSamples);
          int i1;
          for (i1 = lo; i1 < hi; i1++) {
            outside += dm[i1];
            num++;
            if (dm[i1] < peak) break;  // not a local maxima in the negative direction
                // If i1 <  i then dm[i1] is positive, and peak is negative, so this test is false
                // If i1 == i then dm[i] == dm[i1], so this test fails
                // It i1 >  i then this is searching for the first following slope that is even steeper down
          }
          
          outside /= num;

          if ((peak < 0) && (i1 > i + whalf))  // found a local maximum that is not a flat region of 0
            break;
        }

        if (i < numberOfSamples - whalf && peak / outside > 1.5)  // it was a local max - set the target to here
        {
          if (vno == Gdiag_no)
            printf("v %d: resetting target to local max at %2.2f: I=%d, peak=%2.2f, outside=%2.2f, ratio=%2.2f\n",
                   vno,
                   dists[i],
                   (int)mri[i],
                   peak,
                   outside,
                   peak / outside);
          max_mag_val  = mri[i];            // beware - this is finding the highest vno that gets here
          max_mag      = fabs(dm[i]);       // but getting here depends on the previously found max_mag_val !
          max_mag_dist = dists[i];          // so this code can not be parallelized as is...
        }
        
        else if (flags & IPFLAG_FIND_FIRST_WM_PEAK)  // not a local max in 1st derivative - try second */
        {
          for (i = 0; i < numberOfSamples; i++) {
            if (i == Gdiag_no2) DiagBreak();

            if (dm2[i] >= 0) continue;

            peak = dm2[i];
            int num = 0;
            outside = 0.0;
            
            int const lo = MAX(0, i - whalf);          
            int const hi = MIN(i + whalf + 1, numberOfSamples);
            int i1;
            for (i1 = lo; i1 < hi; i1++) {
              outside += dm2[i1];
              num++;
              if (dm2[i1] < dm2[i]) break;  // not a local maxima in the negative direction
            }
            outside /= num;
            double val          = mri[i];
            double next_val     = mri[i + 1];
            double previous_val = mri[i - 1];
            // make sure it is in feasible range
            if ((previous_val > inside_hi) || (previous_val < border_low) || (next_val > outside_hi) ||
                (next_val < outside_low) || (val > border_hi) || (val < border_low))
              continue;

            if ((peak < 0) && (i1 > i + whalf) && mri[i1])  // found a local maximum that is not a flat region of 0
              break;
          }

          if (i < numberOfSamples - whalf && peak / outside > 1.5)  // it was a local max - set the target to here
          {
            if (vno == Gdiag_no)
              printf(
                  "!!!!!!!!! v %d: resetting target to local max at in second derivative %2.2f: I=%d, peak=%2.2f, "
                  "outside=%2.2f, ratio=%2.2f\n",
                  vno,
                  dists[i],
                  (int)mri[i],
                  peak,
                  outside,
                  peak / outside);

            max_mag = dm[i];
            
            int i1;
            for (i1 = i + 1; i1 < numberOfSamples; i1++)  // search forward for largest (negative) derivative
              if (max_mag > dm[i1])           // previous one was largest negative one
                break;
            
            if (i1 < numberOfSamples) i = i1 - 1;
            
            max_mag_val  = mri[i];
            max_mag      = fabs(dm[i]);
            max_mag_dist = dists[i];
          }
        }
        
        if (vno == Gdiag_no) DiagBreak();
      }
    }

    if (which == GRAY_CSF && local_max_found == 0 && max_mag_dist > 0) {
      
      /* check to make sure it's not ringing near the gray white boundary,
         by seeing if there is uniform stuff outside that could be gray matter.
      */
      int allgray = 1;

      float outlen;
      for (outlen = max_mag_dist; outlen < max_mag_dist + 2; outlen += STEP_SIZE) {
        double const x = v->x + v->nx * outlen;
        double const y = v->y + v->ny * outlen;
        double const z = v->z + v->nz * outlen;
        double xw,yw,zw;
        MRISsurfaceRASToVoxelCached(mris, mri_brain, x, y, z, &xw, &yw, &zw);
        double val;
        MRIsampleVolume(mri_brain, xw, yw, zw, &val);
        if ((val < outside_hi /*border_low*/) || (val > border_hi)) {
          allgray = 0;
          break;
        }
      }
      
      if (allgray) {
        if (Gdiag_no == vno)
          printf(
              "v %d: exterior gray matter detected, "
              "ignoring large gradient at %2.3f (I=%2.1f)\n",
              vno,
              max_mag_dist,
              max_mag_val);

        max_mag_val = -10; /* don't worry about largest gradient */
        max_mag_dist = 0;
        num_changed++;
      }
    }

    if (max_mag_val > 0) /* found the border value */
    {
      if (local_max_found) {
        ngrad_max++;
      }
      else {
        ngrad++;
      }
      if (max_mag_dist > 0) {
        nout++;
        nfound++;
        mean_out += max_mag_dist;
      }
      else {
        nin++;
        nfound++;
        mean_in -= max_mag_dist;
      }

      if (max_mag_val < border_low) {
        max_mag_val = border_low;
      }

      mean_dist += max_mag_dist;

      v->val  = max_mag_val;
      v->mean = max_mag;
      
      mean_border += max_mag_val;
      total_vertices++;
      
      v->d = max_mag_dist;
      v->marked = 1;
    }
    else /* couldn't find the border value */
    {
      if (min_val < 1000) {
        nmin++;
        v->d = min_val_dist;
#if 0
        if (min_val > border_hi)  /* found a low value, but not low enough */
        {
          min_val = border_hi ;
        }
        else if (min_val < border_low)
        {
          min_val = border_low ;
        }
#else
        if (min_val < border_low) {
          min_val = border_low;
        }
#endif
        v->val = min_val;
        mean_border += min_val;
        total_vertices++;
        v->marked = 1;
      }
      else {
        /* don't overwrite old target intensity if it was there */
        /*        v->val = -1.0f ;*/
        v->d = 0;
        if (v->val < 0) {
          nalways_missing++;
#if 0
          v->val = (border_low+border_hi)/2 ;
#endif
          v->marked = 0;
        }
        else {
          v->marked = 1;
        }
        nmissing++;
      }
    }
    if (vno == Gdiag_no)
      fprintf(stdout,
              "v %d, target value = %2.1f, mag = %2.1f, dist = %2.2f, %s\n",
              Gdiag_no,
              v->val,
              v->mean,
              v->d,
              local_max_found ? "local max" : max_mag_val > 0 ? "grad" : "min");
#if 0
    if (vno == 44289 || vno == 91080 || vno == 92286 || vno == 46922)
      fprintf(stdout, "v %d, target value = %2.1f, mag = %2.1f, dist=%2.2f\n",
              Gdiag_no, v->val, v->mean, v->d) ;
#endif
  
    ROMP_PFLB_end
  }
  ROMP_PF_end
  
  mean_dist   /= (float)(total_vertices - nmissing);
  mean_border /= (float)total_vertices;

  if (nin > 0) {
    mean_in /= (float)nin;
  }
  if (nout > 0) {
    mean_out /= (float)nout;
  }

#if 0
  MRISsoapBubbleVals(mris, 100) ;
#endif

  /*  MRISaverageVals(mris, 3) ;*/
  fprintf(stdout,
          "NONREPRODUCIBLE NUMBERS NOT USED FOR ANYTHING mean border=%2.1f, %d (%d) missing vertices, mean dist %2.1f "
          "[%2.1f (%%%2.1f)->%2.1f (%%%2.1f))]\n",
          mean_border,
          nmissing,
          nalways_missing,
          mean_dist,
          mean_in,
          100.0f * (float)nin  / (float)nfound,
          mean_out,
          100.0f * (float)nout / (float)nfound);

  fprintf(stdout,
          "NONREPRODUCIBLE NUMBERS NOT USED FOR ANYTHING %%%2.0f local maxima, %%%2.0f large gradients "
          "and %%%2.0f min vals, %d gradients ignored\n",
          100.0f * (float)ngrad_max / (float)mris->nvertices,
          100.0f * (float)ngrad     / (float)mris->nvertices,
          100.0f * (float)nmin      / (float)mris->nvertices,
          num_changed);

  if (log_fp) {

    fprintf(log_fp,
            "NONREPRODUCIBLE NUMBERS NOT USED FOR ANYTHING mean border=%2.1f, %d (%d) missing vertices, mean dist %2.1f "
            "[%2.1f (%%%2.1f)->%2.1f (%%%2.1f))]\n",
            mean_border,
            nmissing,
            nalways_missing,
            mean_dist,
            mean_in,
            100.0f * (float)nin  / (float)nfound,
            mean_out,
            100.0f * (float)nout / (float)nfound);

    fprintf(log_fp,
            "NONREPRODUCIBLE NUMBERS NOT USED FOR ANYTHING %%%2.0f local maxima, %%%2.0f large gradients "
            "and %%%2.0f min vals, %d gradients ignored\n",
            100.0f * (float)ngrad_max / (float)mris->nvertices,
            100.0f * (float)ngrad     / (float)mris->nvertices,
            100.0f * (float)nmin      / (float)mris->nvertices,
            num_changed);
  }

  return (NO_ERROR);
}

#if 1
int MRIScomputeInvertedGrayWhiteBorderValues(MRI_SURFACE *mris,
                                             MRI *mri_brain,
                                             MRI *mri_smooth,
                                             double inside_hi,
                                             double border_hi,
                                             double border_low,
                                             double outside_low,
                                             double outside_hi,
                                             double sigma,
                                             float max_thickness,
                                             FILE *log_fp)
{
  double val, x, y, z, xw, yw, zw, dist, prev_val, next_val;
  int total_vertices, vno, inward_increasing, ngray, nwhite;
  VERTEX *v;
  double mean_white, mean_gray, std_white, std_gray, nsigma, gw_thresh;

  std_white = std_gray = mean_white = mean_gray = 0.0;
  for (ngray = nwhite = total_vertices = vno = 0; vno < mris->nvertices; vno++) {
    v = &mris->vertices[vno];
    if (v->ripflag) {
      continue;
    }
    total_vertices++;
    if (vno == Gdiag_no) {
      DiagBreak();
    }

    /* search for local maximum */
    for (dist = 0; dist < 10; dist += SAMPLE_DIST) {
      x = v->x - dist * v->nx;
      y = v->y - dist * v->ny;
      z = v->z - dist * v->nz;
      MRISsurfaceRASToVoxelCached(mris, mri_brain, x, y, z, &xw, &yw, &zw);
      MRIsampleVolume(mri_brain, xw, yw, zw, &val);
      if (MRIindexNotInVolume(mri_brain, xw, yw, zw) == 0) {
        x = v->x - (dist + SAMPLE_DIST) * v->nx;
        y = v->y - (dist + SAMPLE_DIST) * v->ny;
        z = v->z - (dist + SAMPLE_DIST) * v->nz;
        MRISsurfaceRASToVoxelCached(mris, mri_brain, x, y, z, &xw, &yw, &zw);
        MRIsampleVolume(mri_brain, xw, yw, zw, &next_val);

        x = v->x - (dist - SAMPLE_DIST) * v->nx;
        y = v->y - (dist - SAMPLE_DIST) * v->ny;
        z = v->z - (dist - SAMPLE_DIST) * v->nz;
        MRISsurfaceRASToVoxelCached(mris, mri_brain, x, y, z, &xw, &yw, &zw);
        MRIsampleVolume(mri_brain, xw, yw, zw, &prev_val);
        if (val > next_val && val > prev_val) {
          if (vno == Gdiag_no) {
            printf("white val found at %2.1f\n", val);
          }
          nwhite++;
          mean_white += val;
          std_white += (val * val);
          break;
        }
      }
    }
    x = v->x + 1.0 * v->nx;
    y = v->y + 1.0 * v->ny;
    z = v->z + 1.0 * v->nz;
    MRISsurfaceRASToVoxelCached(mris, mri_brain, x, y, z, &xw, &yw, &zw);
    MRIsampleVolume(mri_brain, xw, yw, zw, &val);
    if (MRIindexNotInVolume(mri_brain, xw, yw, zw) == 0) {
      mean_gray += val;
      std_gray += (val * val);
      ngray++;
    }
  }

  mean_white /= (float)nwhite;
  std_white = sqrt(std_white / (float)nwhite - mean_white * mean_white);
  mean_gray /= (float)ngray;
  std_gray = sqrt(std_gray / (float)ngray - mean_gray * mean_gray);
  nsigma = (mean_gray - mean_white) / (std_gray + std_white);
  gw_thresh = mean_white + nsigma * std_white;
  printf("white %2.1f +- %2.1f,    gray %2.1f +- %2.1f, G/W boundary at %2.1f\n",
         mean_white,
         std_white,
         mean_gray,
         std_gray,
         gw_thresh);

  inward_increasing = mean_gray < mean_white;
  for (total_vertices = vno = 0; vno < mris->nvertices; vno++) {
    v = &mris->vertices[vno];
    if (v->ripflag) {
      continue;
    }
    if (vno == Gdiag_no) {
      DiagBreak();
    }
    v->val = gw_thresh;
  }

  return (NO_ERROR);
}
int MRIScomputeMaxGradBorderValues(MRI_SURFACE *mris,
                                   MRI *mri_brain,
                                   MRI *mri_smooth,
                                   double sigma,
                                   float max_thickness,
                                   float dir,
                                   FILE *log_fp,
                                   MRI *mri_wm,
                                   int callno)
{
  int total_vertices, vno, n, num, found;
  double x, y, z, xv, yv, zv, dist, grad, max_grad, max_grad_dist, sigma_vox, nx, ny, nz, sample_dist, mag,
      max_grad_val, min_val, val, wm_mean, wm_std, wm_hi, wm_lo;
  MRI *mri_median;
  MRI_REGION box;

  box.x = nint(5 / mri_brain->xsize);
  box.y = nint(5 / mri_brain->ysize);
  box.z = nint(5 / mri_brain->zsize);
  box.dx = mri_brain->width - 2 * nint(5 / mri_brain->xsize);
  box.dy = mri_brain->height - 2 * nint(5 / mri_brain->ysize);
  box.dz = mri_brain->depth - 2 * nint(5 / mri_brain->zsize);

  if (0) {
    // mri_median = MRImedian(mri_brain, NULL, 3, &box) ;
    // MRIwrite(mri_median, "median.mgz") ;
  }
  else {
    mri_median = mri_brain;
  }

  sample_dist = .5 * mri_brain->xsize;

  // first compute white matter statistics over space

  sigma_vox = sigma / mri_brain->xsize;
  for (vno = 0; vno < mris->nvertices; vno++) {
    VERTEX * const v  = &mris->vertices[vno];
    if (vno == Gdiag_no) {
      DiagBreak();
    }
    if (v->ripflag) {
      continue;
    }
    // compute surface normal in voxel coords
    MRISsurfaceRASToVoxelCached(mris, mri_brain, v->x, v->y, v->z, &xv, &yv, &zv);
    x = v->x + v->nx;
    y = v->y + v->ny;
    z = v->z + v->nz;
    MRISsurfaceRASToVoxelCached(mris, mri_brain, x, y, z, &nx, &ny, &nz);
    nx -= xv;
    ny -= yv;
    nz -= zv;
    mag = sqrt(nx * nx + ny * ny + nz * nz);
    if (FZERO(mag)) {
      continue;
    }
    nx /= mag;
    ny /= mag;
    nz /= mag;

    // search inwards for min value to estimate wm value
    min_val = 1e10;
    for (dist = 0; dist <= 2; dist += sample_dist) {
      x = v->x - dist * v->nx;
      y = v->y - dist * v->ny;
      z = v->z - dist * v->nz;
      MRISsurfaceRASToVoxelCached(mris, mri_brain, x, y, z, &xv, &yv, &zv);
      if (MRIindexNotInVolume(mri_brain, xv, yv, zv) == 0) {
        MRIsampleVolume(mri_median, xv, yv, zv, &val);
        if (val < min_val) {
          int xi = nint(xv), yi = nint(yv), zi = nint(zv), wsize;
          float min_wm_val, mean_wm_val, std_wm_val;
          if (mri_wm) {
            wsize = nint(3.0 / mri_wm->xsize);
            wsize = (wsize / 2) * 2 + 1;
            min_wm_val = MRIvoxelMin(mri_wm, xi, yi, zi, wsize);
            mean_wm_val = MRIvoxelMean(mri_wm, xi, yi, zi, wsize, 0);
            std_wm_val = MRIvoxelStd(mri_wm, xi, yi, zi, mean_wm_val, wsize);

            if (min_val > min_wm_val) {
              min_val = val;
            }
            else {
              DiagBreak();
            }
          }
          else {
            min_val = val;
          }
        }
      }
    }
    v->val = min_val;
  }

  // let user do smoothing  MRISaverageVals(mris, 10) ;
  for (vno = 0; vno < mris->nvertices; vno++) {
    VERTEX_TOPOLOGY const * const vt = &mris->vertices_topology[vno];
    VERTEX                * const v  = &mris->vertices         [vno];
    if (vno == Gdiag_no) {
      DiagBreak();
    }
    if (v->ripflag) {
      continue;
    }
    wm_mean = v->val;
    wm_std = v->val * v->val;
    v->valbak = v->val2bak = v->val;  // min and max
    for (num = 1, n = 0; n < vt->vtotal; n++) {
      VERTEX const * const vn = &mris->vertices[vt->v[n]];
      if (vn->ripflag) {
        continue;
      }
      if (vn->val < v->valbak) {
        v->valbak = vn->val;
      }
      if (vn->val > v->val2bak) {
        v->val2bak = vn->val;
      }
      wm_mean += vn->val;
      wm_std += vn->val * vn->val;
      num++;
    }
    wm_mean /= num;
    wm_std = sqrt(wm_std / num - wm_mean * wm_mean);
    v->val2 = v->val = wm_mean;
    v->imag_val = wm_std;
  }
  if (Gdiag & DIAG_WRITE && DIAG_VERBOSE_ON) {
    MRISwriteValues(mris, "wm.mgz");
  }

  for (total_vertices = vno = 0; vno < mris->nvertices; vno++) {
    VERTEX * const v = &mris->vertices[vno];
    if (vno == Gdiag_no) {
      DiagBreak();
    }
    if (v->ripflag) {
      continue;
    }
    total_vertices++;

    // compute surface normal in voxel coords
    MRISsurfaceRASToVoxelCached(mris, mri_brain, v->x, v->y, v->z, &xv, &yv, &zv);
    x = v->x + v->nx;
    y = v->y + v->ny;
    z = v->z + v->nz;
    MRISsurfaceRASToVoxelCached(mris, mri_brain, x, y, z, &nx, &ny, &nz);
    nx -= xv;
    ny -= yv;
    nz -= zv;
    mag = sqrt(nx * nx + ny * ny + nz * nz);
    if (FZERO(mag)) {
      continue;
    }
    nx /= mag;
    ny /= mag;
    nz /= mag;

    // +- 2 standard deviations around the local mean
    // don't let std get too small
    wm_mean = v->val2;
    wm_std = v->imag_val;
    wm_std = MAX(wm_std, wm_mean * .1);  // don't let std be too low
    if (dir > 0)                         // allow outside to be brighter to allow for partial volume
    {
      wm_hi = wm_mean + 10 * wm_std;
      wm_lo = wm_mean - 2 * wm_std;
    }
    else {
      wm_hi = wm_mean + 2 * wm_std;
      wm_lo = wm_mean - 7 * wm_std;
    }
    max_grad = 0;
    max_grad_dist = 0;
    found = 0;
    /* search outwards for local maximum */
    for (dist = 0; dist <= max_thickness; dist += sample_dist) {
      x = v->x + dist * v->nx;
      y = v->y + dist * v->ny;
      z = v->z + dist * v->nz;
      MRISsurfaceRASToVoxelCached(mris, mri_brain, x, y, z, &xv, &yv, &zv);
      if (MRIindexNotInVolume(mri_brain, xv, yv, zv) == 0) {
        MRIsampleVolume(mri_brain, xv, yv, zv, &val);
        if (val < wm_lo || val > wm_hi) {
          break;
        }
        MRIsampleVolumeDerivativeScale(mri_brain, xv, yv, zv, nx, ny, nz, &grad, sigma_vox);
        if (grad * dir < 0)  // out of viable region
        {
          if (callno > 0 || dist > max_thickness / 2) {
            break;
          }
        }
        if (dir * grad > dir * max_grad)  // in the right direction
        {
          max_grad = grad;
          max_grad_dist = dist;
          MRIsampleVolume(mri_brain, xv, yv, zv, &max_grad_val);
          found = 1;
        }
      }
    }
    /* search inwards for local maximum */
    for (dist = 0; dist >= -max_thickness; dist -= sample_dist) {
      x = v->x + dist * v->nx;
      y = v->y + dist * v->ny;
      z = v->z + dist * v->nz;
      MRISsurfaceRASToVoxelCached(mris, mri_brain, x, y, z, &xv, &yv, &zv);
      if (MRIindexNotInVolume(mri_brain, xv, yv, zv) == 0) {
        MRIsampleVolume(mri_brain, xv, yv, zv, &val);
        if (val < wm_lo || val > wm_hi) {
          continue;  // allow search to continue as we haven't reached valid region yet
        }
        MRIsampleVolumeDerivativeScale(mri_brain, xv, yv, zv, nx, ny, nz, &grad, sigma_vox);
        if (grad * dir < 0)  // out of viable region
        {
          break;
        }
        if (fabs(grad) > fabs(max_grad))  // in the right direction
        {
          max_grad = grad;
          max_grad_dist = dist;
          MRIsampleVolume(mri_brain, xv, yv, zv, &max_grad_val);
          found = 1;
        }
      }
    }

    if (found) {
      if (vno == Gdiag_no)
        printf("max grad %2.3f found at distance %2.2f, val = %2.0f in [%2.0f %2.0f]\n",
               max_grad,
               max_grad_dist,
               max_grad_val,
               wm_lo,
               wm_hi);
      v->val = max_grad_val;
      v->marked = 1;
    }
    else {
      v->marked = 0;
      if (vno == Gdiag_no) {
        printf("v %d: could not find valid gradient maximum\n", vno);
      }
    }
  }
  for (total_vertices = vno = 0; vno < mris->nvertices; vno++) {
    VERTEX * const v = &mris->vertices[vno];
    if (vno == Gdiag_no) {
      DiagBreak();
    }
    if (v->ripflag) {
      continue;
    }
    v->val2 = sigma;
  }

  MRISsoapBubbleVals(mris, 100);
  if (mri_median != mri_brain) {
    MRIfree(&mri_median);
  }
  return (NO_ERROR);
}
int MRIScomputeMaxGradBorderValuesPial(MRI_SURFACE *mris,
                                       MRI *mri_brain,
                                       MRI *mri_smooth,
                                       double sigma,
                                       float max_thickness,
                                       float dir,
                                       FILE *log_fp,
                                       int callno,
                                       MRI *mri_mask)
{
  int total_vertices, vno, n, num, found;
  double x, y, z, xv, yv, zv, dist, grad, max_grad, max_grad_dist, sigma_vox, xm, ym, zm, nx, ny, nz, sample_dist, mag,
      max_grad_val, min_val, val, wm_mean, wm_std, wm_hi, wm_lo;
  MRI *mri_median, *mri_targets;
  MRI_REGION box;
  MATRIX *m_vox2vox;
  VECTOR *v1, *v2;
  float mval;

  mri_targets = MRIclone(mri_brain, NULL);

  m_vox2vox = MRIgetVoxelToVoxelXform(mri_brain, mri_mask);
  v1 = VectorAlloc(4, MATRIX_REAL);
  v2 = VectorAlloc(4, MATRIX_REAL);
  VECTOR_ELT(v1, 4) = 1.0;
  VECTOR_ELT(v2, 4) = 1.0;

  box.x = nint(5 / mri_brain->xsize);
  box.y = nint(5 / mri_brain->ysize);
  box.z = nint(5 / mri_brain->zsize);
  box.dx = mri_brain->width - 2 * nint(5 / mri_brain->xsize);
  box.dy = mri_brain->height - 2 * nint(5 / mri_brain->ysize);
  box.dz = mri_brain->depth - 2 * nint(5 / mri_brain->zsize);

  if (0) {
    // mri_median = MRImedian(mri_brain, NULL, 3, &box) ;
    // MRIwrite(mri_median, "median.mgz") ;
  }
  else {
    mri_median = mri_brain;
  }

  sample_dist = .5 * mri_brain->xsize;

  // first compute white matter statistics over space

  sigma_vox = sigma / mri_brain->xsize;
  for (vno = 0; vno < mris->nvertices; vno++) {
    VERTEX                * const v  = &mris->vertices         [vno];

    if (vno == Gdiag_no) {
      DiagBreak();
    }
    if (v->ripflag) {
      continue;
    }
    v->marked = 0;  // vertex is no good until marked is changed to 1
    // compute surface normal in voxel coords
    MRISsurfaceRASToVoxelCached(mris, mri_brain, v->x, v->y, v->z, &xv, &yv, &zv);
    x = v->x + v->nx;
    y = v->y + v->ny;
    z = v->z + v->nz;
    MRISsurfaceRASToVoxelCached(mris, mri_brain, x, y, z, &nx, &ny, &nz);
    nx -= xv;
    ny -= yv;
    nz -= zv;
    mag = sqrt(nx * nx + ny * ny + nz * nz);
    if (FZERO(mag)) {
      continue;
    }
    nx /= mag;
    ny /= mag;
    nz /= mag;

    /* search inwards for min value */
    min_val = 1e10;
    for (dist = 0; dist <= 3; dist += sample_dist) {
      x = v->x + dist * v->nx;
      y = v->y + dist * v->ny;
      z = v->z + dist * v->nz;
      MRISsurfaceRASToVoxelCached(mris, mri_brain, x, y, z, &xv, &yv, &zv);
      V3_X(v1) = xv;
      V3_Y(v1) = yv;
      V3_Z(v1) = zv;
      MatrixMultiply(m_vox2vox, v1, v2);
      xm = V3_X(v2);
      ym = V3_Y(v2);
      zm = V3_Z(v2);
      mval = MRIgetVoxVal(mri_mask, xm, ym, zm, 0);
      if (!FZERO(mval)) {
        continue;  // not in interior
      }
      if (MRIindexNotInVolume(mri_brain, xv, yv, zv) == 0) {
        MRIsampleVolume(mri_median, xv, yv, zv, &val);
        if (val < min_val) {
          v->marked = 1;
          min_val = val;
        }
      }
    }
    if (v->marked == 0) {
      DiagBreak();
      if (vno == Gdiag_no) {
        printf("v %d: could not find any interior voxels\n", vno);
      }
    }
    v->val = min_val;
  }

  // let user do smoothing  MRISaverageVals(mris, 10) ;
  for (vno = 0; vno < mris->nvertices; vno++) {
    VERTEX_TOPOLOGY const * const vt = &mris->vertices_topology[vno];
    VERTEX                * const v  = &mris->vertices         [vno];
    if (vno == Gdiag_no) {
      DiagBreak();
    }
    if (v->ripflag || v->marked == 0) {
      continue;
    }
    wm_mean = v->val;
    wm_std = v->val * v->val;
    v->valbak = v->val2bak = v->val;  // min and max
    for (num = 1, n = 0; n < vt->vtotal; n++) {
      VERTEX const * const vn = &mris->vertices[vt->v[n]];
      if (vn->ripflag || vn->marked == 0) {
        continue;
      }
      if (vn->val < v->valbak) {
        v->valbak = vn->val;
      }
      if (vn->val > v->val2bak) {
        v->val2bak = vn->val;
      }
      wm_mean += vn->val;
      wm_std += vn->val * vn->val;
      num++;
    }
    wm_mean /= num;
    wm_std = sqrt(wm_std / num - wm_mean * wm_mean);
    v->val2 = v->val = wm_mean;
    v->imag_val = wm_std;
  }
  if (Gdiag & DIAG_WRITE && DIAG_VERBOSE_ON) {
    MRISwriteValues(mris, "wm.mgz");
  }

  for (total_vertices = vno = 0; vno < mris->nvertices; vno++) {
    VERTEX                * const v  = &mris->vertices         [vno];
    if (vno == Gdiag_no) {
      DiagBreak();
    }
    if (v->ripflag || v->marked == 0) {
      continue;
    }
    total_vertices++;

    // compute surface normal in voxel coords
    MRISsurfaceRASToVoxelCached(mris, mri_brain, v->x, v->y, v->z, &xv, &yv, &zv);
    x = v->x + v->nx;
    y = v->y + v->ny;
    z = v->z + v->nz;
    MRISsurfaceRASToVoxelCached(mris, mri_brain, x, y, z, &nx, &ny, &nz);
    nx -= xv;
    ny -= yv;
    nz -= zv;
    mag = sqrt(nx * nx + ny * ny + nz * nz);
    if (FZERO(mag)) {
      continue;
    }
    nx /= mag;
    ny /= mag;
    nz /= mag;

    // +- 2 standard deviations around the local mean
    // don't let std get too small
    wm_mean = v->val2;
    wm_std = v->imag_val;
    wm_std = MAX(wm_std, wm_mean * .1);  // don't let std be too low
    if (dir > 0)                         // allow outside to be brighter to allow for partial volume
    {
      wm_hi = wm_mean + 10 * wm_std;
      wm_lo = wm_mean - 2 * wm_std;
    }
    else {
      wm_hi = wm_mean + 2 * wm_std;
      wm_lo = wm_mean - 7 * wm_std;
    }
    min_val = 2 * wm_hi;
    found = 0;
    /* search outwards for local maximum */
    max_grad = max_grad_val = max_grad_dist = 0;
    for (dist = 0; dist <= max_thickness; dist += sample_dist) {
      x = v->x + dist * v->nx;
      y = v->y + dist * v->ny;
      z = v->z + dist * v->nz;
      MRISsurfaceRASToVoxelCached(mris, mri_brain, x, y, z, &xv, &yv, &zv);
      V3_X(v1) = xv;
      V3_Y(v1) = yv;
      V3_Z(v1) = zv;
      MatrixMultiply(m_vox2vox, v1, v2);
      xm = V3_X(v2);
      ym = V3_Y(v2);
      zm = V3_Z(v2);
      mval = MRIgetVoxVal(mri_mask, xm, ym, zm, 0);
      if (FZERO(mval)) {
        continue;  // not in possible region
      }
      if (MRIindexNotInVolume(mri_brain, xv, yv, zv) == 0) {
        MRIsampleVolume(mri_brain, xv, yv, zv, &val);
#if 0
        if (val < wm_lo || val > wm_hi)
        {
          break ;
        }
#endif
        MRIsampleVolumeDerivativeScale(mri_brain, xv, yv, zv, nx, ny, nz, &grad, sigma_vox);
        if (grad * dir < 0)  // out of viable region
        {
          if (callno > 0 || dist > max_thickness / 2) {
            break;
          }
        }
        else if ((dir * grad > 0) && val < min_val) {
          min_val = val;
          found = 1;
          max_grad_dist = dist;
          max_grad_val = val;
        }

        if (dir * grad > dir * max_grad)  // in the right direction
        {
          max_grad = grad;
#if 0
          max_grad_dist = dist ;
          max_grad_val = val ;
          found = 1 ;
#endif
        }
      }
    }
    /* search inwards for local maximum */
    for (dist = 0; dist >= -max_thickness; dist -= sample_dist) {
      x = v->x + dist * v->nx;
      y = v->y + dist * v->ny;
      z = v->z + dist * v->nz;
      MRISsurfaceRASToVoxelCached(mris, mri_brain, x, y, z, &xv, &yv, &zv);
      V3_X(v1) = xv;
      V3_Y(v1) = yv;
      V3_Z(v1) = zv;
      MatrixMultiply(m_vox2vox, v1, v2);
      xm = V3_X(v2);
      ym = V3_Y(v2);
      zm = V3_Z(v2);
      mval = MRIgetVoxVal(mri_mask, xm, ym, zm, 0);
      if (FZERO(mval)) {
        continue;  // not in possible region
      }
      if (MRIindexNotInVolume(mri_brain, xv, yv, zv) == 0) {
        MRIsampleVolume(mri_brain, xv, yv, zv, &val);
#if 0
        if (val < wm_lo || val > wm_hi)
        {
          continue ;  // allow search to continue as we haven't reached valid region yet
        }
#endif
        MRIsampleVolumeDerivativeScale(mri_brain, xv, yv, zv, nx, ny, nz, &grad, sigma_vox);
        if (grad * dir < 0)  // out of viable region
        {
          break;
        }
        if ((dir * grad > 0) && val < min_val) {
          min_val = val;
          found = 1;
          max_grad_dist = dist;
          max_grad_val = val;
        }

        if (fabs(grad) > fabs(max_grad))  // in the right direction
        {
          max_grad = grad;
#if 0
          max_grad_dist = dist ;
          MRIsampleVolume(mri_brain, xv, yv, zv, &max_grad_val) ;
          found = 1 ;
#endif
        }
      }
    }

    if (found) {
      if (vno == Gdiag_no)
        printf("max grad %2.3f found at distance %2.2f, val = %2.0f in [%2.0f %2.0f]\n",
               max_grad,
               max_grad_dist,
               max_grad_val,
               wm_lo,
               wm_hi);
      v->val = max_grad_val;
      v->tx = v->x + max_grad_dist * v->nx;
      v->ty = v->y + max_grad_dist * v->ny;
      v->tz = v->z + max_grad_dist * v->nz;
      MRISsurfaceRASToVoxelCached(mris, mri_brain, v->tx, v->ty, v->tz, &xv, &yv, &zv);
      if (MRIindexNotInVolume(mri_targets, xv, yv, zv) == 0) {
        MRIsetVoxVal(mri_targets, xv, yv, zv, 0, 1.0);
      }
      else {
        DiagBreak();
      }
      v->marked = 1;
    }
    else {
      v->marked = 0;
      if (vno == Gdiag_no) {
        printf("v %d: could not find valid gradient maximum\n", vno);
      }
    }
  }
  for (total_vertices = vno = 0; vno < mris->nvertices; vno++) {
    VERTEX * const v  = &mris->vertices         [vno];
    if (vno == Gdiag_no) {
      DiagBreak();
    }
    if (v->ripflag) {
      continue;
    }
    v->val2 = sigma;
  }

  if (Gdiag & DIAG_WRITE && DIAG_VERBOSE_ON) {
    MRIwrite(mri_targets, "targets.mgz");
  }
  MRISsoapBubbleVals(mris, 100);
  if (mri_median != mri_brain) {
    MRIfree(&mri_median);
  }
  MatrixFree(&m_vox2vox);
  VectorFree(&v1);
  VectorFree(&v2);
  return (NO_ERROR);
}
#else
int MRIScomputeInvertedGrayWhiteBorderValues(MRI_SURFACE *mris,
                                             MRI *mri_brain,
                                             MRI *mri_smooth,
                                             double inside_hi,
                                             double border_hi,
                                             double border_low,
                                             double outside_low,
                                             double outside_hi,
                                             double sigma,
                                             float max_thickness,
                                             FILE *log_fp)
{
  double val, x, y, z, xw, yw, zw;
  int total_vertices, vno, inward_increasing, ngray, nwhite;
  VERTEX *v;
  double mean_white, mean_gray, std_white, std_gray, nsigma, gw_thresh;

  std_white = std_gray = mean_white = mean_gray = 0.0;
  for (ngray = nwhite = total_vertices = vno = 0; vno < mris->nvertices; vno++) {
    v = &mris->vertices[vno];
    if (v->ripflag) {
      continue;
    }
    total_vertices++;
    if (vno == Gdiag_no) {
      DiagBreak();
    }

    x = v->x + 1.0 * v->nx;
    y = v->y + 1.0 * v->ny;
    z = v->z + 1.0 * v->nz;
    MRISsurfaceRASToVoxelCached(mris, mri_brain, x, y, z, &xw, &yw, &zw);
    MRIsampleVolume(mri_brain, xw, yw, zw, &val);
    if (MRIindexNotInVolume(mri_brain, xw, yw, zw) == 0) {
      mean_gray += val;
      std_gray += (val * val);
      ngray++;
    }

    x = v->x - 0.5 * v->nx;
    y = v->y - 0.5 * v->ny;
    z = v->z - 0.5 * v->nz;
    MRISsurfaceRASToVoxelCached(mris, mri_brain, x, y, z, &xw, &yw, &zw);
    MRIsampleVolume(mri_brain, xw, yw, zw, &val);
    if (MRIindexNotInVolume(mri_brain, xw, yw, zw) == 0) {
      mean_white += val;
      std_white += (val * val);
      nwhite++;
    }
  }

  mean_white /= (float)nwhite;
  std_white = sqrt(std_white / (float)nwhite - mean_white * mean_white);
  mean_gray /= (float)ngray;
  std_gray = sqrt(std_gray / (float)ngray - mean_gray * mean_gray);
  nsigma = (mean_gray - mean_white) / (std_gray + std_white);
  gw_thresh = mean_white + nsigma * std_white;
  printf(
      "white %2.1f +- %2.1f,    gray %2.1f +- %2.1f, "
      "G/W boundary at %2.1f\n",
      mean_white,
      std_white,
      mean_gray,
      std_gray,
      gw_thresh);

  inward_increasing = mean_gray < mean_white;
  for (total_vertices = vno = 0; vno < mris->nvertices; vno++) {
    v = &mris->vertices[vno];
    if (v->ripflag) {
      continue;
    }
    if (vno == Gdiag_no) {
      DiagBreak();
    }
    v->val = gw_thresh;
  }

  return (NO_ERROR);
}
#endif
#define MAX_SAMPLES 1000
int MRIScomputeInvertedPialBorderValues(MRI_SURFACE *mris,
                                        MRI *mri_brain,
                                        MRI *mri_smooth,
                                        double inside_hi,
                                        double border_hi,
                                        double border_low,
                                        double outside_low,
                                        double outside_hi,
                                        double sigma,
                                        float max_thickness,
                                        FILE *log_fp)
{
  double val, x, y, z, xw, yw, zw, dist, prev_val, next_val;
  int total_vertices, vno, inward_increasing, ngray, ncsf;
  VERTEX *v;
  double mean_csf, mean_gray, std_csf, std_gray, nsigma, gw_thresh, csf_dist;

  std_gray = mean_gray = 0.0;
  for (ngray = total_vertices = vno = 0; vno < mris->nvertices; vno++) {
    v = &mris->vertices[vno];
    if (v->ripflag) {
      continue;
    }
    total_vertices++;
    if (vno == Gdiag_no) {
      DiagBreak();
    }

    /* first find distance to inflection point */
    csf_dist = -1;
    for (dist = 0; dist < 10; dist += SAMPLE_DIST) {
      x = v->x + dist * v->nx;
      y = v->y + dist * v->ny;
      z = v->z + dist * v->nz;
      MRISsurfaceRASToVoxelCached(mris, mri_brain, x, y, z, &xw, &yw, &zw);
      MRIsampleVolume(mri_brain, xw, yw, zw, &val);
      if (MRIindexNotInVolume(mri_brain, xw, yw, zw) == 0) {
        x = v->x + (dist + SAMPLE_DIST) * v->nx;
        y = v->y + (dist + SAMPLE_DIST) * v->ny;
        z = v->z + (dist + SAMPLE_DIST) * v->nz;
        MRISsurfaceRASToVoxelCached(mris, mri_brain, x, y, z, &xw, &yw, &zw);
        MRIsampleVolume(mri_brain, xw, yw, zw, &next_val);

        x = v->x + (dist - SAMPLE_DIST) * v->nx;
        y = v->y + (dist - SAMPLE_DIST) * v->ny;
        z = v->z + (dist - SAMPLE_DIST) * v->nz;
        MRISsurfaceRASToVoxelCached(mris, mri_brain, x, y, z, &xw, &yw, &zw);
        MRIsampleVolume(mri_brain, xw, yw, zw, &prev_val);
        if (val < next_val && val < prev_val) {
          if (vno == Gdiag_no) {
            printf("csf val found at %2.1f\n", val);
          }
          csf_dist = dist;
          break;
        }
      }
    }
    if (csf_dist < 0) {
      continue;
    }

    // compute gray values as 1 mm outside g/w surface.
    // Won't be right everywhere...
    dist = csf_dist / 2;
    x = v->x + dist * v->nx;
    y = v->y + dist * v->ny;
    z = v->z + dist * v->nz;
    MRISsurfaceRASToVoxelCached(mris, mri_brain, x, y, z, &xw, &yw, &zw);
    MRIsampleVolume(mri_brain, xw, yw, zw, &val);
    if (MRIindexNotInVolume(mri_brain, xw, yw, zw) == 0) {
      mean_gray += val;
      std_gray += (val * val);
      ngray++;
    }
  }

  mean_gray /= (float)ngray;
  std_gray = sqrt(std_gray / (float)ngray - mean_gray * mean_gray);

  std_csf = mean_csf = 0.0;
  for (ncsf = total_vertices = vno = 0; vno < mris->nvertices; vno++) {
    v = &mris->vertices[vno];
    if (v->ripflag) {
      continue;
    }
    total_vertices++;
    if (vno == Gdiag_no) {
      DiagBreak();
    }

    /* search for local maximum */
    for (dist = 0; dist < 10; dist += SAMPLE_DIST) {
      x = v->x + dist * v->nx;
      y = v->y + dist * v->ny;
      z = v->z + dist * v->nz;
      MRISsurfaceRASToVoxelCached(mris, mri_brain, x, y, z, &xw, &yw, &zw);
      MRIsampleVolume(mri_brain, xw, yw, zw, &val);
      if (val >= mean_gray - 0.5 * std_gray) {
        continue;
      }
      if (MRIindexNotInVolume(mri_brain, xw, yw, zw) == 0) {
        x = v->x + (dist + SAMPLE_DIST) * v->nx;
        y = v->y + (dist + SAMPLE_DIST) * v->ny;
        z = v->z + (dist + SAMPLE_DIST) * v->nz;
        MRISsurfaceRASToVoxelCached(mris, mri_brain, x, y, z, &xw, &yw, &zw);
        MRIsampleVolume(mri_brain, xw, yw, zw, &next_val);

        x = v->x + (dist - SAMPLE_DIST) * v->nx;
        y = v->y + (dist - SAMPLE_DIST) * v->ny;
        z = v->z + (dist - SAMPLE_DIST) * v->nz;
        MRISsurfaceRASToVoxelCached(mris, mri_brain, x, y, z, &xw, &yw, &zw);
        MRIsampleVolume(mri_brain, xw, yw, zw, &prev_val);
        if (val < next_val && val < prev_val) {
          if (vno == Gdiag_no) {
            printf("csf val found at %2.1f\n", val);
          }
          ncsf++;
          mean_csf += val;
          std_csf += (val * val);
          break;
        }
      }
    }
  }

  mean_csf /= (float)ncsf;
  std_csf = sqrt(std_csf / (float)ncsf - mean_csf * mean_csf);
  nsigma = (mean_gray - mean_csf) / (std_gray + std_csf);
  gw_thresh = mean_csf + nsigma * std_csf;
  printf(
      "csf %2.1f +- %2.1f,  "
      "gray %2.1f +- %2.1f, pial boundary at %2.1f\n",
      mean_csf,
      std_csf,
      mean_gray,
      std_gray,
      gw_thresh);

  inward_increasing = mean_gray < mean_csf;
  for (total_vertices = vno = 0; vno < mris->nvertices; vno++) {
    v = &mris->vertices[vno];
    if (v->ripflag) {
      continue;
    }
    if (vno == Gdiag_no) {
      DiagBreak();
    }
    v->val = gw_thresh;
  }

  return (NO_ERROR);
}
/*-----------------------------------------------------
  Parameters:

  Returns value:

  Description
  ------------------------------------------------------*/

int MRIScomputeGraySurfaceValues(MRI_SURFACE *mris, MRI *mri_brain, MRI *mri_smooth, float gray_surface)
{
  double val, x, y, z, min_val, xw, yw, zw, dx, dy, dz, mag, max_mag;
  int total_vertices, vno, nmissing;
  float mean_gray, dist;
  VERTEX *v;

  if (gray_surface <= 0.0f) {
    gray_surface = MAX_CSF;
  }

  /* first compute intensity of local gray/white boundary */
  mean_gray = 0.0f;

  MRISclearMarks(mris); /* for use in soap bubble later */
  for (nmissing = total_vertices = vno = 0; vno < mris->nvertices; vno++) {
    v = &mris->vertices[vno];
    if (v->ripflag) {
      continue;
    }
    if (vno == Gdiag_no) {
      DiagBreak();
    }

    /*
      search outwards and inwards and find the local gradient maximum
      at a location with a reasonable MR intensity value. This will
      be the location of the edge.
    */

    /* search in the normal direction to find the min value */
    min_val = -10.0f;
    mag = 5.0f;
    max_mag = 0.0f;
    for (dist = 0.0f; dist < 6.0f; dist += STEP_SIZE) {
      x = v->x + v->nx * dist;
      y = v->y + v->ny * dist;
      z = v->z + v->nz * dist;
      // MRIworldToVoxel(mri_brain, x, y, z, &xw, &yw, &zw) ;
      MRISsurfaceRASToVoxelCached(mris, mri_brain, x, y, z, &xw, &yw, &zw);
      MRIsampleVolume(mri_brain, xw, yw, zw, &val);
      if (val < 70 && val > gray_surface) /* in right range */
      {
        /* see if we are at a local maximum in the gradient magnitude */
        MRIsampleVolumeGradient(mri_smooth, xw, yw, zw, &dx, &dy, &dz);
        mag = sqrt(dx * dx + dy * dy + dz * dz);

        if (mag > max_mag) {
          max_mag = mag;
          min_val = val;
        }
      }
    }
    if (!isfinite(min_val) || !isfinite(max_mag) || !isfinite(mag)) {
      DiagBreak();
    }
    if (min_val > 0) {
      v->marked = 1;
      v->val = min_val;
      v->mean = max_mag;
      mean_gray += min_val;
      total_vertices++;
    }
    else {
      nmissing++;
      v->val = 0.0f;
    }

    if (vno == Gdiag_no) fprintf(stdout, "v %d, target value = %2.1f, mag = %2.1f\n", Gdiag_no, v->val, v->mean);
  }
  mean_gray /= (float)total_vertices;
  MRISsoapBubbleVals(mris, 100);
  MRISclearMarks(mris);
  /*  MRISaverageVals(mris, 3) ;*/
  fprintf(stdout, "mean pial surface=%2.1f, %d missing\n", mean_gray, nmissing);
  return (NO_ERROR);
}

/*-----------------------------------------------------
  Parameters:

  Returns value:

  Description
  ------------------------------------------------------*/
int MRISaccumulateMeansInVolume(MRI_SURFACE *mris, MRI *mri, int mris_dof, int mri_dof, int coordinate_system, int sno)
{
  VERTEX *vertex;
  double ndof, x, y, z, mean;
  int vno, xv, yv, zv;

  ndof = (double)(mris_dof + mri_dof);
  for (vno = 0; vno < mris->nvertices; vno++) {
    vertex = &mris->vertices[vno];
    if (vertex->val != 0.0f) {
      x = vertex->x;
      y = vertex->y;
      z = vertex->z;
      switch (coordinate_system) {
        case TALAIRACH_COORDS:
          // MRISworldToTalairachVoxel(mris, mri, x, y, z, &x, &y, &z) ;
          MRISsurfaceRASToTalairachVoxel(mris, mri, x, y, z, &x, &y, &z);
          break;
        default: /* surface-based */
          // MRIworldToVoxel(mri, x, y, z, &x, &y, &z) ;
          MRISsurfaceRASToVoxelCached(mris, mri, x, y, z, &x, &y, &z);
          break;
      }
      xv = nint(x);
      yv = nint(y);
      zv = nint(z);
      if ((xv >= 0 && xv < mri->width) && (yv >= 0 && yv < mri->height) && (zv >= 0 && zv < mri->depth)) {
        mean = MRIFseq_vox(mri, xv, yv, zv, sno);
        mean = (mris_dof * vertex->val + mri_dof * mean) / ndof;
        MRIFseq_vox(mri, xv, yv, zv, sno) = mean;
      }
    }
  }
  return (NO_ERROR);
}
/*-----------------------------------------------------
  Parameters:

  Returns value:

  Description
  actually these are squared standard errors
  ------------------------------------------------------*/
int MRISaccumulateStandardErrorsInVolume(
    MRI_SURFACE *mris, MRI *mri, int mris_dof, int mri_dof, int coordinate_system, int sno)
{
  VERTEX *vertex;
  double ndof, x, y, z, mris_sigma, mri_sigma;
  int vno, xv, yv, zv;

  ndof = (double)(mris_dof + mri_dof);

  /*
    now that we have the values read in, go through the surface, and
    map each vertex into the structural volume via its ellipsoidal
    coordinate.
  */

  for (vno = 0; vno < mris->nvertices; vno++) {
    vertex = &mris->vertices[vno];
    if (vertex->val != 0.0f) {
      x = vertex->x;
      y = vertex->y;
      z = vertex->z;
      switch (coordinate_system) {
        case TALAIRACH_COORDS:
          // MRISworldToTalairachVoxel(mris, mri, x, y, z, &x, &y, &z) ;
          MRISsurfaceRASToTalairachVoxel(mris, mri, x, y, z, &x, &y, &z);
          break;
        default: /* surface-based */
          // MRIworldToVoxel(mri, x, y, z, &x, &y, &z) ;
          MRISsurfaceRASToVoxelCached(mris, mri, x, y, z, &x, &y, &z);
          break;
      }
      xv = nint(x);
      yv = nint(y);
      zv = nint(z);

      if ((xv < 0 || xv >= mri->width) || ((yv < 0) || (yv >= mri->height)) || ((zv < 0) || (zv >= mri->depth))) {
        continue;
      }

      mri_sigma = MRIFseq_vox(mri, xv, yv, zv, sno); /* variance */
      mris_sigma = vertex->val;
      mri_sigma = mris_sigma * SQR(mris_dof) + mri_sigma * SQR(mri_dof);
      mri_sigma /= SQR(ndof);
      if (!isfinite(mri_sigma)) {
        fprintf(stderr, "variance not finite at vno %d!\n", vno);
        DiagBreak();
      }
      MRIFseq_vox(mri, xv, yv, zv, sno) = mri_sigma;
    }
  }

  return (NO_ERROR);
}


int mrisUpdateTargetLocations(MRI_SURFACE *mris, MRI *mri, double target_intensity)
{
  int vno;
  double xv, yv, zv, val, val0, xv0, yv0, zv0;
  VERTEX *v;

  for (vno = 0; vno < mris->nvertices; vno++) {
    v = &mris->vertices[vno];
    if (v->ripflag) continue;
    MRISsurfaceRASToVoxelCached(mris, mri, v->origx, v->origy, v->origz, &xv0, &yv0, &zv0);
    MRIsampleVolume(mri, xv0, yv0, zv0, &val0);
    MRISsurfaceRASToVoxelCached(mris, mri, v->x, v->y, v->z, &xv, &yv, &zv);
    MRIsampleVolume(mri, xv, yv, zv, &val);
    if (vno == Gdiag_no)
      printf("v %d: orig (%2.1f, %2.1f, %2.1f) = %2.1f, current (%2.1f, %2.1f, %2.1f) = %2.1f, marked %d, ripped %d\n",
             vno,
             xv0,
             yv0,
             zv0,
             val0,
             xv,
             yv,
             zv,
             val,
             v->marked,
             v->ripflag);
    if ((val0 > target_intensity && val < target_intensity) || (val0 < target_intensity && val > target_intensity) ||
        (val < target_intensity) ||  // for now
        MRIindexNotInVolume(mri, xv, yv, zv)) {
      if (!v->marked) {
        if (vno == Gdiag_no)
          printf("resetting target location from (%2.1f, %2.1f, %2.1f) to (%2.1f, %2.1f, %2.1f)\n",
                 v->targx,
                 v->targy,
                 v->targz,
                 v->x,
                 v->y,
                 v->z);
        v->targx = v->x;
        v->targy = v->y;
        v->targz = v->z;
        v->marked = 1;
        //    v->ripflag = 1 ;
        v->val2 = val0;
      }
      v->val = val;
    }
  }
  return (NO_ERROR);
}


/*-------------------------------------------------------------
  MRI *MRISfbirnMask_SFG_Cing(MRIS *surf) - creates a mask in the
  SFG where it borders CAcing, PAcing, and RAcing.
  -------------------------------------------------------------*/
MRI *MRISfbirnMask_SFG_Cing(MRIS *surf)
{
  int vtxno, annot, annotid, nnbrs, nbrvtx, nthnbr, nbrannotid;
  int superiorfrontal, posteriorcingulate;
  int caudalanteriorcingulate, rostralanteriorcingulate;
  MRI *mri;

  superiorfrontal = 28;
  posteriorcingulate = 23;
  caudalanteriorcingulate = 2;
  rostralanteriorcingulate = 26;

  mri = MRIalloc(surf->nvertices, 1, 1, MRI_INT);

  for (vtxno = 0; vtxno < surf->nvertices; vtxno++) {
    MRIsetVoxVal(mri, vtxno, 0, 0, 0, 0);

    annot = surf->vertices[vtxno].annotation;
    CTABfindAnnotation(surf->ct, annot, &annotid);

    // Skip if not one of the target areas
    if (annotid != superiorfrontal && annotid != posteriorcingulate && annotid != caudalanteriorcingulate &&
        annotid != rostralanteriorcingulate) {
      continue;
    }

    nnbrs = surf->vertices_topology[vtxno].vnum;
    for (nthnbr = 0; nthnbr < nnbrs; nthnbr++) {
      nbrvtx = surf->vertices_topology[vtxno].v[nthnbr];
      if (surf->vertices[nbrvtx].ripflag) {
        continue;  // skip ripped vtxs
      }
      annot = surf->vertices[nbrvtx].annotation;
      CTABfindAnnotation(surf->ct, annot, &nbrannotid);
      if (annotid == superiorfrontal && (nbrannotid == posteriorcingulate || nbrannotid == caudalanteriorcingulate ||
                                         nbrannotid == rostralanteriorcingulate)) {
        MRIsetVoxVal(mri, vtxno, 0, 0, 0, 1);
      }
    }
  }
  return (mri);
}

/*-------------------------------------------------------------
  MRI *MRISfbirnMask_MOF_RACing(MRIS *surf) - creates a mask at the intersection
  Meidal Orbital Frontal and RA Cingulate
  -------------------------------------------------------------*/
MRI *MRISfbirnMask_MOF_RACing(MRIS *surf)
{
  int vtxno, annot, annotid, nnbrs, nbrvtx, nthnbr, nbrannotid;
  int medialorbitofrontal, rostralanteriorcingulate;
  MRI *mri;

  CTABfindName(surf->ct, "medialorbitofrontal", &medialorbitofrontal);
  CTABfindName(surf->ct, "rostralanteriorcingulate", &rostralanteriorcingulate);

  printf("medialorbitofrontal %d\n", medialorbitofrontal);
  printf("rostralanteriorcingulate %d\n", rostralanteriorcingulate);

  mri = MRIalloc(surf->nvertices, 1, 1, MRI_INT);

  for (vtxno = 0; vtxno < surf->nvertices; vtxno++) {
    MRIsetVoxVal(mri, vtxno, 0, 0, 0, 0);

    annot = surf->vertices[vtxno].annotation;
    CTABfindAnnotation(surf->ct, annot, &annotid);

    if (annotid != medialorbitofrontal) {
      continue;
    }

    nnbrs = surf->vertices_topology[vtxno].vnum;
    for (nthnbr = 0; nthnbr < nnbrs; nthnbr++) {
      nbrvtx = surf->vertices_topology[vtxno].v[nthnbr];
      if (surf->vertices[nbrvtx].ripflag) {
        continue;  // skip ripped vtxs
      }

      annot = surf->vertices[nbrvtx].annotation;
      CTABfindAnnotation(surf->ct, annot, &nbrannotid);
      if (nbrannotid == rostralanteriorcingulate) {
        MRIsetVoxVal(mri, vtxno, 0, 0, 0, 1);
      }
    }
  }
  return (mri);
}

int
MRISimportValFromMRI(MRI_SURFACE *mris, MRI *mri, int frame) 
{
  VERTEX  *v ;
  int     vno ;

  for (vno = 0 ; vno < mris->nvertices ; vno++)
  {
    v = &mris->vertices[vno] ;
    if (v->ripflag)
      continue ;

    v->val = MRIgetVoxVal(mri, vno, 0, 0, frame) ;
  }
  return(NO_ERROR) ;
}

/*-----------------------------------------------------
  Parameters:

  Returns value:

  Description
  ------------------------------------------------------*/
int MRISpaintVolume(MRI_SURFACE *mris, LTA *lta, MRI *mri)
{
  VERTEX *v;
  int vno, width, height, depth;
  double x, y, z, val;
  MATRIX *m_L, *m_ras_to_voxel;
  VECTOR *v_surf, *v_vol;

  if (lta->type != LINEAR_RAS_TO_RAS)
    ErrorReturn(ERROR_UNSUPPORTED, (ERROR_UNSUPPORTED, "MRISsampleVolume: unsupported transform type %d", lta->type));

  v_surf = VectorAlloc(4, MATRIX_REAL);
  v_vol = VectorAlloc(4, MATRIX_REAL);
  *MATRIX_RELT(v_surf, 4, 1) = 1.0;
  *MATRIX_RELT(v_vol, 4, 1) = 1.0;
  m_ras_to_voxel = MRIgetRasToVoxelXform(mri);

  m_L = MatrixMultiply(m_ras_to_voxel, lta->xforms[0].m_L, NULL);
  width = mri->width;
  height = mri->height;
  depth = mri->depth;
  for (vno = 0; vno < mris->nvertices; vno++) {
    if (vno == Gdiag_no) {
      DiagBreak();
    }
    v = &mris->vertices[vno];
    V3_X(v_surf) = v->x + .5 * v->curv * v->nx;
    V3_Y(v_surf) = v->y + .5 * v->curv * v->ny;
    V3_Z(v_surf) = v->z + .5 * v->curv * v->nz;

    MatrixMultiply(m_L, v_surf, v_vol);
    x = V3_X(v_vol);
    y = V3_Y(v_vol);
    z = V3_Z(v_vol);

    MRIsampleVolume(mri, x, y, z, &val);
    v->val = val;
    if (Gdiag_no == vno)
      printf(
          "vertex %d at (%2.1f, %2.1f, %2.1f) --> "
          "voxel (%2.1f, %2.1f, %2.1f) = %2.2f\n",
          vno,
          v->x + .5 * v->curv * v->nx,
          v->y + .5 * v->curv * v->ny,
          v->z + .5 * v->curv * v->nz,
          x,
          y,
          z,
          val);
  }

  MatrixFree(&v_surf);
  MatrixFree(&v_vol);
  MatrixFree(&m_L);
  MatrixFree(&m_ras_to_voxel);
  return (NO_ERROR);
}

/*-----------------------------------------------------------------
  MRISloadSurfVals() - loads surfaces values directly into an MRI
  structure. The surface value file can be any format read by
  MRIread. In addition, it can be a curv or paint file. If
  Surf is non-null, then it is used as a template; otherwise,
  the caller must spec the subject and hemisphere, and then the
  ?h.white surface is used as the template (and then freed).

  If the source file is neither curv nor paint, then MRIreadType is
  used to read the file in as a "volume", and the "volume" is reshaped
  to be nvertices X 1 X 1 X nframes (which is the final size
  regardless).

  If the source is curv format, then the given file is read from the
  subject's surf directory. If Surf is non-null, then the sujbect
  name and hemisphere in the MRI_SURFACE struct are used; otherwise
  they must be passed.

  If the subjectsdir string is NULL, then it reads SUBJECTS_DIR
  from the environment.
  -----------------------------------------------------------------*/
MRI *MRISloadSurfVals(const char *srcvalfile,
                      const char *typestring,
                      MRI_SURFACE *Surf,
                      const char *subject,
                      const char *hemi,
                      const char *subjectsdir)
{
  MRI *SrcVals, *mritmp;
  char fname[2000];
  int srctype, reshapefactor = 0, f;
  float *framepower = NULL;
  SXADAT *sxa;
  int freesurface = 0, err = 0;

  if (Surf == NULL) {
    /*-------- set SUBJECTS DIR -------------*/
    if (subjectsdir == NULL) {
      subjectsdir = getenv("SUBJECTS_DIR");
      if (subjectsdir == NULL) {
        fprintf(stderr, "ERROR: SUBJECTS_DIR not defined in environment\n");
        return (NULL);
      }
    }
    /*------- load the surface -------------*/
    sprintf(fname, "%s/%s/surf/%s.white", subjectsdir, subject, hemi);
    printf("INFO: loading surface %s\n", fname);
    Surf = MRISread(fname);
    if (Surf == NULL) {
      fprintf(stderr, "ERROR: could not read %s\n", fname);
      return (NULL);
    }
    freesurface = 1;
  }
  else {
    subject = Surf->subject_name;
    if (Surf->hemisphere == LEFT_HEMISPHERE) {
      hemi = "lh";
    }
    if (Surf->hemisphere == RIGHT_HEMISPHERE) {
      hemi = "rh";
    }
  }

  /* ------------------ load the source data ----------------------------*/
  printf("Loading surface source data %s as %s\n", srcvalfile, typestring);
  if (!strcmp(typestring, "curv")) {
    /* curvature file */
    sprintf(fname, "%s/%s/surf/%s.%s", subjectsdir, subject, hemi, srcvalfile);
    printf("Reading curvature file %s\n", fname);
    err = MRISreadCurvatureFile(Surf, fname);
    if (err) {
      printf("ERROR: reading curvature\n");
      return (NULL);
    }
    SrcVals = MRIcopyMRIS(NULL, Surf, 0, "curv");
    if (SrcVals == NULL) {
      printf("ERROR: converting surface curv to MRI\n");
      return (NULL);
    }

    // SrcVals = MRIallocSequence(Surf->nvertices, 1, 1,MRI_FLOAT,1);
    // for(vtx = 0; vtx < Surf->nvertices; vtx++){
    //  MRIFseq_vox(SrcVals,vtx,0,0,0) = Surf->vertices[vtx].curv;
    //}
  }
  else if (!strcmp(typestring, "paint") || !strcmp(typestring, "w")) {
    MRISreadValues(Surf, srcvalfile);
    SrcVals = MRIcopyMRIS(NULL, Surf, 0, "val");
    // SrcVals = MRIallocSequence(Surf->nvertices, 1, 1,MRI_FLOAT,1);
    // for(vtx = 0; vtx < Surf->nvertices; vtx++)
    //  MRIFseq_vox(SrcVals,vtx,0,0,0) = Surf->vertices[vtx].val;
  }
  else {
    /* Use MRIreadType */
    srctype = string_to_type(typestring);
    if (srctype == MRI_VOLUME_TYPE_UNKNOWN) {
      printf("ERROR: typestring %s unrecognized\n", typestring);
      return (NULL);
    }
    SrcVals = MRIreadType(srcvalfile, srctype);
    if (SrcVals == NULL) {
      printf("ERROR: could not read %s as type %d\n", srcvalfile, srctype);
      return (NULL);
    }
    if (SrcVals->height != 1 || SrcVals->depth != 1) {
      reshapefactor = SrcVals->height * SrcVals->depth;
      printf("Reshaping %d\n", reshapefactor);
      mritmp = mri_reshape(SrcVals, reshapefactor * SrcVals->width, 1, 1, SrcVals->nframes);
      MRIfree(&SrcVals);
      SrcVals = mritmp;
    }

    if (SrcVals->width != Surf->nvertices) {
      fprintf(stdout, "ERROR: dimension inconsistency in source data\n");
      fprintf(stdout, "       Number of surface vertices = %d\n", Surf->nvertices);
      fprintf(stdout, "       Number of value vertices = %d\n", SrcVals->width);
      return (NULL);
    }
    if (0 && is_sxa_volume(srcvalfile)) {
      // Dont need to do this anymore 10/19/2010
      printf("INFO: Source volume detected as selxavg format\n");
      sxa = ld_sxadat_from_stem(srcvalfile);
      if (sxa == NULL) {
        return (NULL);
      }
      framepower = sxa_framepower(sxa, &f);
      if (f != SrcVals->nframes) {
        fprintf(stderr, " number of frames is incorrect (%d,%d)\n", f, SrcVals->nframes);
        return (NULL);
      }
      printf("INFO: Adjusting Frame Power\n");
      fflush(stdout);
      mri_framepower(SrcVals, framepower);
    }
  }
  if (SrcVals == NULL) {
    fprintf(stderr, "ERROR loading source values from %s\n", srcvalfile);
    return (NULL);
  }
  printf("Done Loading %s\n", srcvalfile);

  if (freesurface) {
    MRISfree(&Surf);
  }

  return (SrcVals);
}
/*-----------------------------------------------------------------
  MRIScopyMRI() - copies the data from an MRI_VOLUME struct into a
  field in the MRI_SURFACE vertex struct. The MRI_VOLUME struct is
  assumed to have the dimension such that ncols*nrows*nslices =
  nvertices. Frame is the zero-based frame number to copy. Field is a
  string that indicates which field of the vertex structure the data
  should be copied to. For example, "val" indicates the val field.
  Other supported fields are: val, stat, valbak, val2, val2bak,
  imag_val, curv, curvbak, fsmask, nc. Others can be easily added. If
  there is an error, a 1 is returned; otherwise 0.
  -----------------------------------------------------------------*/
int MRIScopyMRI(MRIS *Surf, MRI *Src, int Frame, const char *Field)
{
  int vtx, useval = 0, usecurv = 0, nvox, c, r, s;
  float val;

  if (Gdiag_no > 0) {
    printf("MRIScopyMRI\n");
  }

  nvox = Src->width * Src->height * Src->depth;
  if (Surf->nvertices != nvox) {
    printf("ERROR: MRIScopyMRI: Surf/Src dimension mismatch.\n");
    return (1);
  }

  if (Frame >= Src->nframes) {
    printf("ERROR: MRIScopyMRI: requested frame number is too large.\n");
    printf("ERROR:   requested = %d, max = %d\n", Frame, Src->nframes);
    return (1);
  }

  /* A separate variable is used for val and curv for speed purposes */
  if (!strcmp(Field, "val")) {
    useval = 1;
  }
  else {
    useval = 0;
  }
  if (!strcmp(Field, "curv")) {
    usecurv = 1;
  }
  else {
    usecurv = 0;
  }

  /*------------------------------------------------*/
  vtx = 0;
  for (s = 0; s < Src->depth; s++) {
    for (r = 0; r < Src->height; r++) {
      for (c = 0; c < Src->width; c++) {
        val = MRIgetVoxVal(Src, c, r, s, Frame);
        // val = MRIgetVoxVal(Src, vtx, 0, 0, Frame); // was vtx,0,0 dng, wrong

        if (useval) {
          Surf->vertices[vtx].val = val;
        }
        else if (usecurv) {
          Surf->vertices[vtx].curv = val;
        }
        else if (!strcmp(Field, "stat")) {
          Surf->vertices[vtx].stat = val;
        }
        else if (!strcmp(Field, "valbak")) {
          Surf->vertices[vtx].valbak = val;
        }
        else if (!strcmp(Field, "val2")) {
          Surf->vertices[vtx].val2 = val;
        }
        else if (!strcmp(Field, "val2bak")) {
          Surf->vertices[vtx].val2bak = val;
        }
        else if (!strcmp(Field, "imag_val")) {
          Surf->vertices[vtx].imag_val = val;
        }
        else if (!strcmp(Field, "curvbak")) {
          Surf->vertices[vtx].curvbak = val;
        }
        else if (!strcmp(Field, "fsmask")) {
          Surf->vertices[vtx].fsmask = val;
        }
        else if (!strcmp(Field, "nc")) {
          Surf->vertices[vtx].nc = val;
        }
        else if (!strcmp(Field, "undefval")) {
          Surf->vertices[vtx].undefval = val;
        }
        else if (!strcmp(Field, "x")) {
          Surf->vertices[vtx].x = val;
        }
        else if (!strcmp(Field, "y")) {
          Surf->vertices[vtx].y = val;
        }
        else if (!strcmp(Field, "z")) {
          Surf->vertices[vtx].z = val;
        }
        
        // Setting this field requires lots of related changes
        //else if (!strcmp(Field, "vnum")) {
        //  Surf->vertices_topology[vtx].vnum = val;
        //}
        
        else if (!strcmp(Field, "annotation")) {
          Surf->vertices[vtx].annotation = val;
        }
        else if (!strcmp(Field, "ripflag")) {
          Surf->vertices[vtx].ripflag = val;
        }
        else if (!strcmp(Field, "area")) {
          Surf->vertices[vtx].area = val;
        }
        else if (!strcmp(Field, "group_avg_area")) {
          Surf->vertices[vtx].group_avg_area = val;
        }
        else if (!strcmp(Field, "K")) {
          Surf->vertices[vtx].K = val;
        }
        else if (!strcmp(Field, "H")) {
          Surf->vertices[vtx].H = val;
        }
        else if (!strcmp(Field, "k1")) {
          Surf->vertices[vtx].k1 = val;
        }
        else if (!strcmp(Field, "k2")) {
          Surf->vertices[vtx].k2 = val;
        }
        else if (!strcmp(Field, "nx")) {
          Surf->vertices[vtx].nx = val;
        }
        else if (!strcmp(Field, "ny")) {
          Surf->vertices[vtx].ny = val;
        }
        else if (!strcmp(Field, "nz")) {
          Surf->vertices[vtx].nz = val;
        }
        else if (!strcmp(Field, "tx")) {
          Surf->vertices[vtx].tx = val;
        }
        else if (!strcmp(Field, "ty")) {
          Surf->vertices[vtx].ty = val;
        }
        else if (!strcmp(Field, "tz")) {
          Surf->vertices[vtx].tz = val;
        }
        else if (!strcmp(Field, "dx")) {
          Surf->vertices[vtx].dx = val;
        }
        else if (!strcmp(Field, "dy")) {
          Surf->vertices[vtx].dy = val;
        }
        else if (!strcmp(Field, "dz")) {
          Surf->vertices[vtx].dz = val;
        }
        else if (!strcmp(Field, "tdx")) {
          Surf->vertices[vtx].tdx = val;
        }
        else if (!strcmp(Field, "tdy")) {
          Surf->vertices[vtx].tdy = val;
        }
        else if (!strcmp(Field, "tdz")) {
          Surf->vertices[vtx].tdz = val;
        }
        else {
          printf("ERROR: MRIScopyMRI(): Field %s not supported\n", Field);
          return (1);
        }
        vtx++;
      }
    }
  }
  return (0);
}
/*-----------------------------------------------------------------
  MRIcopyMRIS() - copies the data from the given field of an
  MRI_SURFACE struct into a given frame of an MRI_VOLUME struct. The
  MRI_VOLUME should have the dimension such that: ncols*nrows*nslices
  = nvertices.  Frame is the zero-based frame number to copy to. Field
  is a string that indicates which field of the vertex structure the
  data should be copied from. For example, "val" indicates the val
  field.  Other supported fields are: val, stat, valbak, val2,
  val2bak, imag_val, curv, curvbak, fsmask, nc. If mri is NULL, it
  will be allocated with nframes=Frame+1 (ie, just enough frames) and
  type will be MRI_FLOAT. A pointer to mri is returned. If an error
  occurs, NULL is returned.
  -----------------------------------------------------------------*/
MRI *MRIcopyMRIS(MRI *mri, MRIS *surf, int Frame, const char *Field)
{
  int vtx, useval = 0, usecurv = 0, nvox, c, r, s;
  float val;

  if (mri == NULL) {
    mri = MRIallocSequence(surf->nvertices, 1, 1, MRI_FLOAT, Frame + 1);
    if (mri == NULL) {
      printf("ERROR: MRIcopyMRIS: could not alloc\n");
      return (NULL);
    }
  }
  nvox = mri->width * mri->height * mri->depth;
  if (surf->nvertices != nvox) {
    printf("ERROR: MRIcopyMRIS: Surf/Src dimension mismatch.\n");
    return (NULL);
  }
  if (Frame >= mri->nframes) {
    printf("ERROR: MRIScopyMRI: requested frame number is too large.\n");
    printf("ERROR:   requested = %d, max = %d\n", Frame, mri->nframes);
    return (NULL);
  }

  /* A separate variable is used for val and curv for speed purposes */
  if (!strcmp(Field, "val")) {
    useval = 1;
  }
  else {
    useval = 0;
  }
  if (!strcmp(Field, "curv")) {
    usecurv = 1;
  }
  else {
    usecurv = 0;
  }

  /*------------------------------------------------*/
  vtx = 0;
  for (s = 0; s < mri->depth; s++) {
    for (r = 0; r < mri->height; r++) {
      for (c = 0; c < mri->width; c++) {
        if (useval) {
          val = surf->vertices[vtx].val;
        }
        else if (usecurv) {
          val = surf->vertices[vtx].curv;
        }
        else if (!strcmp(Field, "stat")) {
          val = surf->vertices[vtx].stat;
        }
        else if (!strcmp(Field, "d")) {
          val = surf->vertices[vtx].d;
        }
        else if (!strcmp(Field, "valbak")) {
          val = surf->vertices[vtx].valbak;
        }
        else if (!strcmp(Field, "val2")) {
          val = surf->vertices[vtx].val2;
        }
        else if (!strcmp(Field, "val2bak")) {
          val = surf->vertices[vtx].val2bak;
        }
        else if (!strcmp(Field, "imag_val")) {
          val = surf->vertices[vtx].imag_val;
        }
        else if (!strcmp(Field, "curvbak")) {
          val = surf->vertices[vtx].curvbak;
        }
        else if (!strcmp(Field, "fsmask")) {
          val = surf->vertices[vtx].fsmask;
        }
        else if (!strcmp(Field, "fieldsign")) {
          val = surf->vertices[vtx].fieldsign;
        }
        else if (!strcmp(Field, "nc")) {
          val = surf->vertices[vtx].nc;
        }
        else if (!strcmp(Field, "undefval")) {
          val = surf->vertices[vtx].undefval;
        }
        else if (!strcmp(Field, "x")) {
          val = surf->vertices[vtx].x;
        }
        else if (!strcmp(Field, "y")) {
          val = surf->vertices[vtx].y;
        }
        else if (!strcmp(Field, "z")) {
          val = surf->vertices[vtx].z;
        }
        else if (!strcmp(Field, "vnum")) {
          val = surf->vertices_topology[vtx].vnum;
        }
        else if (!strcmp(Field, "annotation")) {
          val = surf->vertices[vtx].annotation;
        }
        else if (!strcmp(Field, "ripflag")) {
          val = surf->vertices[vtx].ripflag;
        }
        else if (!strcmp(Field, "area")) {
          val = surf->vertices[vtx].area;
        }
        else if (!strcmp(Field, "group_avg_area")) {
          val = surf->vertices[vtx].group_avg_area;
        }
        else if (!strcmp(Field, "K")) {
          val = surf->vertices[vtx].K;
        }
        else if (!strcmp(Field, "H")) {
          val = surf->vertices[vtx].H;
        }
        else if (!strcmp(Field, "k1")) {
          val = surf->vertices[vtx].k1;
        }
        else if (!strcmp(Field, "k2")) {
          val = surf->vertices[vtx].k2;
        }
        else if (!strcmp(Field, "nx")) {
          val = surf->vertices[vtx].nx;
        }
        else if (!strcmp(Field, "ny")) {
          val = surf->vertices[vtx].ny;
        }
        else if (!strcmp(Field, "nz")) {
          val = surf->vertices[vtx].nz;
        }
        else if (!strcmp(Field, "tx")) {
          val = surf->vertices[vtx].tx;
        }
        else if (!strcmp(Field, "ty")) {
          val = surf->vertices[vtx].ty;
        }
        else if (!strcmp(Field, "tz")) {
          val = surf->vertices[vtx].tz;
        }
        else if (!strcmp(Field, "tdx")) {
          val = surf->vertices[vtx].tdx;
        }
        else if (!strcmp(Field, "tdy")) {
          val = surf->vertices[vtx].tdy;
        }
        else if (!strcmp(Field, "tdz")) {
          val = surf->vertices[vtx].tdz;
        }
        else if (!strcmp(Field, "dx")) {
          val = surf->vertices[vtx].dx;
        }
        else if (!strcmp(Field, "dy")) {
          val = surf->vertices[vtx].dy;
        }
        else if (!strcmp(Field, "dz")) {
          val = surf->vertices[vtx].dz;
        }
        else {
          printf("ERROR: MRIScopyMRI(): Field %s not supported\n", Field);
          return (NULL);
        }
        MRIsetVoxVal(mri, c, r, s, Frame, val);
        vtx++;
      }
    }
  }
  return (mri);
}
/*-------------------------------------------------------------------
  MRISsmoothMRI() - smooths values on the surface when the surface
  values are stored in an MRI_VOLUME structure with the number of
  spatial voxels equal to the number of nvertices on the surface. Can
  handle multiple frames. Can be performed in-place. If Targ is NULL,
  it will automatically allocate a new MRI structure. Note that the
  input MRI struct does not have to have any particular configuration
  of cols, rows, and slices as long as the product equals nvertices.
  Does not smooth data from ripped vertices into unripped vertices
  (but does go the other way). Same for mask. If mask is NULL, it
  is ignored. See also MRISsmoothMRIFast()
  -------------------------------------------------------------------*/
MRI *MRISsmoothMRI(MRIS *Surf, MRI *Src, int nSmoothSteps, MRI *BinMask, MRI *Targ)
{
  int nnbrs, nthstep, frame, vtx, nbrvtx, nthnbr, **crslut, c, r, s, nvox;
  int nnbrs_actual;
  float val, m;
  MRI *SrcTmp;
  struct timeb mytimer;
  int msecTime;
  const char *UFSS;

  // Must explicity "setenv USE_FAST_SURF_SMOOTHER 0" to turn off fast
  UFSS = getenv("USE_FAST_SURF_SMOOTHER");
  if (!UFSS) {
    UFSS = "1";
  }
  if (strcmp(UFSS, "0")) {
    Targ = MRISsmoothMRIFast(Surf, Src, nSmoothSteps, BinMask, Targ);
    return (Targ);
  }
  if (Gdiag_no > 0) {
    printf("MRISsmoothMRI()\n");
  }

  nvox = Src->width * Src->height * Src->depth;
  if (Surf->nvertices != nvox) {
    printf("ERROR: MRISsmooth: Surf/Src dimension mismatch\n");
    return (NULL);
  }

  // Build LUT to map from col,row,slice to vertex
  crslut = MRIScrsLUT(Surf, Src);

  if (Targ == NULL) {
    Targ = MRIallocSequence(Src->width, Src->height, Src->depth, MRI_FLOAT, Src->nframes);
    if (Targ == NULL) {
      printf("ERROR: MRISsmooth: could not alloc\n");
      return (NULL);
    }
    MRIcopyHeader(Src, Targ);
  }
  else {
    if (Src->width != Targ->width || Src->height != Targ->height || Src->depth != Targ->depth ||
        Src->nframes != Targ->nframes) {
      printf("ERROR: MRISsmooth: output dimension mismatch\n");
      return (NULL);
    }
    if (Targ->type != MRI_FLOAT) {
      printf("ERROR: MRISsmooth: structure passed is not MRI_FLOAT\n");
      return (NULL);
    }
  }

  /*------------------------------------------------------------*/
  TimerStart(&mytimer);
  SrcTmp = MRIcopy(Src, NULL);
  for (nthstep = 0; nthstep < nSmoothSteps; nthstep++) {
    if (Gdiag_no > 1) {
      msecTime = TimerStop(&mytimer);
      printf("Step = %d, tsec = %g\n", nthstep, msecTime / 1000.0);
      fflush(stdout);
    }

    for (vtx = 0; vtx < Surf->nvertices; vtx++) {
      nnbrs = Surf->vertices_topology[vtx].vnum;
      c = crslut[0][vtx];
      r = crslut[1][vtx];
      s = crslut[2][vtx];
      if (BinMask) {
        m = MRIgetVoxVal(BinMask, c, r, s, 0);
        if (m < 0.5) {
          for (frame = 0; frame < Targ->nframes; frame++) {
            MRIFseq_vox(Targ, c, r, s, frame) = 0;
          }
          continue;
        }
      }
      for (frame = 0; frame < Targ->nframes; frame++) {
        val = MRIFseq_vox(SrcTmp, c, r, s, frame);

        nnbrs_actual = 0;
        for (nthnbr = 0; nthnbr < nnbrs; nthnbr++) {
          nbrvtx = Surf->vertices_topology[vtx].v[nthnbr];
          if (Surf->vertices[nbrvtx].ripflag) {
            continue;  // skip ripped vtxs
          }
          // check mask
          if (BinMask) {
            m = MRIgetVoxVal(BinMask, crslut[0][nbrvtx], crslut[1][nbrvtx], crslut[2][nbrvtx], 0);
            if (m < 0.5) {
              continue;
            }
          }
          val += MRIFseq_vox(SrcTmp, crslut[0][nbrvtx], crslut[1][nbrvtx], crslut[2][nbrvtx], frame);
          nnbrs_actual++;
        } /* end loop over neighbor */

        MRIFseq_vox(Targ, c, r, s, frame) = (val / (nnbrs_actual + 1));
      } /* end loop over frame */

    } /* end loop over vertex */

    MRIcopy(Targ, SrcTmp);
  } /* end loop over smooth step */

  msecTime = TimerStop(&mytimer);
  if (Gdiag_no > 0) {
    printf("Smoothing done, nsteps = %d, tsec = %g\n", nthstep, msecTime / 1000.0);
  }
  fflush(stdout);

  MRIfree(&SrcTmp);
  MRIScrsLUTFree(crslut);
  return (Targ);
}
/*-------------------------------------------------------------------
  MRISsmoothMRIFast() - faster version of MRISsmoothMRI(). Smooths
  values on the surface when the surface values are stored in an
  MRI_VOLUME structure with the number of spatial voxels equal to the
  number of nvertices on the surface. Can handle multiple frames. Can
  be performed in-place. If Targ is NULL, it will automatically
  allocate a new MRI structure. Note that the input MRI struct does
  not have to have any particular configuration of cols, rows, and
  slices as long as the product equals nvertices.  Does not smooth
  data from ripped vertices into unripped vertices (but does go the
  other way). Same for mask. The mask is inclusive, so voxels with
  mask=1 are included. If mask is NULL, it is ignored. Gives identical
  results as MRISsmoothMRI(); see MRISsmoothMRIFastCheck().
  -------------------------------------------------------------------*/
MRI *MRISsmoothMRIFast(MRIS *Surf, MRI *Src, int nSmoothSteps, MRI *IncMask, MRI *Targ)
{
  int nnbrs, nthstep, frame, vno, nthnbr, num, nvox, nbrvno, reshape;
  MRI *SrcTmp, *mritmp, *IncMaskTmp = NULL;
  struct timeb mytimer;
  int msecTime;
  int *nNbrs, *nNbrs0, *rip, *rip0, nNbrsMax;
  float **pF, **pF0, *tF, *tF0, sumF;
  VERTEX *v, *vn;

  if (Gdiag_no > 0) printf("MRISsmoothMRIFast()\n");

  nvox = Src->width * Src->height * Src->depth;
  if (Surf->nvertices != nvox) {
    printf("ERROR: MRISsmoothMRIFast(): Surf/Src dimension mismatch\n");
    return (NULL);
  }
  if (IncMask) {
    if (IncMask->width * IncMask->height * IncMask->depth != nvox) {
      printf("ERROR: MRISsmoothMRIFast(): Surf/Mask dimension mismatch\n");
      return (NULL);
    }
    if (IncMask->width != nvox)
      IncMaskTmp = mri_reshape(IncMask, nvox, 1, 1, IncMask->nframes);
    else
      IncMaskTmp = MRIcopy(IncMask, NULL);
  }
  // Reshape the source if needed
  if (Src->width != nvox) {
    if (Gdiag_no > 0) printf("MRISsmoothMRIFast(): reshaping\n");
    SrcTmp = mri_reshape(Src, nvox, 1, 1, Src->nframes);
    reshape = 1;
  }
  else {
    reshape = 0;
    SrcTmp = MRIcopy(Src, NULL);
  }
  if (Targ != NULL) {
    if (MRIdimMismatch(Src, Targ, 1)) {
      printf("ERROR: MRISsmoothFast(): output dimension mismatch\n");
      return (NULL);
    }
    if (Targ->type != MRI_FLOAT) {
      printf("ERROR: MRISsmoothFast(): structure passed is not MRI_FLOAT\n");
      return (NULL);
    }
  }

  // Alloc arrays. If there are ripped vertices, then only rip
  // needs nvertices elements
  nNbrsMax = 12;  // Should measure this, but overalloc does not hurt
  pF = (float **)calloc(Surf->nvertices * nNbrsMax, sizeof(float *));
  tF = (float *)calloc(Surf->nvertices, sizeof(float));
  nNbrs = (int *)calloc(Surf->nvertices, sizeof(int));
  rip = (int *)calloc(Surf->nvertices, sizeof(int));

  pF0 = pF;
  tF0 = tF;
  rip0 = rip;
  nNbrs0 = nNbrs;

  TimerStart(&mytimer);

  // Loop through frames
  for (frame = 0; frame < Src->nframes; frame++) {
    // Set up pointers for this frame
    pF = pF0;
    rip = rip0;
    nNbrs = nNbrs0;
    for (vno = 0; vno < Surf->nvertices; vno++) {
      v = &Surf->vertices[vno];
      if (IncMaskTmp && MRIgetVoxVal(IncMaskTmp, vno, 0, 0, 0) < 0.5) {
        // Mask is inclusive, so look for out of mask
        // should exclude rips here too? Original does not.
        rip[vno] = 1;
        MRIFseq_vox(SrcTmp, vno, 0, 0, frame) = 0;
        continue;
      }
      rip[vno] = 0;
      *pF++ = (float *)(&(MRIFseq_vox(SrcTmp, vno, 0, 0, frame)));
      nnbrs = Surf->vertices_topology[vno].vnum;
      num = 1;
      for (nthnbr = 0; nthnbr < nnbrs; nthnbr++) {
        nbrvno = Surf->vertices_topology[vno].v[nthnbr];
        vn = &Surf->vertices[nbrvno];
        if (vn->ripflag) {
          continue;
        }
        if (IncMaskTmp && MRIgetVoxVal(IncMaskTmp, nbrvno, 0, 0, 0) < 0.5) {
          continue;
        }
        *pF++ = (float *)(&(MRIFseq_vox(SrcTmp, nbrvno, 0, 0, frame)));
        num++;
      }
      *nNbrs++ = num;  // num takes into account all rips/masks
    }

    // Step through the iterations
    for (nthstep = 0; nthstep < nSmoothSteps; nthstep++) {
      // Init pointers for this iteration
      pF = pF0;
      rip = rip0;
      tF = tF0;
      nNbrs = nNbrs0;
      // Loop through vertices, average nearest neighbors
      for (vno = 0; vno < Surf->nvertices; vno++) {
        if (*rip++) {
          continue;
        }
        sumF = *(*pF++);
        for (nthnbr = 0; nthnbr < (*nNbrs) - 1; nthnbr++) {
          sumF += *(*pF++);
        }
        *tF++ = sumF / (*nNbrs);
        nNbrs++;
      }
      // Load up for the next step
      rip = rip0;
      tF = tF0;
      for (vno = 0; vno < Surf->nvertices; vno++) {
        if (*rip++) {
          continue;
        }
        MRIsetVoxVal(SrcTmp, vno, 0, 0, frame, *tF++);
      }
    }

  } /* end loop over frame */

  // Copy to the output
  if (reshape) {
    if (Gdiag_no > 0) printf("MRISsmoothFast() reshaping again\n");
    mritmp = mri_reshape(SrcTmp, Src->width, Src->height, Src->depth, Src->nframes);
    Targ = MRIcopy(mritmp, Targ);
    MRIfree(&mritmp);
  }
  else
    Targ = MRIcopy(SrcTmp, Targ);

  msecTime = TimerStop(&mytimer);
  if (Gdiag_no > 0) {
    printf("MRISsmoothFast() nsteps = %d, tsec = %g\n", nSmoothSteps, msecTime / 1000.0);
    fflush(stdout);
  }

  MRIfree(&SrcTmp);
  if (IncMaskTmp) MRIfree(&IncMaskTmp);
  free(pF0);
  free(tF0);
  free(rip0);
  free(nNbrs0);

  return (Targ);
}

/*-------------------------------------------------------------------
  MRISsmoothMRIFastD() - basically the same thing as MRISsmoothMRIFasD()
  but uses a double array internally to reduce accumulation errors.
  Tests indicate that there is not much difference between using
  float or using double (and this function is slower than the float
  version).
  -------------------------------------------------------------------*/
MRI *MRISsmoothMRIFastD(MRIS *Surf, MRI *Src, int nSmoothSteps, MRI *IncMask, MRI *Targ)
{
  int nnbrs, nthstep, frame, vno, nthnbr, num, nvox, nbrvno, c, r, s;
  MRI *IncMaskTmp = NULL;
  struct timeb mytimer;
  int msecTime;
  int *nNbrs, *nNbrs0, *rip, *rip0, nNbrsMax;
  double **pF, **pF0, *tF, *tF0, sumF;
  double *pD, *pD0;
  VERTEX *v, *vn;

  if (Gdiag_no > 0) printf("MRISsmoothMRIFastD()\n");

  nvox = Src->width * Src->height * Src->depth;
  if (Surf->nvertices != nvox) {
    printf("ERROR: MRISsmoothMRIFastD(): Surf/Src dimension mismatch\n");
    return (NULL);
  }
  if (IncMask) {
    if (IncMask->width * IncMask->height * IncMask->depth != nvox) {
      printf("ERROR: MRISsmoothMRIFastD(): Surf/Mask dimension mismatch\n");
      return (NULL);
    }
    if (IncMask->width != nvox)
      IncMaskTmp = mri_reshape(IncMask, nvox, 1, 1, IncMask->nframes);
    else
      IncMaskTmp = MRIcopy(IncMask, NULL);
  }
  if (Targ == NULL) {
    Targ = MRIallocSequence(Src->width, Src->height, Src->depth, MRI_FLOAT, Src->nframes);
    if (Targ == NULL) {
      printf("ERROR: MRISsmoothMRIFastD(): could not alloc\n");
      return (NULL);
    }
    MRIcopyHeader(Src, Targ);
  }
  if (MRIdimMismatch(Src, Targ, 1)) {
    printf("ERROR: MRISsmoothFastD(): output dimension mismatch\n");
    return (NULL);
  }
  if (Targ->type != MRI_FLOAT) {
    printf("ERROR: MRISsmoothFastD(): structure passed is not MRI_FLOAT\n");
    return (NULL);
  }

  // Alloc arrays. If there are ripped vertices, then only rip
  // needs nvertices elements
  nNbrsMax = 12;  // Should measure this, but overalloc does not hurt
  pF = (double **)calloc(Surf->nvertices * nNbrsMax, sizeof(double *));
  tF = (double *)calloc(Surf->nvertices, sizeof(double));
  nNbrs = (int *)calloc(Surf->nvertices, sizeof(int));
  rip = (int *)calloc(Surf->nvertices, sizeof(int));
  pD = (double *)calloc(Surf->nvertices, sizeof(double));

  pF0 = pF;
  tF0 = tF;
  rip0 = rip;
  nNbrs0 = nNbrs;
  pD0 = pD;

  TimerStart(&mytimer);

  // Loop through frames
  for (frame = 0; frame < Src->nframes; frame++) {
    // Set up pointers for this frame
    pD = pD0;
    pF = pF0;
    rip = rip0;
    nNbrs = nNbrs0;
    for (vno = 0; vno < Surf->nvertices; vno++) {
      v = &Surf->vertices[vno];
      if (IncMaskTmp && MRIgetVoxVal(IncMaskTmp, vno, 0, 0, 0) < 0.5) {
        // Mask is inclusive, so look for out of mask
        // should exclude rips here too? Original does not.
        rip[vno] = 1;
        *pD++ = 0;
        continue;
      }
      *pD++ = MRIgetVoxVal(Src, vno, 0, 0, frame);
      *pF++ = (double *)(&(pD0[vno]));
      rip[vno] = 0;
      nnbrs = Surf->vertices_topology[vno].vnum;
      num = 1;
      for (nthnbr = 0; nthnbr < nnbrs; nthnbr++) {
        nbrvno = Surf->vertices_topology[vno].v[nthnbr];
        vn = &Surf->vertices[nbrvno];
        if (vn->ripflag) continue;
        if (IncMaskTmp && MRIgetVoxVal(IncMaskTmp, nbrvno, 0, 0, 0) < 0.5) continue;
        *pF++ = (double *)(&(pD0[nbrvno]));
        num++;
      }
      *nNbrs++ = num;  // num takes into account all rips/masks
    }

    // Step through the iterations
    for (nthstep = 0; nthstep < nSmoothSteps; nthstep++) {
      // Init pointers for this iteration
      pF = pF0;
      rip = rip0;
      tF = tF0;
      nNbrs = nNbrs0;
      // Loop through vertices, average nearest neighbors
      for (vno = 0; vno < Surf->nvertices; vno++) {
        if (*rip++) continue;
        sumF = *(*pF++);
        for (nthnbr = 0; nthnbr < (*nNbrs) - 1; nthnbr++) sumF += *(*pF++);
        *tF++ = sumF / (*nNbrs);
        nNbrs++;
      }
      // Load up for the next step
      rip = rip0;
      tF = tF0;
      for (vno = 0; vno < Surf->nvertices; vno++) {
        if (*rip++) continue;
        pD0[vno] = *tF++;
      }
    }  // end iteration steps

    // Pack output back into MRI structure (float)
    pD = pD0;
    for (c = 0; c < Targ->width; c++) {
      for (r = 0; r < Targ->height; r++) {
        for (s = 0; s < Targ->depth; s++) {
          MRIsetVoxVal(Targ, c, r, s, frame, (*pD));
          pD++;
        }
      }
    }

  } /* end loop over frame */

  msecTime = TimerStop(&mytimer);
  if (Gdiag_no > 0) {
    printf("MRISsmoothFastD() nsteps = %d, tsec = %g\n", nSmoothSteps, msecTime / 1000.0);
    fflush(stdout);
  }

  if (IncMaskTmp) MRIfree(&IncMaskTmp);
  free(pF0);
  free(tF0);
  free(rip0);
  free(nNbrs0);
  free(pD0);

  return (Targ);
}

/*------------------------------------------------------------------
  MRISsmoothMRIFastFrame() same as MRISsmoothMRIFast() but operates
  on a single frame. This allows the pointers to be cached in
  a static data structure. Note: this will fail if Src is not
  nvertices x 1 x 1 x nframes.
  ------------------------------------------------------------------*/
int MRISsmoothMRIFastFrame(MRIS *Surf, MRI *Src, int frame, int nSmoothSteps, MRI *IncMask)
{
  int nnbrs, nthstep, vno, nthnbr, num, nvox, nbrvno;
  struct timeb mytimer;
  int msecTime;
  float sumF;
  VERTEX *v, *vn;
  static MRIS *SurfInit = NULL;
  static MRI *SrcInit = NULL;
  static int DoInit = 1, frameInit = 0, *nNbrs = NULL, *nNbrs0 = NULL, *rip = NULL, *rip0 = NULL, nNbrsMax = 0;
  static float **pF = NULL, **pF0 = NULL, *tF = NULL, *tF0 = NULL;

  if (Gdiag_no > 0) printf("MRISsmoothMRIFastFrame()\n");

  if (DoInit) {
    if (Gdiag_no > 0) {
      printf("MRISsmoothMRIFastFrame() Init\n");
    }
    nvox = Src->width * Src->height * Src->depth;
    if (Surf->nvertices != nvox) {
      printf("ERROR: MRISsmoothMRIFastFrame(): Surf/Src dimension mismatch\n");
      return (1);
    }
    // Alloc arrays. If there are ripped vertices, then only rip
    // needs nvertices elements
    nNbrsMax = 12;  // Should measure this, but overalloc does not hurt
    pF = (float **)calloc(Surf->nvertices * nNbrsMax, sizeof(float *));
    tF = (float *)calloc(Surf->nvertices, sizeof(float));
    nNbrs = (int *)calloc(Surf->nvertices, sizeof(int));
    rip = (int *)calloc(Surf->nvertices, sizeof(int));
    pF0 = pF;
    tF0 = tF;
    rip0 = rip;
    nNbrs0 = nNbrs;
    SrcInit = Src;
    SurfInit = Surf;
    frameInit = frame;
    DoInit = 0;
  }
  if (Src != SrcInit) {
    printf("ERROR: Src and SrcInit do not agree\n");
    exit(1);
  }
  if (Surf != SurfInit) {
    printf("ERROR: Surf and SurfInit do not agree\n");
    exit(1);
  }
  if (frame != frameInit) {
    printf("ERROR: frame and frameInit do not agree\n");
    exit(1);
  }

  TimerStart(&mytimer);

  // Set up pointers for this call
  pF = pF0;
  rip = rip0;
  nNbrs = nNbrs0;
  for (vno = 0; vno < Surf->nvertices; vno++) {
    v = &Surf->vertices[vno];
    if (IncMask && MRIgetVoxVal(IncMask, vno, 0, 0, 0) < 0.5) {
      // Mask is inclusive, so look for out of mask
      // should exclude rips here too? Original does not.
      rip[vno] = 1;
      MRIFseq_vox(Src, vno, 0, 0, frame) = 0;
      continue;
    }
    rip[vno] = 0;
    *pF++ = (float *)(&(MRIFseq_vox(Src, vno, 0, 0, frame)));
    nnbrs = Surf->vertices_topology[vno].vnum;
    num = 1;
    for (nthnbr = 0; nthnbr < nnbrs; nthnbr++) {
      nbrvno = Surf->vertices_topology[vno].v[nthnbr];
      vn = &Surf->vertices[nbrvno];
      if (vn->ripflag) {
        continue;
      }
      if (IncMask && MRIgetVoxVal(IncMask, nbrvno, 0, 0, 0) < 0.5) {
        continue;
      }
      *pF++ = (float *)(&(MRIFseq_vox(Src, nbrvno, 0, 0, frame)));
      num++;
    }
    *nNbrs++ = num;  // num takes into account all rips/masks
  }

  // Step through the iterations
  for (nthstep = 0; nthstep < nSmoothSteps; nthstep++) {
    // Init pointers for this iteration
    pF = pF0;
    rip = rip0;
    tF = tF0;
    nNbrs = nNbrs0;
    // Loop through vertices, average nearest neighbors
    for (vno = 0; vno < Surf->nvertices; vno++) {
      if (*rip++) {
        continue;
      }
      sumF = *(*pF++);
      for (nthnbr = 0; nthnbr < (*nNbrs) - 1; nthnbr++) {
        sumF += *(*pF++);
      }
      *tF++ = sumF / (*nNbrs);
      nNbrs++;
    }
    // Load up for the next step
    rip = rip0;
    tF = tF0;
    for (vno = 0; vno < Surf->nvertices; vno++) {
      if (*rip++) {
        continue;
      }
      MRIsetVoxVal(Src, vno, 0, 0, frame, *tF++);
    }
  }

  msecTime = TimerStop(&mytimer);
  if (Gdiag_no > 0) {
    printf("MRISsmoothFast2() nsteps = %d, tsec = %g\n", nSmoothSteps, msecTime / 1000.0);
    fflush(stdout);
  }

  return (0);
}
/*--------------------------------------------------------------*/
int MRISsmoothMRIFastCheck(int nSmoothSteps)
{
  char tmpstr[2000], *UFSS;
  MRIS *mris;
  MRI *src, *mri1, *mri2, *mask;
  int k, nerrs, c, r, s, f, cmax, rmax, smax, fmax;
  float val1, val2, diff, maxdiff;

  // Make sure to turn off override (restored later)
  UFSS = getenv("USE_FAST_SURF_SMOOTHER");
  setenv("USE_FAST_SURF_SMOOTHER", "0", 1);

  printf("MRISsmoothMRIFastCheck() nSmoothSteps = %d\n", nSmoothSteps);

  sprintf(tmpstr, "%s/subjects/fsaverage/surf/lh.white", getenv("FREESURFER_HOME"));
  printf("Reading surface %s\n", tmpstr);
  mris = MRISread(tmpstr);
  if (mris == NULL) {
    printf("ERROR: could not read %s\n", tmpstr);
    return (-1);
  }

  // Use 3 frames
  src = MRIrandn(mris->nvertices, 1, 1, 3, .5, 1, NULL);

  // Create mask
  mask = MRIconst(mris->nvertices, 1, 1, 1, 1.0, NULL);
  for (k = 0; k < mris->nvertices - 1; k++) {
    MRIsetVoxVal(mask, k, 0, 0, 0, 0.0);  // turn off mask
    mris->vertices[k + 1].ripflag = 1;    // rip a few
  }

  printf("Running slow smoother\n");
  mri1 = MRISsmoothMRI(mris, src, nSmoothSteps, mask, NULL);
  printf("Running fast smoother\n");
  mri2 = MRISsmoothMRIFast(mris, src, nSmoothSteps, mask, NULL);

  printf("Checking differences\n");
  nerrs = 0;
  cmax = 0;
  rmax = 0;
  smax = 0;
  fmax = 0;
  maxdiff = 0.0;
  for (c = 0; c < src->width; c++) {
    for (r = 0; r < src->height; r++) {
      for (s = 0; s < src->depth; s++) {
        for (f = 0; f < src->nframes; f++) {
          val1 = MRIgetVoxVal(mri1, c, r, s, f);
          val2 = MRIgetVoxVal(mri2, c, r, s, f);
          diff = val1 - val2;
          if (fabs(maxdiff) < fabs(diff)) {
            maxdiff = diff;
            cmax = c;
            rmax = r;
            smax = s;
            fmax = f;
          }
          if (!FZERO(diff)) {
            nerrs++;
          }
        }
      }
    }
  }
  printf("nerrs = %d, maxdiff %f at %d %d %d %d\n", nerrs, maxdiff, cmax, rmax, smax, fmax);

  setenv("USE_FAST_SURF_SMOOTHER", UFSS, 1);

  return (nerrs);
}


/*-------------------------------------------------------------------
  MRISremoveRippedFromMask() - sets voxels in mask to 0 if corresponding
  vertex has been ripped.
  -------------------------------------------------------------------*/
MRI *MRISremoveRippedFromMask(MRIS *surf, MRI *mask, MRI *outmask)
{
  int c, r, s, vtx;
  outmask = MRIcopy(mask, outmask);
  vtx = 0;
  for (s = 0; s < mask->depth; s++) {
    for (r = 0; r < mask->height; r++) {
      for (c = 0; c < mask->width; c++) {
        if (surf->vertices[vtx].ripflag) {
          MRIsetVoxVal(outmask, c, r, s, 0, 0.0);
        }
        vtx++;
      }
    }
  }
  return (outmask);
}
/*------------------------------------------------------------------
  MRISlabel2Mask() - creates a mask from the label by setting each
  voxel corresponding to a label point to 1 with the rest 0.
  ------------------------------------------------------------------*/
MRI *MRISlabel2Mask(MRIS *surf, LABEL *lb, MRI *mask)
{
  int vtxno, n;

  if (mask == NULL)  // create mask as all 0s
  {
    mask = MRIconst(surf->nvertices, 1, 1, 1, 0, NULL);
  }

  for (n = 0; n < lb->n_points; n++) {
    vtxno = lb->lv[n].vno;
    if (vtxno >= surf->nvertices) {
      printf("ERROR: MRISlabel2Mask(): label vertex %d is >= nvertices %d\n", vtxno, surf->nvertices);
      fflush(stdout);
      return (NULL);
    }
    MRIsetVoxVal(mask, vtxno, 0, 0, 0, 1);
  }
  return (mask);
}

// expand the surface by "h" and create a volume
// which has "val" outside of this surface
unsigned long MRISeraseOutsideOfSurface(float h, MRI *mri_dst, MRIS *mris, unsigned char val)
{
  int i, j, k, imnr;
  float x0, y0, z0, x1, y1, z1, x2, y2, z2, d0, d1, d2, dmax, u, v;
  float px, py, pz, px0, py0, pz0, px1, py1, pz1;
  int numu, numv, totalfilled, newfilled;
  double tx, ty, tz;
  unsigned long brainsize;

  int width, height, depth;
  MRI *mri_buff;

  width = mri_dst->width;
  height = mri_dst->height;
  depth = mri_dst->depth;

  mri_buff = MRIalloc(width, height, depth, MRI_UCHAR);

  for (k = 0; k < mris->nvertices; k++) {
    // cache the values
    mris->vertices[k].tx = mris->vertices[k].x;
    mris->vertices[k].ty = mris->vertices[k].y;
    mris->vertices[k].tz = mris->vertices[k].z;

    // expand by h using normal
    mris->vertices[k].x += h * mris->vertices[k].nx;
    mris->vertices[k].y += h * mris->vertices[k].ny;
    mris->vertices[k].z += h * mris->vertices[k].nz;
  }

  for (k = 0; k < mris->nfaces; k++) {
    // calculate three vertices
    x0 = mris->vertices[mris->faces[k].v[0]].x;
    y0 = mris->vertices[mris->faces[k].v[0]].y;
    z0 = mris->vertices[mris->faces[k].v[0]].z;
    x1 = mris->vertices[mris->faces[k].v[1]].x;
    y1 = mris->vertices[mris->faces[k].v[1]].y;
    z1 = mris->vertices[mris->faces[k].v[1]].z;
    x2 = mris->vertices[mris->faces[k].v[2]].x;
    y2 = mris->vertices[mris->faces[k].v[2]].y;
    z2 = mris->vertices[mris->faces[k].v[2]].z;
    // calculate the sides
    d0 = sqrt(SQR(x1 - x0) + SQR(y1 - y0) + SQR(z1 - z0));
    d1 = sqrt(SQR(x2 - x1) + SQR(y2 - y1) + SQR(z2 - z1));
    d2 = sqrt(SQR(x0 - x2) + SQR(y0 - y2) + SQR(z0 - z2));
    dmax = (d0 >= d1 && d0 >= d2) ? d0 : (d1 >= d0 && d1 >= d2) ? d1 : d2;
    numu = (int)(ceil(2 * d0));
    numv = (int)(ceil(2 * dmax));

    for (v = 0; v <= numv; v++) {
      px0 = x0 + (x2 - x0) * v / numv;
      py0 = y0 + (y2 - y0) * v / numv;
      pz0 = z0 + (z2 - z0) * v / numv;
      px1 = x1 + (x2 - x1) * v / numv;
      py1 = y1 + (y2 - y1) * v / numv;
      pz1 = z1 + (z2 - z1) * v / numv;
      for (u = 0; u <= numu; u++) {
        px = px0 + (px1 - px0) * u / numu;
        py = py0 + (py1 - py0) * u / numu;
        pz = pz0 + (pz1 - pz0) * u / numu;

        // MRIworldToVoxel(mri_dst,px,py,pz,&tx,&ty,&tz);
        MRISsurfaceRASToVoxelCached(mris, mri_dst, px, py, pz, &tx, &ty, &tz);

        imnr = (int)(tz + 0.5);
        j = (int)(ty + 0.5);
        i = (int)(tx + 0.5);
        if (i >= 0 && i < width && j >= 0 && j < height && imnr >= 0 && imnr < depth) {
          MRIvox(mri_buff, i, j, imnr) = 255;
        }
      }
    }
  }

  MRIvox(mri_buff, 1, 1, 1) = 64;
  totalfilled = newfilled = 1;
  while (newfilled > 0) {
    newfilled = 0;
    for (k = 0; k < depth; k++)
      for (j = 0; j < height; j++)
        for (i = 0; i < width; i++)
          if (MRIvox(mri_buff, i, j, k) == 0)
            if (MRIvox(mri_buff, i, j, mri_buff->zi[k - 1]) == 64 ||
                MRIvox(mri_buff, i, mri_buff->yi[j - 1], k) == 64 ||
                MRIvox(mri_buff, mri_buff->xi[i - 1], j, k) == 64) {
              MRIvox(mri_buff, i, j, k) = 64;
              newfilled++;
            }
    for (k = depth - 1; k >= 0; k--)
      for (j = height - 1; j >= 0; j--)
        for (i = width - 1; i >= 0; i--)
          if (MRIvox(mri_buff, i, j, k) == 0)
            if (MRIvox(mri_buff, i, j, mri_buff->zi[k + 1]) == 64 ||
                MRIvox(mri_buff, i, mri_buff->yi[j + 1], k) == 64 ||
                MRIvox(mri_buff, mri_buff->xi[i + 1], j, k) == 64) {
              MRIvox(mri_buff, i, j, k) = 64;
              newfilled++;
            }
    totalfilled += newfilled;
  }

  // modify mri_dst so that outside = 0
  brainsize = 0;
  if (val == 0)
    for (k = 0; k < depth; k++)
      for (j = 0; j < height; j++)
        for (i = 0; i < width; i++) {
          if (MRIvox(mri_buff, i, j, k) == 64) {
            MRIvox(mri_dst, i, j, k) = 0;
          }
          else {
            brainsize++;
          }
        }
  else {
    for (k = 0; k < depth; k++)
      for (j = 0; j < height; j++)
        for (i = 0; i < width; i++) {
          if (MRIvox(mri_buff, i, j, k) != 64) {
            MRIvox(mri_dst, i, j, k) = val;
          }
          else {
            brainsize++;
          }
        }
  }
  // restore the surface
  for (k = 0; k < mris->nvertices; k++) {
    mris->vertices[k].x = mris->vertices[k].tx;
    mris->vertices[k].y = mris->vertices[k].ty;
    mris->vertices[k].z = mris->vertices[k].tz;
  }
  // calculate the normals
  MRIScomputeNormals(mris);

  MRIfree(&mri_buff);
  return brainsize;
}

/*--------------------------------------------------------------------------
  MRISgaussianWeights() - fills the weight (4th) row of the MRI dist
  struct.  dist is the distance between vertices as returned by
  MRISdistSphere().  The first 3 rows of dist correspond to: 0=actual
  number of extended neighbors, 1=vertex number of extended neighbors,
  2=distance along the sphere. The weight will be placed in the 4th
  row.  Each of the extended neighbrs is placed in a frame. The weights
  for a given vertex will be normalized so that the sum=1.
  --------------------------------------------------------------------------*/
int MRISgaussianWeights(MRIS *surf, MRI *dist, double GStd)
{
  int n, m, nXNbrs;
  double GVar2, f, gsum, d, g;

  GVar2 = 2 * (GStd * GStd); /* twice the variance */
  f = 1 / (sqrt(2 * M_PI) * GStd);

  for (n = 0; n < surf->nvertices; n++) {
    nXNbrs = MRIFseq_vox(dist, n, 0, 0, 0); /*1st row is number of ext neighbors*/
    gsum = 0;
    for (m = 0; m < nXNbrs; m++) {
      d = MRIFseq_vox(dist, n, 2, 0, m); /*3rd row is dist to ext neighbors*/
      g = f * exp(-(d * d) / (GVar2));   /* gaussian weight */
      MRIFseq_vox(dist, n, 3, 0, m) = g; /*4th row is weight of  ext neighbors*/
      gsum += g;
    }
    /* Normalize */
    for (m = 0; m < nXNbrs; m++) {
      MRIFseq_vox(dist, n, 3, 0, m) /= gsum;
    }
  }
  return (0);
}
/*-------------------------------------------------------------------
  MRISspatialFilter() - spatially fillters Src by computing the dot
  product of Src and the weights wdist. The wdist MRI struct must
  be as computed by MRISgaussianWeights() and MRISdistSphere(). It is
  assumed that the weights are already normalized.
  -------------------------------------------------------------------*/
MRI *MRISspatialFilter(MRI *Src, MRI *wdist, MRI *Targ)
{
  int n, nXNbrs, m, vtxno, frame;
  float w, val;
  MRI *SrcCopy = NULL;

  if (wdist->width != Src->width) {
    printf("ERROR: MRISspatialFilter: wdist/Src dimension mismatch\n");
    return (NULL);
  }

  /* Make a copy in case this is done in place */
  SrcCopy = MRIcopy(Src, NULL);

  // Set the target to 0
  Targ = MRIconst(Src->width, Src->height, Src->depth, Src->nframes, 0, Targ);
  if (Targ == NULL) {
    printf("ERROR: MRISgaussianSmooth: Targ\n");
    return (NULL);
  }

  /* Loop thru each target vertex */
  for (n = 0; n < Targ->width; n++) {
    nXNbrs = MRIFseq_vox(wdist, n, 0, 0, 0); /*1st row is number of ext neighbors*/
    for (m = 0; m < nXNbrs; m++) {
      vtxno = MRIFseq_vox(wdist, n, 1, 0, m); /*2nd row is the vtxno of ext neighbors*/
      w = MRIFseq_vox(wdist, n, 3, 0, m);     /*4th row is the weight of ext neighbors*/
      for (frame = 0; frame < Targ->nframes; frame++) {
        val = w * MRIFseq_vox(SrcCopy, vtxno, 0, 0, frame);
        MRIFseq_vox(Targ, n, 0, 0, frame) += val;
      } /* frame */
    }   /* neighbor */
  }     /* primary vertex */

  MRIfree(&SrcCopy);
  return (Targ);
}

/*-------------------------------------------------------------------
  MRISdistSphere() - distance between two vertices on the sphere. The
  MRI structure returned is of size (nvertices,4,1,nXNbrsMax). Where
  nXNbrsMax is the maximum number of extended neighbors. The first 3
  rows correspond to: 0=actual number of extended neighbors for that
  vertex, 1=vertex number of extended neighbors, 2=distance along the
  sphere. The last row is free and can be used for computing
  weights. Each of the extended neighbors is placed in a frame. The
  extended neighborhood of a vertex includes itself. See also
  MRISgaussianWeights().
  -------------------------------------------------------------------*/
MRI *MRISdistSphere(MRIS *surf, double dmax)
{
  int vtxno;
  MRI *dist;
  double Radius, Radius2, d, costheta, theta;
  int n, err, *nXNbrs, nXNbrsMax;
  double *XNbrDotProd, DotProdThresh;
  int *XNbrVtxNoTmp, **XNbrVtxNo;
  float **XNbrDist;
  double VertexRadiusStdDev;

  // Create temp variables to hold info that will eventually
  // go into MRI dist
  nXNbrs = (int *)calloc(surf->nvertices, sizeof(int));
  XNbrVtxNo = (int **)calloc(surf->nvertices, sizeof(int *));
  XNbrDist = (float **)calloc(surf->nvertices, sizeof(float *));

  // Compute the average radius of the sphere
  Radius = MRISavgVetexRadius(surf, &VertexRadiusStdDev);
  Radius2 = Radius * Radius;  // Square of the radius
  printf("Radius = %g +/- %g\n", Radius, VertexRadiusStdDev);

  // Compute dot product threshold that corresponds to dmax
  // distance along the surface of the sphere.
  DotProdThresh = Radius2 * cos(dmax / Radius) * (1.0001);

  /* These are needed by MRISextendedNeighbors()*/
  XNbrVtxNoTmp = (int *)calloc(surf->nvertices, sizeof(int));
  XNbrDotProd = (double *)calloc(surf->nvertices, sizeof(double));

  /*-------- Loop through the vertices ------------------*/
  nXNbrsMax = 0;
  for (vtxno = 0; vtxno < surf->nvertices; vtxno++) {
    /* Get a count of the number of extended neighbors (including self)*/
    nXNbrs[vtxno] = 0;
    err = MRISextendedNeighbors(
        surf, vtxno, vtxno, DotProdThresh, XNbrVtxNoTmp, XNbrDotProd, &(nXNbrs[vtxno]), surf->nvertices, 1);

    if (vtxno % 1000 == 0) {
      // print something every 1000 vertices
      printf("vtxno = %d, nXNbrs = %d (Max=%d)\n", vtxno, nXNbrs[vtxno], nXNbrsMax);
      fflush(stdout);
    }

    /*Keep track of max*/
    if (nXNbrsMax < nXNbrs[vtxno]) {
      nXNbrsMax = nXNbrs[vtxno];
    }

    // Alloc enough for this vertex
    XNbrDist[vtxno] = (float *)calloc(nXNbrs[vtxno], sizeof(float));
    XNbrVtxNo[vtxno] = (int *)calloc(nXNbrs[vtxno], sizeof(int));

    // Can just copy vertex numbers
    memmove(XNbrVtxNo[vtxno], XNbrVtxNoTmp, nXNbrs[vtxno] * sizeof(int));

    /* Loop through the extended neighbors */
    for (n = 0; n < nXNbrs[vtxno]; n++) {
      // Compute the cos of the angle betweent the two vertices
      // based on the dot product between the two
      costheta = XNbrDotProd[n] / Radius2;

      // cos theta might be slightly > 1 due to precision
      if (costheta > +1.0) {
        costheta = +1.0;
      }
      if (costheta < -1.0) {
        costheta = -1.0;
      }

      // Compute the angle between the vertices
      theta = acos(costheta);

      /* Compute the distance bet vertices along the surface of the sphere */
      d = Radius * theta;
      XNbrDist[vtxno][n] = d;

    } /* end loop over vertex2 */

  } /* end loop over vertex1 */

  printf("nNbrsMax = %d\n", nXNbrsMax);

  free(XNbrVtxNoTmp);
  free(XNbrDotProd);

  /* Copy info into the dist structure */
  /*row 0=nXNbrs, 1=XNbrVtxNo, 2=XNbrDist, 3 = free */
  dist = MRIallocSequence(surf->nvertices, 4, 1, MRI_FLOAT, nXNbrsMax);

  for (vtxno = 0; vtxno < surf->nvertices; vtxno++) {
    MRIFseq_vox(dist, vtxno, 0, 0, 0) = nXNbrs[vtxno];
    for (n = 0; n < nXNbrs[vtxno]; n++) {
      MRIFseq_vox(dist, vtxno, 1, 0, n) = XNbrVtxNo[vtxno][n];
      MRIFseq_vox(dist, vtxno, 2, 0, n) = XNbrDist[vtxno][n];
    }
    free(XNbrVtxNo[vtxno]);
    free(XNbrDist[vtxno]);
  }
  free(nXNbrs);

  return (dist);
}

///////////////////////////////////////////////////////////
// surfaceRASToSurfaceRAS routines
//
//
//        conformed  ------>   surfaceRAS   (c_(ras) = 0)   surface lives here
//          |                   |
//          |                   |   [ 1  Csrc ]
//          |                   |   [ 0  1    ]
//          V                   V
//         src     ------>     RAS  ( Csrc != 0 )
//          |                   |
//          |                   |
//          V                   V
//         dst    ------->     RAS  ( Ctal  ! = 0)
//          |                   |
//          |                   |   [ 1   - Ctal  ]
//          |                   |   [ 0       1   ]
//          V                   V
//     conformed   ------->  surfaceRAS (c_(ras) = 0 )
//            surface lives here for talairach space
//
MATRIX *surfaceRASToSurfaceRAS_(MRI *src, MRI *dst, LTA *lta)
{
  MATRIX *sRASToRAS = 0;
  MATRIX *RASToSRAS = 0;
  MATRIX *tmp = 0;
  MATRIX *res = 0;
  MATRIX *RASToRAS = 0;

  sRASToRAS = RASFromSurfaceRAS_(src);
  RASToSRAS = surfaceRASFromRAS_(dst);

  if (lta->type == LINEAR_RAS_TO_RAS) {
    tmp = MatrixMultiply(lta->xforms[0].m_L, sRASToRAS, NULL);
    res = MatrixMultiply(RASToSRAS, tmp, NULL);
    MatrixFree(&sRASToRAS);
    MatrixFree(&RASToSRAS);
    MatrixFree(&tmp);

    return res;
  }
  else if (lta->type == LINEAR_VOX_TO_VOX) {
    // just make sure
    if (!src->r_to_i__) {
      src->r_to_i__ = extract_r_to_i(src);
    }

    MATRIX *tmp2 = NULL;
    if (!dst->i_to_r__) {
      tmp2 = extract_i_to_r(dst);
      AffineMatrixAlloc(&(dst->i_to_r__));
      SetAffineMatrix(dst->i_to_r__, tmp);
    }
    else {
      tmp2 = MatrixAlloc(4, 4, MATRIX_REAL);
      GetAffineMatrix(tmp, dst->i_to_r__);
    }

    // create ras_to_ras transform
    tmp = MatrixMultiply(lta->xforms[0].m_L, src->r_to_i__, NULL);
    RASToRAS = MatrixMultiply(tmp2, tmp, NULL);
    tmp = MatrixMultiply(RASToRAS, sRASToRAS, NULL);
    res = MatrixMultiply(RASToSRAS, tmp, NULL);

    MatrixFree(&RASToRAS);
    MatrixFree(&sRASToRAS);
    MatrixFree(&RASToSRAS);
    MatrixFree(&tmp);
    MatrixFree(&tmp2);

    return res;
  }
  else
    ErrorExit(ERROR_BADPARM, "%s: xfm passed is neither of RAS-to-RAS type nor Vox-To-Vox type.", Progname);
  return 0;
}

// transform surface vertices to the dst volume surface
int MRISsurf2surf(MRIS *mris, MRI *dst, LTA *lta)
{
  VECTOR *sX = 0;
  VECTOR *dX = 0;
  MATRIX *surf2surf = 0;
  MRI *src = 0;
  int i;
  int ltaNULL = 0;

  if (lta == NULL) {
    ltaNULL = 1;
    lta = LTAalloc(1, NULL);
    lta->type = LINEAR_RAS_TO_RAS;
    MatrixIdentity(4, lta->xforms[0].m_L);
    getVolGeom(dst, &lta->xforms[0].dst);
    getVolGeom(dst, &lta->inv_xforms[0].src);
    if (mris->vg.valid == 1) {
      copyVolGeom(&mris->vg, &lta->xforms[0].src);
      copyVolGeom(&mris->vg, &lta->inv_xforms[0].dst);
    }
  }
  src = MRIallocHeader(mris->vg.width, mris->vg.height, mris->vg.depth, MRI_VOLUME_TYPE_UNKNOWN, 1);
  if (mris->vg.valid == 0) {
    fprintf(stderr, "INFO: surface does not contain the volume geometry info\n");
    fprintf(stderr, "INFO: surf2surf conversion may be incorrect.\n");
  }
  useVolGeomToMRI(&mris->vg, src);

  sX = VectorAlloc(4, MATRIX_REAL);
  dX = VectorAlloc(4, MATRIX_REAL);
  surf2surf = surfaceRASFromSurfaceRAS_(dst, src, lta);
  // now get all the vertex points and change them to
  //    the corresponding dst surface vertices
  for (i = 0; i < mris->nvertices; i++) {
    V4_LOAD(sX, mris->vertices[i].x, mris->vertices[i].y, mris->vertices[i].z, 1.);
    MatrixMultiply(surf2surf, sX, dX);
    mris->vertices[i].x = VECTOR_ELT(dX, 1);
    mris->vertices[i].y = VECTOR_ELT(dX, 2);
    mris->vertices[i].z = VECTOR_ELT(dX, 3);
  }
  // modify the geometry stored
  getVolGeom(dst, &mris->vg);
  // recalculate properties
  MRIScomputeNormals(mris);

  MRIfree(&src);
  src = 0;
  VectorFree(&sX);
  sX = 0;
  VectorFree(&dX);
  dX = 0;
  MatrixFree(&surf2surf);
  surf2surf = 0;
  if (ltaNULL == 1) {
    LTAfree(&lta);
  }

  return 0;
}
// transform surface vertices positions (all of them) to the dst volume surface
int MRISsurf2surfAll(MRIS *mris, MRI *dst, LTA *lta)
{
  VECTOR *sX = 0;
  VECTOR *dX = 0;
  MATRIX *surf2surf = 0;
  MRI *src = 0;
  int i;
  int ltaNULL = 0;

  if (lta == NULL) {
    ltaNULL = 1;
    lta = LTAalloc(1, NULL);
    lta->type = LINEAR_RAS_TO_RAS;
    MatrixIdentity(4, lta->xforms[0].m_L);
    getVolGeom(dst, &lta->xforms[0].dst);
    getVolGeom(dst, &lta->inv_xforms[0].src);
    if (mris->vg.valid == 1) {
      copyVolGeom(&mris->vg, &lta->xforms[0].src);
      copyVolGeom(&mris->vg, &lta->inv_xforms[0].dst);
    }
  }
  src = MRIallocHeader(mris->vg.width, mris->vg.height, mris->vg.depth, MRI_VOLUME_TYPE_UNKNOWN, 1);
  if (mris->vg.valid == 0) {
    fprintf(stderr, "INFO: surface does not contain the volume geometry info\n");
    fprintf(stderr, "INFO: surf2surf conversion may be incorrect.\n");
  }
  useVolGeomToMRI(&mris->vg, src);
  if (mris->useRealRAS == 0) {
    dst->c_r = dst->c_a = dst->c_s = 0;
  }

  mris->useRealRAS = 1;
  sX = VectorAlloc(4, MATRIX_REAL);
  dX = VectorAlloc(4, MATRIX_REAL);
  surf2surf = surfaceRASFromSurfaceRAS_(dst, src, lta);
  // now get all the vertex points and change them to
  //    the corresponding dst surface vertices
  for (i = 0; i < mris->nvertices; i++) {
    // current
    V4_LOAD(sX, mris->vertices[i].x, mris->vertices[i].y, mris->vertices[i].z, 1.);
    MatrixMultiply(surf2surf, sX, dX);
    mris->vertices[i].x = VECTOR_ELT(dX, 1);
    mris->vertices[i].y = VECTOR_ELT(dX, 2);
    mris->vertices[i].z = VECTOR_ELT(dX, 3);

    // original
    V4_LOAD(sX, mris->vertices[i].origx, mris->vertices[i].origy, mris->vertices[i].origz, 1.);
    MatrixMultiply(surf2surf, sX, dX);
    MRISsetOriginalXYZ(mris, i, VECTOR_ELT(dX, 1), VECTOR_ELT(dX, 2), VECTOR_ELT(dX, 3));

    // white
    V4_LOAD(sX, mris->vertices[i].whitex, mris->vertices[i].whitey, mris->vertices[i].whitez, 1.);
    MatrixMultiply(surf2surf, sX, dX);
    mris->vertices[i].whitex = VECTOR_ELT(dX, 1);
    mris->vertices[i].whitey = VECTOR_ELT(dX, 2);
    mris->vertices[i].whitez = VECTOR_ELT(dX, 3);

    // pial
    V4_LOAD(sX, mris->vertices[i].pialx, mris->vertices[i].pialy, mris->vertices[i].pialz, 1.);
    MatrixMultiply(surf2surf, sX, dX);
    mris->vertices[i].pialx = VECTOR_ELT(dX, 1);
    mris->vertices[i].pialy = VECTOR_ELT(dX, 2);
    mris->vertices[i].pialz = VECTOR_ELT(dX, 3);
  }
  // modify the geometry stored
  getVolGeom(dst, &mris->vg);
  // recalculate properties
  MRIScomputeNormals(mris);

  MRIfree(&src);
  src = 0;
  VectorFree(&sX);
  sX = 0;
  VectorFree(&dX);
  dX = 0;
  MatrixFree(&surf2surf);
  surf2surf = 0;
  if (ltaNULL == 1) {
    LTAfree(&lta);
  }

  return 0;
}

int MRISsmoothFrames(MRI_SURFACE *mris, MRI *mri, int navgs)
{
  int frame;

  for (frame = 0; frame < mri->nframes; frame++) {
    MRISwriteFrameToValues(mris, mri, frame);
    MRISaverageVals(mris, navgs);
    MRISreadFrameFromValues(mris, mri, frame);
  }
  return (NO_ERROR);
}
int MRISwriteFrameToValues(MRI_SURFACE *mris, MRI *mri, int frame)
{
  int vno;
  VERTEX *v;

  if (mri->width != mris->nvertices)
    ErrorReturn(
        ERROR_BADPARM,
        (ERROR_BADPARM, "MRISwriteFrameToValues: mri width %d != mris->nvertices %d", mri->width, mris->nvertices));
  for (vno = 0; vno < mris->nvertices; vno++) {
    v = &mris->vertices[vno];
    if (vno == Gdiag_no) {
      DiagBreak();
    }
    v->val = MRIgetVoxVal(mri, vno, 0, 0, frame);
  }
  return (NO_ERROR);
}
int MRISreadFrameFromValues(MRI_SURFACE *mris, MRI *mri, int frame)
{
  int vno;
  VERTEX *v;

  if (mri->width != mris->nvertices)
    ErrorReturn(
        ERROR_BADPARM,
        (ERROR_BADPARM, "MRISreadFrameToValues: mri width %d != mris->nvertices %d", mri->width, mris->nvertices));
  for (vno = 0; vno < mris->nvertices; vno++) {
    v = &mris->vertices[vno];
    if (vno == Gdiag_no) {
      DiagBreak();
    }
    MRIsetVoxVal(mri, vno, 0, 0, frame, v->val);
  }
  return (NO_ERROR);
}


int MRIScopyVolGeomFromMRI(MRI_SURFACE *mris, MRI *mri)
{
  VOL_GEOM *vg = &mris->vg;

  vg->xsize = mri->xsize;
  vg->ysize = mri->ysize;
  vg->zsize = mri->zsize;
  vg->x_r = mri->x_r;
  vg->y_r = mri->y_r;
  vg->z_r = mri->z_r;
  vg->c_r = mri->c_r;
  vg->x_a = mri->x_a;
  vg->y_a = mri->y_a;
  vg->z_a = mri->z_a;
  vg->c_a = mri->c_a;
  vg->x_s = mri->x_s;
  vg->y_s = mri->y_s;
  vg->z_s = mri->z_s;
  vg->c_s = mri->c_s;
  vg->width = mri->width;
  vg->height = mri->height;
  vg->depth = mri->depth;
  vg->valid = 1;
  strcpy(vg->fname, mri->fname);
  return (NO_ERROR);
}

int MRIScomputeClassStatistics(
    MRI_SURFACE *mris, MRI *mri, float *pwhite_mean, float *pwhite_std, float *pgray_mean, float *pgray_std)
{
  double val, x, y, z, xw, yw, zw;
  int total_vertices, vno;
  VERTEX *v;
  double mean_white, mean_gray, std_white, std_gray, nsigma, gw_thresh;
  FILE *fpwm, *fpgm;

  if (Gdiag & DIAG_WRITE && DIAG_VERBOSE_ON) {
    fpwm = fopen("wm.dat", "w");
    fpgm = fopen("gm.dat", "w");
  }
  else {
    fpgm = fpwm = NULL;
  }

  std_white = std_gray = mean_white = mean_gray = 0.0;
  for (total_vertices = vno = 0; vno < mris->nvertices; vno++) {
    v = &mris->vertices[vno];
    if (v->ripflag) {
      continue;
    }
    total_vertices++;
    if (vno == Gdiag_no) {
      DiagBreak();
    }

    x = v->x + 1.0 * v->nx;
    y = v->y + 1.0 * v->ny;
    z = v->z + 1.0 * v->nz;
    MRISsurfaceRASToVoxelCached(mris, mri, x, y, z, &xw, &yw, &zw);
    MRIsampleVolume(mri, xw, yw, zw, &val);
    if (fpgm) {
      fprintf(fpgm, "%d %2.1f %2.1f %2.1f %f\n", vno, xw, yw, zw, val);
    }
    mean_gray += val;
    std_gray += (val * val);

    x = v->x - 0.5 * v->nx;
    y = v->y - 0.5 * v->ny;
    z = v->z - 0.5 * v->nz;
    MRISsurfaceRASToVoxelCached(mris, mri, x, y, z, &xw, &yw, &zw);
    MRIsampleVolume(mri, xw, yw, zw, &val);
    if (fpwm) {
      fprintf(fpwm, "%d %2.1f %2.1f %2.1f %f\n", vno, xw, yw, zw, val);
    }
    mean_white += val;
    std_white += (val * val);
  }

  *pwhite_mean = mean_white /= (float)total_vertices;
  *pwhite_std = std_white = sqrt(std_white / (float)total_vertices - mean_white * mean_white);
  *pgray_mean = mean_gray /= (float)total_vertices;
  *pgray_std = std_gray = sqrt(std_gray / (float)total_vertices - mean_gray * mean_gray);
  nsigma = (mean_gray - mean_white) / (std_gray + std_white);
  gw_thresh = mean_white + nsigma * std_white;
  printf(
      "white %2.1f +- %2.1f,    "
      "gray %2.1f +- %2.1f, G/W boundary at %2.1f\n",
      mean_white,
      std_white,
      mean_gray,
      std_gray,
      gw_thresh);

  if (fpwm) {
    fclose(fpgm);
    fclose(fpwm);
  }
  return (NO_ERROR);
}

/*
  \fn int MRIScomputeClassModes()
  \brief Computes the modes and stddevs of WM, GM, and CSF.  It goes
  through the non-ripped vertices in the surface and samples the mri
  at 1mm inside the white surface (WHITE_VERTICES) to get WM samples,
  1mm outside the white surface to get GM values, and 0.5mm outside
  the pial surface (PIAL_VERTICES) to get CSF samples. When CSF is
  requested, GM samples are also obtained from 0.5mm inside the
  pial. So the GM stats can change depending upon whether CSF stats
  are or are not being computed (they will not be if pcsf_mode==NULL).
  Once the samples of a class are obtained, they are histogrammed; the
  mode is determined from the peak of the hist, the stddev is
  determined by fitting a Gaussian to the hist.
*/
int MRIScomputeClassModes(MRI_SURFACE *mris,
                          MRI *mri,
                          float *pwhite_mode,
                          float *pgray_mode,
                          float *pcsf_mode,
                          float *pwhite_std,
                          float *pgray_std,
                          float *pcsf_std)
{
  HISTOGRAM *h_white, *h_csf, *h_gray;
  float min_val, max_val, white_std, gray_std, csf_std = 0;
  int nbins, b, vno, gray_peak, white_peak, csf_peak, bin;
  VERTEX *v;
  double val, x, y, z, xw, yw, zw;
  double WM_SAMPLE_DIST = 1.0; //1mm hidden parameter

  MRIvalRange(mri, &min_val, &max_val);
  nbins = ceil(max_val - min_val) + 1;
  h_white = HISTOalloc(nbins);
  h_csf = HISTOalloc(nbins);
  h_gray = HISTOalloc(nbins);

  for (b = 0; b < nbins; b++) {
    h_white->bins[b] = min_val + b;
    h_csf->bins[b] = min_val + b;
    h_gray->bins[b] = min_val + b;
  }
  h_white->min = h_gray->min = h_csf->min = min_val;
  h_white->max = h_gray->max = h_csf->max = max_val;
  h_white->bin_size = h_gray->bin_size = h_csf->bin_size = 1;

  // use g/w boundary to compute gray and white histograms
  MRISsaveVertexPositions(mris, TMP_VERTICES);
  MRISrestoreVertexPositions(mris, WHITE_VERTICES);
  MRIScomputeMetricProperties(mris);
  for (vno = 0; vno < mris->nvertices; vno++) {
    v = &mris->vertices[vno];
    if (v->ripflag) continue;
    if (vno == Gdiag_no) DiagBreak();

    // Project 1mm (WM_SAMPLE_DIST) into white matter
    x = v->x - WM_SAMPLE_DIST * v->nx;
    y = v->y - WM_SAMPLE_DIST * v->ny;
    z = v->z - WM_SAMPLE_DIST * v->nz;
    MRISsurfaceRASToVoxelCached(mris, mri, x, y, z, &xw, &yw, &zw);
    MRIsampleVolume(mri, xw, yw, zw, &val);
    bin = nint(val - min_val);
    if (bin < 0 || bin >= h_white->nbins) {
      DiagBreak();
    }
    h_white->counts[bin]++; // add to the WM bin

    // Project 1mm into gray matter
    x = v->x + 1.0 * v->nx;
    y = v->y + 1.0 * v->ny;
    z = v->z + 1.0 * v->nz;
    MRISsurfaceRASToVoxelCached(mris, mri, x, y, z, &xw, &yw, &zw);
    MRIsampleVolume(mri, xw, yw, zw, &val);
    bin = nint(val - min_val);
    if (bin < 0 || bin >= h_gray->nbins) {
      DiagBreak();
    }
    h_gray->counts[bin]++;  // add to the GM bin
  }

  if (pcsf_mode) {
    MRISrestoreVertexPositions(mris, PIAL_VERTICES);
    MRIScomputeMetricProperties(mris);
    for (vno = 0; vno < mris->nvertices; vno++) {
      v = &mris->vertices[vno];
      if (v->ripflag) {
        continue;
      }
      if (vno == Gdiag_no) {
        DiagBreak();
      }

      // Project 0.5mm into GM (from pial)
      x = v->x - 0.5 * v->nx;
      y = v->y - 0.5 * v->ny;
      z = v->z - 0.5 * v->nz;
      MRISsurfaceRASToVoxelCached(mris, mri, x, y, z, &xw, &yw, &zw);
      if (MRIindexNotInVolume(mri, xw, yw, zw) == 0) {
        MRIsampleVolume(mri, xw, yw, zw, &val);
        bin = nint(val - min_val);
        if (bin < 0 || bin >= h_gray->nbins) {
          DiagBreak();
        }
        h_gray->counts[bin]++; // add to the GM bin
      }

      // Project 0.5mm into extrcerebral CSF (from pial)
      x = v->x + 0.5 * v->nx;
      y = v->y + 0.5 * v->ny;
      z = v->z + 0.5 * v->nz;
      MRISsurfaceRASToVoxelCached(mris, mri, x, y, z, &xw, &yw, &zw);
      if (MRIindexNotInVolume(mri, xw, yw, zw) == 0) {
        MRIsampleVolume(mri, xw, yw, zw, &val);
        bin = nint(val - min_val);
        if (bin < 0 || bin >= h_csf->nbins) {
          DiagBreak();
        }
        if (bin == 68) {
          DiagBreak();
        }
        h_csf->counts[bin]++;
      }
    }
    // Compute the mode of the CSF as the peak and the stddev based
    // on the shape of the histo (Gaussian model)
    HISTOclearZeroBin(h_csf);
    csf_peak = HISTOfindHighestPeakInRegion(h_csf, 0, h_csf->nbins);
    *pcsf_mode = h_csf->bins[csf_peak];
    if (Gdiag & DIAG_WRITE && DIAG_VERBOSE_ON) {
      HISTOplot(h_csf, "csf.plt");
    }
    csf_std = HISTOcomputeFWHM(h_csf, csf_peak) / 2.3;
    if (pcsf_std) *pcsf_std = csf_std;
  }

  HISTOclearZeroBin(h_white);
  HISTOclearZeroBin(h_gray);
  white_peak = HISTOfindHighestPeakInRegion(h_white, 0, h_white->nbins);
  gray_peak = HISTOfindHighestPeakInRegion(h_gray, 0, h_gray->nbins);
  *pwhite_mode = h_white->bins[white_peak];
  *pgray_mode = h_gray->bins[gray_peak];
  white_std = HISTOcomputeFWHM(h_white, white_peak) / 2.3;
  gray_std = HISTOcomputeFWHM(h_gray, gray_peak) / 2.3;

  if (pwhite_std) *pwhite_std = white_std;
  if (pgray_std) *pgray_std = gray_std;
  if (pcsf_mode)
    printf("intensity peaks found at WM=%d+-%2.1f,   GM=%d+-%2.1f,  CSF=%d+-%2.1f\n",
           nint(*pwhite_mode),
           white_std,
           nint(*pgray_mode),
           gray_std,
           nint(*pcsf_mode),
           csf_std);
  else
    printf("intensity peaks found at WM=%d+-%2.1f,    GM=%d+-%2.1f\n",
           nint(*pwhite_mode),
           white_std,
           nint(*pgray_mode),
           gray_std);
  if (Gdiag & DIAG_WRITE && DIAG_VERBOSE_ON) {
    HISTOplot(h_white, "wm.plt");
    HISTOplot(h_gray, "gm.plt");
  }
  HISTOfree(&h_white);
  HISTOfree(&h_csf);
  HISTOfree(&h_gray);

  // back to initial state
  MRISrestoreVertexPositions(mris, TMP_VERTICES);
  MRIScomputeMetricProperties(mris);
  return (NO_ERROR);
}

int MRISrasToVoxel(MRI_SURFACE *mris, MRI *mri, double xs, double ys, double zs, double *pxv, double *pyv, double *pzv)
{
  return (MRISsurfaceRASToVoxelCached(mris, mri, xs, ys, zs, pxv, pyv, pzv));
}

int MRISvertexNormalInVoxelCoords(MRI_SURFACE *mris, MRI *mri, int vno, double *pnx, double *pny, double *pnz)
{
  double xv0, yv0, zv0, xv1, yv1, zv1, xw, yw, zw, norm, nx, ny, nz;
  VERTEX *v;

  v = &mris->vertices[vno];
  MRISvertexToVoxel(mris, v, mri, &xv0, &yv0, &zv0);

  xw = v->x + v->nx;
  yw = v->y + v->ny;
  zw = v->z + v->nz;
  MRISsurfaceRASToVoxelCached(mris, mri, xw, yw, zw, &xv1, &yv1, &zv1);

  nx = xv1 - xv0;
  ny = yv1 - yv0;
  nz = zv1 - zv0;
  norm = sqrt(nx * nx + ny * ny + nz * nz);
  if (!FZERO(norm)) {
    nx /= norm;
    ny /= norm;
    nz /= norm;
  }
  *pnx = nx;
  *pny = ny;
  *pnz = nz;

  return (NO_ERROR);
}
#include "diag.h"
int MRISmakeDensityMap(MRI_SURFACE *mris, double resolution, double radius, int diag_no, MRI **pmri)
{
  MRI *mri_interior;
  int x, y, z, vno, num, vradius, xmin, xmax, ymin, ymax, zmin, zmax;
  VERTEX *v;
  double dist, dx, dy, dz, sphere_volume;
  double xf, yf, zf;

  mri_interior = MRISfillInterior(mris, resolution, NULL);
  if (Gdiag & DIAG_WRITE && DIAG_VERBOSE_ON) {
    MRIwrite(mri_interior, "int.mgz");
  }

  if (diag_no >= 0) {
    *pmri = MRIclone(mri_interior, NULL);
  }

  vradius = radius / (MIN(MIN(mri_interior->xsize, mri_interior->ysize), mri_interior->zsize));
  sphere_volume = vradius * vradius * vradius * M_PI * 4 / 3;
  for (vno = 0; vno < mris->nvertices; vno++) {
    v = &mris->vertices[vno];
    if (vno == Gdiag_no) {
      DiagBreak();
    }
    if (v->ripflag) {
      continue;
    }
    MRISvertexToVoxel(mris, v, mri_interior, &xf, &yf, &zf);
    xmin = floor(xf - vradius);
    xmax = ceil(xf + vradius);
    ymin = floor(yf - vradius);
    ymax = ceil(yf + vradius);
    zmin = floor(zf - vradius);
    zmax = ceil(zf + vradius);
    for (num = 0, x = xmin; x <= xmax; x++) {
      if (x < 0 || x >= mri_interior->width) {
        continue;
      }
      for (y = ymin; y <= ymax; y++) {
        if (y < 0 || y >= mri_interior->height) {
          continue;
        }
        for (z = zmin; z <= zmax; z++) {
          if (x == Gx && y == Gy && z == Gz) {
            DiagBreak();
          }
          if (z < 0 || z >= mri_interior->depth) {
            continue;
          }
          dx = x - xf;
          dy = y - yf;
          dz = z - zf;
          dist = sqrt(dx * dx + dy * dy + dz * dz);
          if (dist > vradius) {
            continue;
          }
          if (MRIgetVoxVal(mri_interior, x, y, z, 0) > 0) {
            num++;
            if (vno == Gdiag_no) {
              MRIsetVoxVal(*pmri, x, y, z, 0, 1.0);
            }
          }
        }
      }
    }
    v->curv = (double)num / sphere_volume;
  }

  return (NO_ERROR);
}

/*!
  \fn double MRIScomputeWhiteVolume(MRI_SURFACE *mris, MRI *mri_aseg, double resolution)
  \brief Computes surface-based white matter volume, excluding subcort
*/
double MRIScomputeWhiteVolume(MRI_SURFACE *mris, MRI *mri_aseg, double resolution)
{
  MRI *mri_filled;
  MATRIX *m_vox2vox;
  double total_volume = 0.0, vox_volume;
  int x, y, z, label, xa, ya, za;
  VECTOR *v1, *v2;
  double val;

  // Create a volume with everything inside the surface set to 1
  mri_filled = MRISfillInterior(mris, resolution, NULL);
  if (Gdiag & DIAG_WRITE && DIAG_VERBOSE_ON) {
    MRIwrite(mri_filled, "f.mgz");
  }
  m_vox2vox = MRIgetVoxelToVoxelXform(mri_filled, mri_aseg);
  v1 = VectorAlloc(4, MATRIX_REAL);
  v2 = VectorAlloc(4, MATRIX_REAL);
  VECTOR_ELT(v1, 4) = 1.0;
  VECTOR_ELT(v2, 4) = 1.0;
  vox_volume = mri_filled->xsize * mri_filled->ysize * mri_filled->zsize;

  for (x = 0; x < mri_filled->width; x++) {
    V3_X(v1) = x;
    for (y = 0; y < mri_filled->height; y++) {
      V3_Y(v1) = y;
      for (z = 0; z < mri_filled->depth; z++) {
        // Exclude voxel if not inside the surface
        val = MRIgetVoxVal(mri_filled, x, y, z, 0);
        if (FZERO(val)) {
          continue;
        }

        if (x == Gx && y == Gy && z == Gz) {
          DiagBreak();
        }
        V3_Z(v1) = z;
        MatrixMultiply(m_vox2vox, v1, v2);
        xa = nint(V3_X(v2));
        ya = nint(V3_Y(v2));
        za = nint(V3_Z(v2));
        if (xa < 0 || xa >= mri_aseg->width || ya < 0 || ya >= mri_aseg->height || za < 0 || za >= mri_aseg->depth) {
          ErrorPrintf(ERROR_BADPARM,
                      "MRIScomputeWhiteVolume: src (%d, %d, %d) maps to (%d, %d, %d) - OOB",
                      x,
                      y,
                      z,
                      xa,
                      ya,
                      za);
          continue;
        }
        label = (int)MRIgetVoxVal(mri_aseg, xa, ya, za, 0);
        if (xa == Gx && ya == Gy && za == Gz) {
          DiagBreak();
        }
        switch (label) {
          // Note: {Left,Right}_Cerebral_Cortex are here to catch voxels on the edge
          case Left_Cerebral_Cortex:
          case Right_Cerebral_Cortex:
          case Left_Cerebral_White_Matter:
          case Right_Cerebral_White_Matter:
          case Left_WM_hypointensities:
          case Right_WM_hypointensities:
          case CC_Posterior:
          case CC_Mid_Posterior:
          case CC_Central:
          case CC_Mid_Anterior:
          case CC_Anterior:
            total_volume += vox_volume;
            break;
        }
      }
    }
  }

  MatrixFree(&m_vox2vox);
  MatrixFree(&v1);
  MatrixFree(&v2);
  MRIfree(&mri_filled);
  return (total_volume);
}
MRI *MRISfillWhiteMatterInterior(
    MRI_SURFACE *mris, MRI *mri_aseg, MRI *mri_filled, double resolution, int wm_val, int gm_val, int csf_val)
{
  MATRIX *m_vox2vox;
  double vox_volume;
  int x, y, z, label, xa, ya, za;
  VECTOR *v1, *v2;
  double val;

  mri_filled = MRISfillInterior(mris, resolution, mri_filled);
  MRIbinarize(mri_filled, mri_filled, 1, 0, wm_val);

  // This cras adjustment is now done in MRISfillInterior() DNG 7/8/08
  // mri_filled->c_r += mri_aseg->c_r ;
  // mri_filled->c_a += mri_aseg->c_a ;
  // mri_filled->c_s += mri_aseg->c_s ;
  if (Gdiag & DIAG_WRITE && DIAG_VERBOSE_ON) {
    MRIwrite(mri_filled, "f.mgz");
  }
  m_vox2vox = MRIgetVoxelToVoxelXform(mri_filled, mri_aseg);
  v1 = VectorAlloc(4, MATRIX_REAL);
  v2 = VectorAlloc(4, MATRIX_REAL);
  VECTOR_ELT(v1, 4) = 1.0;
  VECTOR_ELT(v2, 4) = 1.0;
  vox_volume = mri_filled->xsize * mri_filled->ysize * mri_filled->zsize;

  for (x = 0; x < mri_filled->width; x++) {
    V3_X(v1) = x;
    for (y = 0; y < mri_filled->height; y++) {
      V3_Y(v1) = y;
      for (z = 0; z < mri_filled->depth; z++) {
        val = MRIgetVoxVal(mri_filled, x, y, z, 0);
        if (FZERO(val)) {
          continue;
        }
        if (x == Gx && y == Gy && z == Gz) {
          DiagBreak();
        }
        V3_Z(v1) = z;
        MatrixMultiply(m_vox2vox, v1, v2);
        xa = nint(V3_X(v2));
        ya = nint(V3_Y(v2));
        za = nint(V3_Z(v2));
        if (xa < 0 || xa >= mri_aseg->width || ya < 0 || ya >= mri_aseg->height || za < 0 || za >= mri_aseg->depth) {
          ErrorPrintf(ERROR_BADPARM,
                      "MRIScomputeWhiteVolume: src (%d, %d, %d) maps to (%d, %d, %d) - OOB",
                      x,
                      y,
                      z,
                      xa,
                      ya,
                      za);
          continue;
        }
        label = (int)MRIgetVoxVal(mri_aseg, xa, ya, za, 0);
        if (xa == Gx && ya == Gy && za == Gz) {
          DiagBreak();
        }
        switch (label) {
          case Left_choroid_plexus:
          case Right_choroid_plexus:
          case Right_Lateral_Ventricle:
          case Left_Lateral_Ventricle:
          case Right_Inf_Lat_Vent:
          case Left_Inf_Lat_Vent:
            MRIsetVoxVal(mri_filled, x, y, z, 0, csf_val);  // ventricle
            break;
          case Left_Cerebral_Cortex:
          case Right_Cerebral_Cortex:
          case Left_Cerebral_White_Matter:
          case Right_Cerebral_White_Matter:
          case Left_WM_hypointensities:
          case Right_WM_hypointensities:
          case CC_Posterior:
          case CC_Mid_Posterior:
          case CC_Central:
          case CC_Mid_Anterior:
          case CC_Anterior:
          case Brain_Stem:
            MRIsetVoxVal(mri_filled, x, y, z, 0, wm_val);  // white matter
            break;
          case Left_Hippocampus:
          case Right_Hippocampus:
          case Left_Amygdala:
          case Right_Amygdala:
          case Left_Putamen:
          case Left_Pallidum:
          case Right_Putamen:
          case Right_Pallidum:
          case Left_Caudate:
          case Right_Caudate:
          case Left_Thalamus:
          case Right_Thalamus:
          case Left_Accumbens_area:
          case Right_Accumbens_area:
            MRIsetVoxVal(mri_filled, x, y, z, 0, gm_val);  // gray matter
            break;
        }
      }
    }
  }

  MatrixFree(&m_vox2vox);
  MatrixFree(&v1);
  MatrixFree(&v2);
  return (mri_filled);
}


// sets the RAS for a surface
// FUNDAMENTAL ASSUMPTION - all the vertices are created in voxel coordinates associated with srcMri
void MRISsetVolumeForSurface(MRI_SURFACE *mris, MRI *srcMri)
{
  MRIScopyVolGeomFromMRI(mris, srcMri);
  MATRIX *matrix = surfaceRASFromVoxel_(srcMri);
  MRISmatrixMultiply(mris, matrix);
  MatrixFree(&matrix);
}

MRI *MRIScomputeDistanceToSurface(MRI_SURFACE *mris, MRI *mri_dist, float resolution)
{
  MRI *mri_tmp, *mri_mask;

  if (mri_dist != NULL) {
    mri_tmp = MRIclone(mri_dist, NULL);
  }
  else {
    mri_tmp = NULL;  // will get allocated by MRISfillInterior
  }
  mri_tmp = MRISfillInterior(mris, resolution, mri_tmp);

#define PAD 10
  if (mri_dist == NULL) {
    mri_mask = MRIextractRegionAndPad(mri_tmp, NULL, NULL, PAD);
  }
  else {
    mri_mask = MRIcopy(mri_tmp, NULL);  // geometry specified by caller
  }
  mri_dist = MRIdistanceTransform(mri_mask, mri_dist, 1, nint(PAD / mri_mask->xsize), DTRANS_MODE_SIGNED, NULL);

  MRIfree(&mri_tmp);
  MRIfree(&mri_mask);
  return (mri_dist);
}


int MRISsurfaceRASToVoxel(MRI_SURFACE *mris, MRI *mri, double r, double a, double s, double *px, double *py, double *pz)
{
  MATRIX *m_sras2vox = NULL;
  MATRIX *m_sras2ras, *m_ras2vox;
  MRI *mri_tmp;
  static VECTOR *v1[_MAX_FS_THREADS] = {NULL};
  static VECTOR *v2[_MAX_FS_THREADS] = {NULL};

#ifdef HAVE_OPENMP
  // thread ID
  int tid = omp_get_thread_num();
#else
  int tid = 0;
#endif

  // this speeds things up because the alloc is only done once.
  if (v1[tid] == NULL) {
    v1[tid] = VectorAlloc(4, MATRIX_REAL);
    v2[tid] = VectorAlloc(4, MATRIX_REAL);
    VECTOR_ELT(v1[tid], 4) = 1.0;
    VECTOR_ELT(v2[tid], 4) = 1.0;
  }

  if (mris->vg.valid) {
    mri_tmp = MRIallocHeader(mris->vg.width, mris->vg.height, mris->vg.depth, MRI_UCHAR, 1);
    MRIcopyVolGeomToMRI(mri_tmp, &mris->vg);
    m_sras2ras = RASFromSurfaceRAS_(mri_tmp);
    MRIfree(&mri_tmp);
  }
  else  // no valid geom - assume it came from provided volume
  {
    m_sras2ras = RASFromSurfaceRAS_(mri);
  }

  m_ras2vox = MRIgetRasToVoxelXform(mri);
  m_sras2vox = MatrixMultiply(m_ras2vox, m_sras2ras, NULL);

  V3_X(v1[tid]) = r;
  V3_Y(v1[tid]) = a;
  V3_Z(v1[tid]) = s;
  MatrixMultiply(m_sras2vox, v1[tid], v2[tid]);
  *px = V3_X(v2[tid]);
  *py = V3_Y(v2[tid]);
  *pz = V3_Z(v2[tid]);
  MatrixFree(&m_ras2vox);
  MatrixFree(&m_sras2ras);
  MatrixFree(&m_sras2vox);

  return (NO_ERROR);
}

void MRIS_loadRAS2VoxelMap(MRIS_SurfRAS2VoxelMap* cache, MRI const * const mri, MRI_SURFACE const * const mris)
{
    if (cache->mri) {
        if (MRIcompareHeaders(cache->mri, mri) == 0) return;    // already loaded
        MRIS_unloadRAS2VoxelMap(cache);                            // unload the wrong stuff       
    }
    cache->mri = MRIcopyHeader(mri, NULL);

    // compute surface ras to vox transform
    //
    {
        // Get surface ras to scanner ras
        //
        MATRIX* surfRas2scannerRas;
        if (mris->vg.valid) {
            // Use VOL_GEOM struct (MRI) that is internal to MRIS if valid
            // (this is usually that of orig.mgz) Good for freeview, but for
            // this to work, the passed mri must share a scanner RAS with mris->vg.
            MRI* mri_tmp = MRIallocHeader(mris->vg.width, mris->vg.height, mris->vg.depth, MRI_UCHAR, 1);
            MRIcopyVolGeomToMRI(mri_tmp, &mris->vg);
            surfRas2scannerRas = RASFromSurfaceRAS_(mri_tmp);
            MRIfree(&mri_tmp);
        } else {
            // Use geometry from MRI struct passed with function
            // Function should reduce to inv(Vox2TkRegRAS)*SurfRAS
            surfRas2scannerRas = RASFromSurfaceRAS_(mri);
        }
        // Scanner RAS to Vox for passed MRI
        MATRIX* scannerRas2vox = MRIgetRasToVoxelXform(mri);
        // SurfRAS2Vox = ScanRAS-To-Vox * SurfRAS-To-ScanRAS
        cache->sras2vox = MatrixMultiply(scannerRas2vox, surfRas2scannerRas, NULL);
        MatrixFree(&scannerRas2vox);
        MatrixFree(&surfRas2scannerRas);
    }
}

void MRIS_unloadRAS2VoxelMap(MRIS_SurfRAS2VoxelMap* map)
{
    MRIfree(&map->mri);
    MatrixFree(&map->sras2vox); // was missing, caused mem leak 7/31/2018
}

MRIS_SurfRAS2VoxelMap* MRIS_makeRAS2VoxelMap(MRI const * const mri, MRI_SURFACE const * const mris)
{
    MRIS_SurfRAS2VoxelMap* map = (MRIS_SurfRAS2VoxelMap*)calloc(1, sizeof(MRIS_SurfRAS2VoxelMap));
    MRIS_loadRAS2VoxelMap(map, mri, mris);
    return map;   
}

void MRIS_freeRAS2VoxelMap(MRIS_SurfRAS2VoxelMap** const mapPtr)
{
    MRIS_SurfRAS2VoxelMap* map = *mapPtr; *mapPtr = NULL;
    if (!map) return;
    MRIS_unloadRAS2VoxelMap(map);
    int i;
    for (i = 0; i < _MAX_FS_THREADS; i++) {
        VECTOR* v2 = map->v2[i]; if (v2) VectorFree(&v2);
        VECTOR* v1 = map->v1[i]; if (v1) VectorFree(&v1);
    }
    free(map);
}

void MRIS_useRAS2VoxelMap(
    MRIS_SurfRAS2VoxelMap * map_nonconst, MRI const * const mri,
    double r, double a, double s, double *px, double *py, double *pz)
{
    const MRIS_SurfRAS2VoxelMap * map = map_nonconst;
    
    int const tid =   // thread ID
#ifdef HAVE_OPENMP
        omp_get_thread_num();
#else
        0;
#endif

    // Get some temps
    //
    VECTOR* v1 = map_nonconst->v1[tid];
    VECTOR* v2 = map_nonconst->v2[tid];
    if (v1 == NULL) // avoid the lock if possible 
#ifdef HAVE_OPENMP
    #pragma omp critical
#endif
    {
        v1 = map_nonconst->v1[tid];
        v2 = map_nonconst->v2[tid];
        if (v1 == NULL) 
        {   // order is important since v1 is tested outside the critical code
            map_nonconst->v2[tid] = v2 = VectorAlloc(4, MATRIX_REAL);  VECTOR_ELT(v2, 4) = 1.0;
            map_nonconst->v1[tid] = v1 = VectorAlloc(4, MATRIX_REAL);  VECTOR_ELT(v1, 4) = 1.0;
        }
    }

    // Note - use of general matrices to do a 3x3 operation is incredibly inefficient
    //        if this is hot, it is easily replaced!
    //
    V3_X(v1) = r;
    V3_Y(v1) = a;
    V3_Z(v1) = s;
    MatrixMultiply(map->sras2vox, v1, v2);
    *px = V3_X(v2);
    *py = V3_Y(v2);
    *pz = V3_Z(v2);
}


static int MRISsurfaceRASToVoxelCached_old(
    MRI_SURFACE *mris, MRI *mri, double r, double a, double s, double *px, double *py, double *pz);
    

/*!
  \fn int MRISsurfaceRASToVoxelCached(MRI_SURFACE *mris, MRI *mri,
        double r, double a, double s,
        double *px, double *py, double *pz)
  \brief Computes voxel coordinates of a given surface RAS.
   Notes: The passed mri must share  a scanner RAS with mris->vg.
          SurfaceRAS is the same as tkRegRAS
  \param mris - surface (only used to get MRI struct)
  \param mri - defines target voxel space
  \param r, a, s - surface coordinates
  \param px, py, pz - pointers to col, rowl, and slice in mri (output)
  Note: keeps a copy of the mri header and sras2vox. When an MRI with 
  a different geometry is passed, if frees the MRI header and sras2vox,
  and copies the new one in. This can make it inefficient if switching
  back and forth between different MRIs (eg, mris_make_surfaces)
*/
int MRISsurfaceRASToVoxelCached(
    MRI_SURFACE *mris, MRI *mri, double r, double a, double s, double *px, double *py, double *pz)
{
    static MRIS_SurfRAS2VoxelMap map;
    MRIS_loadRAS2VoxelMap(&map, mri, mris);

    MRIS_useRAS2VoxelMap(&map, mri, r,a,s, px, py, pz);

    if (0) {  
        double old_px, old_py, old_pz;
        int result = MRISsurfaceRASToVoxelCached_old(mris, mri, r, a, s, &old_px, &old_py, &old_pz);
  
        if (result != NO_ERROR || *px != old_px || *py != old_py || *pz != old_pz) {
            fprintf(stderr, "%s:%d MRIS_useRAS2VoxelMap gets wrong answer\n", __FILE__, __LINE__);
            exit(1);
        }
    }
    
    return (NO_ERROR);
}


// This is the old code, kept to be compared to the new to verify the new is correct
//
static int MRISsurfaceRASToVoxelCached_old(
    MRI_SURFACE *mris, MRI *mri, double r, double a, double s, double *px, double *py, double *pz)
{
  static VECTOR *v1[_MAX_FS_THREADS] = {NULL};
  static VECTOR *v2[_MAX_FS_THREADS] = {NULL};

#ifdef HAVE_OPENMP
  // thread ID
  int tid = omp_get_thread_num();
#else
  int tid = 0;
#endif

  // this speeds things up because the alloc is only done once.
  if (v1[tid] == NULL) {
    v1[tid] = VectorAlloc(4, MATRIX_REAL);
    v2[tid] = VectorAlloc(4, MATRIX_REAL);
    VECTOR_ELT(v1[tid], 4) = 1.0;
    VECTOR_ELT(v2[tid], 4) = 1.0;
  }

  // a different volume then previously used
  if (MRIcompareHeaders(mris->mri_sras2vox, mri)) {
    if (mris->m_sras2vox) {
      MatrixFree(&mris->m_sras2vox);  // free it so it will be recomputed
    }
    if (mris->mri_sras2vox) {
      MRIfree(&mris->mri_sras2vox);
    }
    // Header of MRI whose voxel coordinates are going to be computed
    mris->mri_sras2vox = MRIcopyHeader(mri, NULL);
  }

  // recompute surface ras to vox transform
  if (mris->m_sras2vox == NULL) {
    // Get surface ras to scanner ras
    MRI *mri_tmp;
    MATRIX *m_sras2ras, *m_ras2vox;
    if (mris->vg.valid) {
      // Use VOL_GEOM struct (MRI) that is internal to MRIS if valid
      // (this is usually that of orig.mgz) Good for freeview, but for
      // this to work, the passed mri must share a scanner RAS with
      // mris->vg.
      mri_tmp = MRIallocHeader(mris->vg.width, mris->vg.height, mris->vg.depth, MRI_UCHAR, 1);
      MRIcopyVolGeomToMRI(mri_tmp, &mris->vg);
      m_sras2ras = RASFromSurfaceRAS_(mri_tmp);
      MRIfree(&mri_tmp);
    }
    else {
      // Use geometry from MRI struct passed with function
      // Function should reduce to inv(Vox2TkRegRAS)*SurfRAS
      m_sras2ras = RASFromSurfaceRAS_(mri);
    }
    // Scanner RAS to Vox for passed MRI
    m_ras2vox = MRIgetRasToVoxelXform(mri);
    // SurfRAS2Vox = ScanRAS-To-Vox * SurfRAS-To-ScanRAS
    mris->m_sras2vox = MatrixMultiply(m_ras2vox, m_sras2ras, NULL);
    MatrixFree(&m_sras2ras);
    MatrixFree(&m_ras2vox);
  }

  V3_X(v1[tid]) = r;
  V3_Y(v1[tid]) = a;
  V3_Z(v1[tid]) = s;
  MatrixMultiply(mris->m_sras2vox, v1[tid], v2[tid]);
  *px = V3_X(v2[tid]);
  *py = V3_Y(v2[tid]);
  *pz = V3_Z(v2[tid]);

  return (NO_ERROR);
}

int MRISsurfaceRASFromVoxel(
    MRI_SURFACE *mris, MRI *mri, double x, double y, double z, double *pr, double *pa, double *ps)
{
  MATRIX *m_sras2vox = NULL;
  MATRIX *m_vox2sras = NULL;
  MATRIX *m_sras2ras, *m_ras2vox;
  MRI *mri_tmp;
  static VECTOR *v1[_MAX_FS_THREADS] = {NULL};
  static VECTOR *v2[_MAX_FS_THREADS] = {NULL};

#ifdef HAVE_OPENMP
  // thread ID
  int tid = omp_get_thread_num();
#else
  int tid = 0;
#endif

  // this speeds things up because the alloc is only done once.
  if (v1[tid] == NULL) {
    v1[tid] = VectorAlloc(4, MATRIX_REAL);
    v2[tid] = VectorAlloc(4, MATRIX_REAL);
    VECTOR_ELT(v1[tid], 4) = 1.0;
    VECTOR_ELT(v2[tid], 4) = 1.0;
  }

  if (mris->vg.valid) {
    mri_tmp = MRIallocHeader(mris->vg.width, mris->vg.height, mris->vg.depth, MRI_UCHAR, 1);
    MRIcopyVolGeomToMRI(mri_tmp, &mris->vg);
    m_sras2ras = RASFromSurfaceRAS_(mri_tmp);
    MRIfree(&mri_tmp);
  }
  else  // no valid geom - assume it came from provided volume
  {
    m_sras2ras = RASFromSurfaceRAS_(mri);
  }

  m_ras2vox = MRIgetRasToVoxelXform(mri);
  m_sras2vox = MatrixMultiply(m_ras2vox, m_sras2ras, NULL);
  m_vox2sras = MatrixInverse(m_sras2vox, NULL);

  V3_X(v1[tid]) = x;
  V3_Y(v1[tid]) = y;
  V3_Z(v1[tid]) = z;
  MatrixMultiply(m_vox2sras, v1[tid], v2[tid]);
  *pr = V3_X(v2[tid]);
  *pa = V3_Y(v2[tid]);
  *ps = V3_Z(v2[tid]);
  MatrixFree(&m_ras2vox);
  MatrixFree(&m_sras2ras);
  MatrixFree(&m_sras2vox);
  MatrixFree(&m_vox2sras);

  return (NO_ERROR);
}

int MRISsurfaceRASFromVoxelCached(
    MRI_SURFACE *mris, MRI *mri, double x, double y, double z, double *pr, double *pa, double *ps)
{
  static MATRIX *m_vox2sras[_MAX_FS_THREADS] = {NULL};
  static VECTOR *v1[_MAX_FS_THREADS] = {NULL};
  static VECTOR *v2[_MAX_FS_THREADS] = {NULL};

#ifdef HAVE_OPENMP
  // thread ID
  int tid = omp_get_thread_num();
#else
  int tid = 0;
#endif

  if (m_vox2sras[tid] == NULL) {
    MRI *mri_tmp;
    MATRIX *m_sras2ras, *m_ras2vox;
    MATRIX *m_sras2vox = NULL;

    if (mris->vg.valid) {
      mri_tmp = MRIallocHeader(mris->vg.width, mris->vg.height, mris->vg.depth, MRI_UCHAR, 1);
      MRIcopyVolGeomToMRI(mri_tmp, &mris->vg);
      m_sras2ras = RASFromSurfaceRAS_(mri_tmp);
      MRIfree(&mri_tmp);
    }
    else {
      m_sras2ras = RASFromSurfaceRAS_(mri);
    }

    m_ras2vox = MRIgetRasToVoxelXform(mri);
    m_sras2vox = MatrixMultiply(m_ras2vox, m_sras2ras, NULL);
    m_vox2sras[tid] = MatrixInverse(m_sras2vox, NULL);
    v1[tid] = VectorAlloc(4, MATRIX_REAL);
    v2[tid] = VectorAlloc(4, MATRIX_REAL);
    VECTOR_ELT(v1[tid], 4) = 1.0;
    VECTOR_ELT(v2[tid], 4) = 1.0;
    MatrixFree(&m_sras2vox);
    MatrixFree(&m_ras2vox);
    MatrixFree(&m_sras2ras);
  }

  V3_X(v1[tid]) = x;
  V3_Y(v1[tid]) = y;
  V3_Z(v1[tid]) = z;
  MatrixMultiply(m_vox2sras[tid], v1[tid], v2[tid]);
  *pr = V3_X(v2[tid]);
  *pa = V3_Y(v2[tid]);
  *ps = V3_Z(v2[tid]);

  return (NO_ERROR);
}

MRI *MRISlaplacian(MRI_SURFACE *mris, MRI *mri_cmatrix, double inner_width, double outer_width)
{
  int vno, vno2, i, num, n;

  MRI *mri_laplacian;
  double val0, val1, rms;

  mri_laplacian = MRIalloc(mris->nvertices, 1, 1, MRI_FLOAT);
  if (mri_laplacian == NULL) {
    ErrorExit(ERROR_NOMEMORY, "MRISlaplacian: could not allocate laplacian struct");
  }

  for (vno = 0; vno < mris->nvertices; vno++) {
    VERTEX_TOPOLOGY const * const vt = &mris->vertices_topology[vno];
    if (vno == Gdiag_no) {
      DiagBreak();
    }

    rms = 0.0;
    num = 0;
    for (n = 0; n < vt->vtotal; n++) {
      vno2 = vt->v[n];
      for (i = 0; i < mri_cmatrix->nframes; i++) {
        val0 = MRIgetVoxVal(mri_cmatrix, vno, 0, 0, i);
        val1 = MRIgetVoxVal(mri_cmatrix, vno2, 0, 0, i);
        rms += (val0 - val1) * (val0 - val1);
      }
    }
    rms = sqrt(rms / (vt->vtotal * mri_cmatrix->nframes));
    MRIsetVoxVal(mri_laplacian, vno, 0, 0, 0, rms);
  }

  return (mri_laplacian);
}
double MRISsampleValue(MRI_SURFACE *mris, FACE *f, double xp, double yp, double zp, int which, MRI *mri_vals)
{
  double val, x[VERTICES_PER_FACE], y[VERTICES_PER_FACE], dx1, dy1, dx2, dy2, e1x, e1y, e1z, e2x, e2y, e2z, x0, y0, a0,
      a1, a2, val0, val1, val2;
  int n;
  VERTEX *v;

  e1x = e2x = e1y = e2y = e1z = e2z = 0.0;
  for (n = 0; n < VERTICES_PER_FACE; n++) {
    v = &mris->vertices[f->v[n]];
    e1x += v->e1x;
    e1y += v->e1y;
    e1z += v->e1z;
    e2x += v->e2x;
    e2y += v->e2y;
    e2z += v->e2z;
  }

  for (n = 0; n < VERTICES_PER_FACE; n++) {
    v = &mris->vertices[f->v[n]];
    x[n] = v->x * e1x + v->y * e1y + v->z * e1z;
    y[n] = v->x * e2x + v->y * e2y + v->z * e2z;
  }
  x0 = xp * e1x + yp * e1y + zp * e1z;
  y0 = xp * e2x + yp * e2y + zp * e2z;

  dx1 = x[1] - x0;
  dy1 = y[1] - y0;
  dx2 = x[2] - x0;
  dy2 = y[2] - y0;
  a0 = 0.5 * fabs(dx1 * dy2 - dx1 * dx2);

  dx1 = x[0] - x0;
  dy1 = y[0] - y0;
  dx2 = x[2] - x0;
  dy2 = y[2] - y0;
  a1 = 0.5 * fabs(dx1 * dy2 - dx1 * dx2);

  dx1 = x[0] - x0;
  dy1 = y[0] - y0;
  dx2 = x[1] - x0;
  dy2 = y[1] - y0;
  a2 = 0.5 * fabs(dx1 * dy2 - dx1 * dx2);

  val0 = MRIgetVoxVal(mri_vals, f->v[0], 0, 0, 0);
  val1 = MRIgetVoxVal(mri_vals, f->v[1], 0, 0, 0);
  val2 = MRIgetVoxVal(mri_vals, f->v[2], 0, 0, 0);

  a0 /= (a0 + a1 + a2);
  a1 /= (a0 + a1 + a2);
  a2 /= (a0 + a1 + a2);
  val = a0 * val0 + a1 + val1 + a2 * val2;
  return (val);
}

