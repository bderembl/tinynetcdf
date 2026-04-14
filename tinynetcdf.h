#ifndef TINYNETCDF_H
#define TINYNETCDF_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

/* -----------------------------------------------------------------------
 * NetCDF-3 type codes
 * --------------------------------------------------------------------- */
typedef enum {
    NC_BYTE   = 1,
    NC_CHAR   = 2,
    NC_SHORT  = 3,
    NC_INT    = 4,
    NC_FLOAT  = 5,
    NC_DOUBLE = 6
} nc_type_t;

/* -----------------------------------------------------------------------
 * Limits
 * --------------------------------------------------------------------- */
#define NCF_MAX_NAME    256
#define NCF_MAX_DIMS    32
#define NCF_MAX_VARS    512
#define NCF_MAX_ATTS    128

/* -----------------------------------------------------------------------
 * Attribute
 * --------------------------------------------------------------------- */
typedef struct {
    char      name[NCF_MAX_NAME];
    nc_type_t type;
    size_t    len;       /* number of elements */
    void     *values;    /* heap-allocated */
} ncf_att_t;

/* -----------------------------------------------------------------------
 * Dimension
 * --------------------------------------------------------------------- */
typedef struct {
    char     name[NCF_MAX_NAME];
    int32_t  length;     /* 0 = unlimited (record dimension) */
} ncf_dim_t;

/* -----------------------------------------------------------------------
 * Variable
 * --------------------------------------------------------------------- */
typedef struct {
    char      name[NCF_MAX_NAME];
    nc_type_t type;
    int       ndims;
    int       dimids[NCF_MAX_DIMS];  /* indices into file->dims */
    int       natts;
    ncf_att_t atts[NCF_MAX_ATTS];

    /* layout info (filled during write/read) */
    int32_t   vsize;     /* size of one record (or whole var if non-rec) */
    int64_t   begin;     /* byte offset of data in file */

    /* in-memory data pointer (owned by library on read, caller on write) */
    void     *data;
    size_t    data_size; /* bytes */
} ncf_var_t;

/* -----------------------------------------------------------------------
 * File object
 * --------------------------------------------------------------------- */
typedef struct {
    FILE      *fp;
    char       mode;          /* 'r', 'w', 'a' */
    int        version;       /* 1 = classic, 2 = 64-bit offset */

    int        ndims;
    ncf_dim_t  dims[NCF_MAX_DIMS];

    int        nvars;
    ncf_var_t  vars[NCF_MAX_VARS];

    int        ngatts;
    ncf_att_t  gatts[NCF_MAX_ATTS];

    int32_t    nrecs;         /* number of records written */
    int32_t    nrecs_at_open; /* nrecs when file was opened in 'a' mode */
    int        header_read;   /* 1 after header has been parsed (append mode) */
    int32_t    recsize;       /* bytes per record across all rec vars */
} ncf_file_t;

/* -----------------------------------------------------------------------
 * Return codes
 * --------------------------------------------------------------------- */
#define NCF_OK        0
#define NCF_ERR      -1

/* -----------------------------------------------------------------------
 * API
 * --------------------------------------------------------------------- */

/* Open / close */
ncf_file_t *ncf_open (const char *path, char mode);
int         ncf_close(ncf_file_t *f);

/* Dimensions */
int ncf_def_dim(ncf_file_t *f, const char *name, int32_t length);
int ncf_inq_dim(ncf_file_t *f, const char *name, int32_t *length);

/* Variables */
int ncf_def_var(ncf_file_t *f, const char *name, nc_type_t type,
                int ndims, const char **dimnames);
int ncf_put_var(ncf_file_t *f, const char *name, const void *data);
int ncf_get_var(ncf_file_t *f, const char *name, void **data, size_t *nbytes);

/* Record (unlimited dimension) API */
int ncf_put_rec  (ncf_file_t *f, const char *varname, int32_t rec,
                  const void *data);
int ncf_get_rec  (ncf_file_t *f, const char *varname, int32_t rec,
                  void **data, size_t *nbytes);
int ncf_inq_nrecs(ncf_file_t *f, int32_t *nrecs);

/* Attributes (varname=NULL for global) */
int ncf_put_att(ncf_file_t *f, const char *varname,
                const char *attname, nc_type_t type,
                size_t len, const void *values);
int ncf_get_att(ncf_file_t *f, const char *varname,
                const char *attname, nc_type_t *type,
                size_t *len, void **values);

/* Flush / sync */
int ncf_sync(ncf_file_t *f);

/* Helpers */
size_t      ncf_type_size(nc_type_t type);
const char *ncf_type_name(nc_type_t type);

#endif /* TINYNETCDF_H */
