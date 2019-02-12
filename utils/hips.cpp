#include <iostream>
#include "hips.h"

int hips_cplxtor = 0;
int hips_rtocplx = 0;

#define HIPS_DEP_ERROR { std::cerr << "error: hips is not supported" << std::endl; return 0; }

double h_entropy(int *table,int count,int pairflag) HIPS_DEP_ERROR
int alloc_histo(struct hips_histo *histo,union pixelval *min,union pixelval *max,int nbins,int format) HIPS_DEP_ERROR
int alloc_histobins(struct hips_histo *histo) HIPS_DEP_ERROR
int clearparam(struct header *hd, const char *name) HIPS_DEP_ERROR
int fread_header(FILE *fp,struct header *hd,const char *fname) HIPS_DEP_ERROR
int fread_image(FILE *fp,struct header *hd,int fr,const char *fname) HIPS_DEP_ERROR
int free_hdrcon(struct header *hd) HIPS_DEP_ERROR
int free_header(struct header *hd) HIPS_DEP_ERROR
int fwrite_header(FILE *fp,struct header *hd,const char *fname) HIPS_DEP_ERROR
int fwrite_image(FILE *fp,struct header *hd,int fr,const char *fname) HIPS_DEP_ERROR
int getparam(struct header *hda,...) HIPS_DEP_ERROR
int h_add(struct header *hdi1,struct header *hdi2,struct header *hdo) HIPS_DEP_ERROR
int h_clearhisto(struct hips_histo *histogram) HIPS_DEP_ERROR
int h_copy(struct header *hdi,struct header *hdo) HIPS_DEP_ERROR
int h_diff(struct header *hdi1,struct header *hdi2,struct header *hdo) HIPS_DEP_ERROR
int h_div(struct header *hdi1,struct header *hdi2,struct header *hdo) HIPS_DEP_ERROR
int h_divscale(struct header *hdi,struct header *hdo,union pixelval *b) HIPS_DEP_ERROR
int h_enlarge(struct header *hdi,struct header *hdo,int xf,int yf) HIPS_DEP_ERROR
int h_entropycnt(struct header *hd,int *table,int pairflag) HIPS_DEP_ERROR
int h_flipquad(struct header *hdi,struct header *hdo) HIPS_DEP_ERROR
int h_fourtr(struct header *hd) HIPS_DEP_ERROR
int h_histo(struct header *hd,struct hips_histo *histogram,int nzflag,int *count) HIPS_DEP_ERROR
int h_histoeq(struct hips_histo *histogram,int count,unsigned char *map) HIPS_DEP_ERROR
int h_invert(struct header *hdi,struct header *hdo) HIPS_DEP_ERROR
int h_invfourtr(struct header *hd) HIPS_DEP_ERROR
int h_linscale(struct header *hdi,struct header *hdo,float b,float c) HIPS_DEP_ERROR
int h_median(struct header *hdi,struct header *hdo,int size) HIPS_DEP_ERROR
int h_minmax(struct header *hd,union pixelval *minval,union pixelval *maxval,int nzflag) HIPS_DEP_ERROR
int h_morphdil(struct header *hdi,struct header *hde,struct header *hdo,int centerr,int centerc,int gray) HIPS_DEP_ERROR
int h_mul(struct header *hdi1,struct header *hdi2,struct header *hdo) HIPS_DEP_ERROR
int h_mulscale(struct header *hdi,struct header *hdo,union pixelval *b) HIPS_DEP_ERROR
int h_pixmap(struct header *hdi,struct header *hdo,unsigned char *map) HIPS_DEP_ERROR
int h_reduce(struct header *hdi,struct header *hdo,int xf,int yf) HIPS_DEP_ERROR
int h_softthresh(struct header *hdi,struct header *hdo,union pixelval *thresh) HIPS_DEP_ERROR
int h_tob(struct header *hdi,struct header *hdo) HIPS_DEP_ERROR
int h_toc(struct header *hdi,struct header *hdo) HIPS_DEP_ERROR
int h_tod(struct header *hdi,struct header *hdo) HIPS_DEP_ERROR
int h_todc(struct header *hdi,struct header *hdo) HIPS_DEP_ERROR
int h_tof(struct header *hdi,struct header *hdo) HIPS_DEP_ERROR
int h_toi(struct header *hdi,struct header *hdo) HIPS_DEP_ERROR
int init_header(struct header *hd,const char *onm, const char *snm,int nfr, const char *odt,int rw,int cl,int pfmt,int nc,const char *desc) HIPS_DEP_ERROR
int setparam(struct header *hda,...) HIPS_DEP_ERROR
int update_header(struct header *hd,int argc,char **argv ) HIPS_DEP_ERROR
struct extpar *findparam(struct header *hd, const char *name) HIPS_DEP_ERROR
