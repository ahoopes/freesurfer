#ifndef HIPS_H
#define HIPS_H

// ------ typedefs ------

typedef unsigned char byte;
typedef char sbyte;
typedef unsigned short h_ushort;
typedef unsigned int h_uint;
typedef float h_complex[2];
typedef double h_dblcom[2];
typedef unsigned long fs_hsize_t;
typedef const char *  Filename;
typedef int h_boolean;

// ------ macros ------

#define MSBF_PACKING 1 /* use most significant bit first packing */
#define LSBF_PACKING 2 /* use least significant bit first packing */

#define CPLX_MAG 1 /* complex magnitude */
#define CPLX_PHASE 2 /* complex phase */
#define CPLX_REAL 3 /* complex - real part only */
#define CPLX_IMAG 4 /* complex - imaginary part only */

#define CPLX_RVI0 1 /* real part = value, imaginary = 0 */
#define CPLX_R0IV 2 /* real part = 0, imaginary = value */

#define PFBYTE  0 /* Bytes interpreted as unsigned integers */
#define PFSHORT  1 /* Short integers (2 bytes) */
#define PFINT  2 /* Integers (4 bytes) */
#define PFFLOAT  3 /* Float's (4 bytes)*/
#define PFCOMPLEX  4 /* 2 Float's interpreted as (real,imaginary) */
#define PFDBLCOM  7 /* Double complex's (2 Double's) */
#define PFDOUBLE  6 /* Double's (8 byte floats) */
#define PFMSBF  30 /* packed, most-significant-bit first */
#define PFLSBF  31 /* packed, least-significant-bit first */
#define PFRGB  35 /* RGB RGB RGB bytes */

#define HIPS_ERROR -1 /* error-return from hips routines */
#define HIPS_OK  0 /* one possible nonerror-return value */

#define FBUFLIMIT 30000  /* increase this if you use large PLOT3D files */
#define LINELENGTH 200  /* max characters per line in header vars */
#define NULLPAR ((struct extpar *) 0)

// ------ math macros ------

#ifndef MAX
# define MAX(A,B)  ((A) > (B) ? (A) : (B))
#endif
#ifndef MIN
# define MIN(A,B)  ((A) < (B) ? (A) : (B))
#endif
#ifndef ABS
# define ABS(A)    ((A) > 0 ? (A) : (-(A)))
#endif
#ifndef BETWEEN
# define BETWEEN(A,B,C) (((A) < (B)) ? (B) : (((A) > (C)) ? (C) : (A)))
#endif
#ifndef SIGN
# define SIGN(A,B) (((B) > 0) ? (A) : (-(A)))
#endif
#ifndef TOascii
# define TOascii(c) ((c) & 0x7f)
#endif

// ------ structs ------

union pixelval {
  byte v_byte;
  sbyte v_sbyte;
  short v_short;
  h_ushort v_ushort;
  int v_int;
  h_uint v_uint;
  float v_float;
  double v_double;
  h_complex v_complex;
  h_dblcom v_dblcom;
};

typedef union pixelval Pixelval;

struct header {
  char *orig_name; /* The originator of this sequence */
  h_boolean ondealloc; /* If nonzero, free orig_name when requested */
  char *seq_name; /* The name of this sequence */
  h_boolean sndealloc; /* If nonzero, free seq_name when requested */
  int num_frame; /* The number of frames in this sequence */
  char *orig_date; /* The date the sequence was originated */
  h_boolean oddealloc; /* If nonzero, free orig_date when requested */
  int orows;  /* The number of rows in each stored image */
  int ocols;  /* The number of columns in each stored image */
  int rows;  /* The number of rows in each image (ROI) */
  int cols;  /* The number of columns in each image (ROI) */
  int frow;  /* The first ROI row */
  int fcol;  /* The first ROI col */
  int pixel_format; /* The format of each pixel */
  int numcolor; /* The number of color frames per image */
  int numpix;  /* The number of pixels per stored frame */
  fs_hsize_t sizepix; /* The number of bytes per pixel */
  fs_hsize_t sizeimage; /* The number of bytes per stored frame */
  byte *image;  /* The image itself */
  h_boolean imdealloc; /* if nonzero, free image when requested */
  byte *firstpix; /* Pointer to first pixel (for ROI) */
  int sizehist; /* Number of bytes in history (excluding null, including <newline>) */
  char *seq_history; /* The sequence's history of transformations */
  h_boolean histdealloc; /* If nonzero, free history when requested */
  int sizedesc; /* Number of bytes in description (excluding null, including <newline>) */
  char *seq_desc; /* Descriptive information */
  h_boolean seqddealloc; /* If nonzero, free desc when requested */
  int numparam; /* Count of additional parameters */
  h_boolean paramdealloc; /* If nonzero, free param structures and/or param values when requested */
  struct extpar *params; /* Additional parameters */
  float xsize ;
  float ysize ;
};

struct hips_roi {
  int rows;  /* The number of rows in the ROI */
  int cols;  /* The number of columns in the ROI */
  int frow;  /* The first ROI row */
  int fcol;  /* The first ROI col */
};

struct extpar {
  char *name;  /* name of this variable */
  int format;  /* format of values (PFBYTE, PFINT, etc.) */
  int count;  /* number of values */
  union {
    byte v_b; /* PFBYTE/PFASCII, count = 1 */
    int v_i; /* PFINT, count = 1 */
    short v_s; /* PFSHORT, count = 1 */
    float v_f; /* PFFLOAT, count = 1 */
    byte *v_pb; /* PFBYT/PFASCIIE, count > 1 */
    int *v_pi; /* PFINT, count > 1 */
    short *v_ps; /* PFSHORT, count > 1 */
    float *v_pf; /* PFFLOAT, count > 1 */
  } val;
  h_boolean dealloc; /* if nonzero, free memory for val */
  struct extpar *nextp; /* next parameter in list */
};

struct hips_histo {
  int nbins;
  int *histo;
  fs_hsize_t sizehist;
  h_boolean histodealloc;
  int pixel_format;
  Pixelval minbin;
  Pixelval binwidth;
};

// ------ functions ------

double h_entropy(int *table,int count,int pairflag);
int alloc_histo(struct hips_histo *histo,union pixelval *min,union pixelval *max,int nbins,int format);
int alloc_histobins(struct hips_histo *histo);
int clearparam(struct header *hd,char *name);
int fread_header(FILE *fp,struct header *hd,const char *fname);
int fread_image(FILE *fp,struct header *hd,int fr,const char *fname);
int free_hdrcon(struct header *hd);
int free_header(struct header *hd);
int fwrite_header(FILE *fp,struct header *hd,const char *fname);
int fwrite_image(FILE *fp,struct header *hd,int fr,const char *fname);
int getparam(struct header *hda,...);
int h_add(struct header *hdi1,struct header *hdi2,struct header *hdo);
int h_clearhisto(struct hips_histo *histogram);
int h_copy(struct header *hdi,struct header *hdo);
int h_diff(struct header *hdi1,struct header *hdi2,struct header *hdo);
int h_div(struct header *hdi1,struct header *hdi2,struct header *hdo);
int h_divscale(struct header *hdi,struct header *hdo,union pixelval *b);
int h_enlarge(struct header *hdi,struct header *hdo,int xf,int yf);
int h_entropycnt(struct header *hd,int *table,int pairflag);
int h_flipquad(struct header *hdi,struct header *hdo);
int h_fourtr(struct header *hd);
int h_histo(struct header *hd,struct hips_histo *histogram,int nzflag,int *count);
int h_histoeq(struct hips_histo *histogram,int count,unsigned char *map);
int h_invert(struct header *hdi,struct header *hdo);
int h_invfourtr(struct header *hd);
int h_linscale(struct header *hdi,struct header *hdo,float b,float c);
int h_median(struct header *hdi,struct header *hdo,int size);
int h_minmax(struct header *hd,union pixelval *minval,union pixelval *maxval,int nzflag);
int h_morphdil(struct header *hdi,struct header *hde,struct header *hdo,int centerr,int centerc,int gray);
int h_mul(struct header *hdi1,struct header *hdi2,struct header *hdo);
int h_mulscale(struct header *hdi,struct header *hdo,union pixelval *b);
int h_pixmap(struct header *hdi,struct header *hdo,unsigned char *map);
int h_reduce(struct header *hdi,struct header *hdo,int xf,int yf);
int h_softthresh(struct header *hdi,struct header *hdo,union pixelval *thresh);
int h_tob(struct header *hdi,struct header *hdo);
int h_toc(struct header *hdi,struct header *hdo);
int h_tod(struct header *hdi,struct header *hdo);
int h_todc(struct header *hdi,struct header *hdo);
int h_tof(struct header *hdi,struct header *hdo);
int h_toi(struct header *hdi,struct header *hdo);
int init_header(struct header *hd,char *onm,char *snm,int nfr,char *odt,int rw,int cl,int pfmt,int nc,char *desc);
int setparam(struct header *hda,...);
int update_header(struct header *hd,int argc,char **argv );
struct extpar *findparam(struct header *hd,char *name);

// ------ externs ------

extern int hips_rtocplx;
extern int hips_cplxtor;

#endif
