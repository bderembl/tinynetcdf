/* This library was inspired by the fantastic scipy.io.netcdf library */  

#include "tinynetcdf.h"

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>

/* -----------------------------------------------------------------------
 * NetCDF-3 binary constants
 * --------------------------------------------------------------------- */
static const uint8_t MAGIC[3]    = {'C','D','F'};
static const uint8_t ABSENT[8]   = {0,0,0,0,0,0,0,0};
static const uint8_t TAG_DIM[4]  = {0,0,0,10};
static const uint8_t TAG_VAR[4]  = {0,0,0,11};
static const uint8_t TAG_ATT[4]  = {0,0,0,12};
static const uint8_t TAG_ZERO[4] = {0,0,0,0};

/* -----------------------------------------------------------------------
 * Endian helpers
 * --------------------------------------------------------------------- */
static int is_little_endian(void) {
    uint16_t x = 1;
    return *(uint8_t *)&x;
}
static uint16_t swap16(uint16_t v) {
    return (uint16_t)((v >> 8) | (v << 8));
}
static uint32_t swap32(uint32_t v) {
    return ((v & 0xFF000000u) >> 24) | ((v & 0x00FF0000u) >>  8) |
           ((v & 0x0000FF00u) <<  8) | ((v & 0x000000FFu) << 24);
}
static uint64_t swap64(uint64_t v) {
    return ((uint64_t)swap32((uint32_t)(v & 0xFFFFFFFFu)) << 32) |
            (uint64_t)swap32((uint32_t)(v >> 32));
}
static void to_be(void *buf, size_t n, size_t size) {
    if (!is_little_endian()) return;
    uint8_t *p = (uint8_t *)buf;
    for (size_t i = 0; i < n; i++, p += size) {
        if (size == 2) { uint16_t v; memcpy(&v,p,2); v=swap16(v); memcpy(p,&v,2); }
        if (size == 4) { uint32_t v; memcpy(&v,p,4); v=swap32(v); memcpy(p,&v,4); }
        if (size == 8) { uint64_t v; memcpy(&v,p,8); v=swap64(v); memcpy(p,&v,8); }
    }
}
static void from_be(void *buf, size_t n, size_t size) { to_be(buf, n, size); }

static int write_be(ncf_file_t *f, const void *data, size_t nelems, size_t typesize) {
    size_t sz = nelems * typesize;
    void *tmp = malloc(sz);
    if (!tmp) return NCF_ERR;
    memcpy(tmp, data, sz);
    to_be(tmp, nelems, typesize);
    fwrite(tmp, 1, sz, f->fp);
    free(tmp);
    return NCF_OK;
}

/* -----------------------------------------------------------------------
 * Low-level pack / unpack
 * --------------------------------------------------------------------- */
static int pack_int32(ncf_file_t *f, int32_t v) {
    uint32_t be; memcpy(&be, &v, 4); to_be(&be, 1, 4);
    return fwrite(&be, 4, 1, f->fp) == 1 ? NCF_OK : NCF_ERR;
}
static int unpack_int32(ncf_file_t *f, int32_t *v) {
    uint32_t be;
    if (fread(&be, 4, 1, f->fp) != 1) return NCF_ERR;
    from_be(&be, 1, 4); memcpy(v, &be, 4);
    return NCF_OK;
}
static int pack_int64(ncf_file_t *f, int64_t v) {
    uint64_t be; memcpy(&be, &v, 8); to_be(&be, 1, 8);
    return fwrite(&be, 8, 1, f->fp) == 1 ? NCF_OK : NCF_ERR;
}
static int unpack_int64(ncf_file_t *f, int64_t *v) {
    uint64_t be;
    if (fread(&be, 8, 1, f->fp) != 1) return NCF_ERR;
    from_be(&be, 1, 8); memcpy(v, &be, 8);
    return NCF_OK;
}
static int pack_begin(ncf_file_t *f, int64_t v) {
    return f->version == 2 ? pack_int64(f, v) : pack_int32(f, (int32_t)v);
}
static int unpack_begin(ncf_file_t *f, int64_t *v) {
    if (f->version == 2) return unpack_int64(f, v);
    int32_t v32; int r = unpack_int32(f, &v32); *v = v32; return r;
}
static int pack_string(ncf_file_t *f, const char *s) {
    int32_t len = (int32_t)strlen(s);
    if (pack_int32(f, len) != NCF_OK) return NCF_ERR;
    if (fwrite(s, 1, len, f->fp) != (size_t)len) return NCF_ERR;
    int pad = (-len) & 3;
    uint8_t zeros[3] = {0,0,0};
    if (pad && fwrite(zeros, 1, pad, f->fp) != (size_t)pad) return NCF_ERR;
    return NCF_OK;
}
static int unpack_string(ncf_file_t *f, char *buf, size_t bufsz) {
    int32_t len;
    if (unpack_int32(f, &len) != NCF_OK) return NCF_ERR;
    if ((size_t)len >= bufsz) return NCF_ERR;
    if (fread(buf, 1, len, f->fp) != (size_t)len) return NCF_ERR;
    buf[len] = '\0';
    int pad = (-len) & 3;
    if (pad) fseek(f->fp, pad, SEEK_CUR);
    return NCF_OK;
}

/* -----------------------------------------------------------------------
 * Helpers
 * --------------------------------------------------------------------- */
size_t ncf_type_size(nc_type_t type) {
    switch (type) {
        case NC_BYTE:   return 1;
        case NC_CHAR:   return 1;
        case NC_SHORT:  return 2;
        case NC_INT:    return 4;
        case NC_FLOAT:  return 4;
        case NC_DOUBLE: return 8;
        default:        return 0;
    }
}
const char *ncf_type_name(nc_type_t type) {
    switch (type) {
        case NC_BYTE:   return "byte";
        case NC_CHAR:   return "char";
        case NC_SHORT:  return "short";
        case NC_INT:    return "int";
        case NC_FLOAT:  return "float";
        case NC_DOUBLE: return "double";
        default:        return "unknown";
    }
}

static uint8_t type_tag(nc_type_t t) { return (uint8_t)t; }

static int find_dim(ncf_file_t *f, const char *name) {
    for (int i = 0; i < f->ndims; i++)
        if (strcmp(f->dims[i].name, name) == 0) return i;
    return -1;
}
static int find_var(ncf_file_t *f, const char *name) {
    for (int i = 0; i < f->nvars; i++)
        if (strcmp(f->vars[i].name, name) == 0) return i;
    return -1;
}
static ncf_att_t *find_att(ncf_att_t *atts, int natts, const char *name) {
    for (int i = 0; i < natts; i++)
        if (strcmp(atts[i].name, name) == 0) return &atts[i];
    return NULL;
}

/* -----------------------------------------------------------------------
 * Open / close
 * --------------------------------------------------------------------- */
ncf_file_t *ncf_open(const char *path, char mode) {
    ncf_file_t *f = (ncf_file_t *)calloc(1, sizeof(ncf_file_t));
    if (!f) return NULL;
    const char *fmode = (mode == 'w') ? "wb" :
                        (mode == 'a') ? "r+b" : "rb";
    f->fp = fopen(path, fmode);
    if (!f->fp) { free(f); return NULL; }
    f->mode    = mode;
    f->version = 2; // 2 by default
    if (mode == 'r' || mode == 'a') {
      if (ncf_sync(f) != NCF_OK) { fclose(f->fp); free(f); return NULL; }
      if (mode == 'a') {
        f->nrecs_at_open = f->nrecs;
        f->header_read   = 1;
      }
//      if (mode == 'a') f->nrecs_at_open = f->nrecs;
    }
    return f;
}

static void free_atts(ncf_att_t *atts, int natts) {
    for (int i = 0; i < natts; i++) free(atts[i].values);
}


int ncf_close(ncf_file_t *f) {
    if (!f) return NCF_ERR;
    int rc = NCF_OK;
    if (f->mode == 'w' || f->mode == 'a') rc = ncf_sync(f);
    free_atts(f->gatts, f->ngatts);
    for (int i = 0; i < f->nvars; i++) {
        free_atts(f->vars[i].atts, f->vars[i].natts);
        free(f->vars[i].data);
    }
    fclose(f->fp);
    free(f);
    return rc;
}

/* -----------------------------------------------------------------------
 * Define dimensions / variables / attributes
 * --------------------------------------------------------------------- */
int ncf_def_dim(ncf_file_t *f, const char *name, int32_t length) {
    if (f->ndims >= NCF_MAX_DIMS) return NCF_ERR;
    if (length == 0) {
        for (int i = 0; i < f->ndims; i++)
            if (f->dims[i].length == 0) return NCF_ERR; /* already have one */
        /* if (f->ndims != 0) return NCF_ERR; /\* must be first *\/ */
    }
    ncf_dim_t *d = &f->dims[f->ndims++];
    strncpy(d->name, name, NCF_MAX_NAME - 1);
    d->length = length;
    return NCF_OK;
}

int ncf_inq_dim(ncf_file_t *f, const char *name, int32_t *length) {
    int id = find_dim(f, name);
    if (id < 0) return NCF_ERR;
    *length = f->dims[id].length;
    return NCF_OK;
}

int ncf_def_var(ncf_file_t *f, const char *name, nc_type_t type,
                int ndims, const char **dimnames) {
    if (f->nvars >= NCF_MAX_VARS) return NCF_ERR;
    ncf_var_t *v = &f->vars[f->nvars++];
    memset(v, 0, sizeof(*v));
    strncpy(v->name, name, NCF_MAX_NAME - 1);
    v->type  = type;
    v->ndims = ndims;
    for (int i = 0; i < ndims; i++) {
        int id = find_dim(f, dimnames[i]);
        if (id < 0) return NCF_ERR;
        v->dimids[i] = id;
    }
    return NCF_OK;
}

int ncf_put_var(ncf_file_t *f, const char *name, const void *data) {
    int id = find_var(f, name);
    if (id < 0) return NCF_ERR;
    ncf_var_t *v = &f->vars[id];

    /* compute size from dims */
    size_t nelems = 1;
    for (int i = 0; i < v->ndims; i++)
        nelems *= (size_t)f->dims[v->dimids[i]].length;
    size_t nbytes = nelems * ncf_type_size(v->type);

    free(v->data);
    v->data = malloc(nbytes);
    if (!v->data) return NCF_ERR;
    memcpy(v->data, data, nbytes);
    v->data_size = nbytes;
    return NCF_OK;
}

int ncf_get_var(ncf_file_t *f, const char *name, void **data, size_t *nbytes) {
    int id = find_var(f, name);
    if (id < 0) return NCF_ERR;
    *data   = f->vars[id].data;
    *nbytes = f->vars[id].data_size;
    return NCF_OK;
}

/* -----------------------------------------------------------------------
 * Record variable helpers
 * --------------------------------------------------------------------- */
static int var_is_rec(ncf_file_t *f, ncf_var_t *v) {
    return v->ndims > 0 && f->dims[v->dimids[0]].length == 0;
}

static size_t var_rec_slice_bytes(ncf_file_t *f, ncf_var_t *v) {
    size_t sz = ncf_type_size(v->type);
    for (int i = 1; i < v->ndims; i++)
        sz *= (size_t)f->dims[v->dimids[i]].length;
    return sz;
}

static int32_t compute_recsize(ncf_file_t *f) {
    int32_t rs = 0;
    for (int vi = 0; vi < f->nvars; vi++) {
        ncf_var_t *v = &f->vars[vi];
        if (!var_is_rec(f, v)) continue;
        size_t sz = var_rec_slice_bytes(f, v);
        rs += (int32_t)((sz + 3) & ~(size_t)3);
    }
    return rs;
}

static int32_t var_vsize(ncf_file_t *f, ncf_var_t *v) {
    size_t sz = ncf_type_size(v->type);
    for (int i = (var_is_rec(f, v) ? 1 : 0); i < v->ndims; i++)
        sz *= (size_t)f->dims[v->dimids[i]].length;
    sz = (sz + 3) & ~(size_t)3;
    return (int32_t)sz;
}

int ncf_inq_nrecs(ncf_file_t *f, int32_t *nrecs) {
    if (!f || !nrecs) return NCF_ERR;
    *nrecs = f->nrecs;
    return NCF_OK;
}

int ncf_put_rec(ncf_file_t *f, const char *varname, int32_t rec,
                const void *data) {
    int id = find_var(f, varname);
    if (id < 0) return NCF_ERR;
    ncf_var_t *v = &f->vars[id];
    if (!var_is_rec(f, v)) return NCF_ERR;

    size_t slice = var_rec_slice_bytes(f, v);

    size_t needed = (size_t)(rec + 1) * slice;
    if (needed > v->data_size) {
        void *nb = realloc(v->data, needed);
        if (!nb) return NCF_ERR;
        memset((uint8_t *)nb + v->data_size, 0, needed - v->data_size);
        v->data      = nb;
        v->data_size = needed;
    }
    memcpy((uint8_t *)v->data + (size_t)rec * slice, data, slice);
    if (rec + 1 > f->nrecs) f->nrecs = rec + 1;
    return NCF_OK;
}

int ncf_get_rec(ncf_file_t *f, const char *varname, int32_t rec,
                void **data, size_t *nbytes) {
    int id = find_var(f, varname);
    if (id < 0) return NCF_ERR;
    ncf_var_t *v = &f->vars[id];
    if (!var_is_rec(f, v)) return NCF_ERR;
    if (rec < 0 || rec >= f->nrecs) return NCF_ERR;

    size_t slice = var_rec_slice_bytes(f, v);
    *data   = (uint8_t *)v->data + (size_t)rec * slice;
    *nbytes = slice;
    return NCF_OK;
}

/* -----------------------------------------------------------------------
 * Attributes
 * --------------------------------------------------------------------- */
static int put_att_impl(ncf_att_t *atts, int *natts, int maxatts,
                        const char *attname, nc_type_t type,
                        size_t len, const void *values) {
    ncf_att_t *a = find_att(atts, *natts, attname);
    if (!a) {
        if (*natts >= maxatts) return NCF_ERR;
        a = &atts[(*natts)++];
        strncpy(a->name, attname, NCF_MAX_NAME - 1);
    } else {
        free(a->values);
    }
    a->type = type; a->len = len;
    size_t sz = len * ncf_type_size(type);
    a->values = malloc(sz);
    if (!a->values) return NCF_ERR;
    memcpy(a->values, values, sz);
    return NCF_OK;
}

int ncf_put_att(ncf_file_t *f, const char *varname,
                const char *attname, nc_type_t type,
                size_t len, const void *values) {
    if (!varname)
        return put_att_impl(f->gatts, &f->ngatts, NCF_MAX_ATTS,
                            attname, type, len, values);
    int id = find_var(f, varname);
    if (id < 0) return NCF_ERR;
    return put_att_impl(f->vars[id].atts, &f->vars[id].natts, NCF_MAX_ATTS,
                        attname, type, len, values);
}

int ncf_get_att(ncf_file_t *f, const char *varname,
                const char *attname, nc_type_t *type,
                size_t *len, void **values) {
    ncf_att_t *a = NULL;
    if (!varname) {
        a = find_att(f->gatts, f->ngatts, attname);
    } else {
        int id = find_var(f, varname);
        if (id < 0) return NCF_ERR;
        a = find_att(f->vars[id].atts, f->vars[id].natts, attname);
    }
    if (!a) return NCF_ERR;
    if (type)   *type   = a->type;
    if (len)    *len    = a->len;
    if (values) *values = a->values;
    return NCF_OK;
}

/* -----------------------------------------------------------------------
 * Read/Write attribute array
 * --------------------------------------------------------------------- */
static int write_att_array(ncf_file_t *f, ncf_att_t *atts, int natts) {
    if (natts == 0) { fwrite(ABSENT, 1, 8, f->fp); return NCF_OK; }
    fwrite(TAG_ATT, 1, 4, f->fp);
    pack_int32(f, natts);
    for (int i = 0; i < natts; i++) {
        ncf_att_t *a = &atts[i];
        pack_string(f, a->name);
        uint8_t tag[4] = {0,0,0, type_tag(a->type)};
        fwrite(tag, 1, 4, f->fp);
        pack_int32(f, (int32_t)a->len);
        size_t sz = a->len * ncf_type_size(a->type);
        write_be(f, a->values, a->len, ncf_type_size(a->type));
        int pad = (-(int)sz) & 3;
        uint8_t zeros[3] = {0,0,0};
        if (pad) fwrite(zeros, 1, pad, f->fp);
    }
    return NCF_OK;
}

static int read_att_array(ncf_file_t *f, ncf_att_t *atts, int *natts) {
    uint8_t tag[4];
    if (fread(tag, 1, 4, f->fp) != 4) return NCF_ERR;
    if (memcmp(tag, TAG_ATT, 4) != 0) {
      /* ABSENT or unknown tag — consume the count word and return empty */
      int32_t n; unpack_int32(f, &n);
      *natts = 0;
      return memcmp(tag, TAG_ZERO, 4) == 0 ? NCF_OK : NCF_ERR;
    }
    int32_t n; unpack_int32(f, &n);
    *natts = n;
    for (int i = 0; i < n; i++) {
        unpack_string(f, atts[i].name, NCF_MAX_NAME);
        uint8_t ttag[4]; fread(ttag, 1, 4, f->fp);
        atts[i].type = (nc_type_t)ttag[3];
        int32_t len; unpack_int32(f, &len);
        atts[i].len = len;
        size_t sz = len * ncf_type_size(atts[i].type);
        atts[i].values = malloc(sz + 1);
        fread(atts[i].values, 1, sz, f->fp);
        from_be(atts[i].values, len, ncf_type_size(atts[i].type));
        int pad = (-(int)sz) & 3;
        if (pad) fseek(f->fp, pad, SEEK_CUR);
    }
    return NCF_OK;
}

static void write_rec_slices(ncf_file_t *f, int32_t from_rec, int32_t to_rec) {
    for (int vi = 0; vi < f->nvars; vi++) {
        ncf_var_t *v = &f->vars[vi];
        if (!var_is_rec(f, v) || !v->data || v->data_size == 0) continue;
        size_t slice    = var_rec_slice_bytes(f, v);
        size_t typesize = ncf_type_size(v->type);
        for (int32_t r = from_rec; r < to_rec; r++) {
          long pos = (long)v->begin + (long)r * f->recsize;
          fseek(f->fp, pos, SEEK_SET);
          write_be(f, (uint8_t *)v->data + (size_t)r * slice,
                   slice / typesize, typesize);
        }
    }
}

/* -----------------------------------------------------------------------
 * ncf_sync
 * --------------------------------------------------------------------- */
int ncf_sync(ncf_file_t *f) {

    /* ================================================================
     * READ PATH
     * ============================================================== */
    if (f->mode == 'r' || (f->mode == 'a' && !f->header_read)) {
        rewind(f->fp);

        uint8_t magic[3];
        if (fread(magic, 1, 3, f->fp) != 3) return NCF_ERR;
        if (memcmp(magic, MAGIC, 3) != 0) return NCF_ERR;
        uint8_t ver;
        if (fread(&ver, 1, 1, f->fp) != 1) return NCF_ERR;
        f->version = ver;

        if (unpack_int32(f, &f->nrecs) != NCF_OK) return NCF_ERR;
        /* dim_array */
        uint8_t tag[4];
        if (fread(tag, 1, 4, f->fp) != 4) return NCF_ERR;
        if (memcmp(tag, TAG_ZERO, 4) != 0 && memcmp(tag, TAG_DIM, 4) != 0)
            return NCF_ERR;
        int32_t ndims; unpack_int32(f, &ndims);
        f->ndims = ndims;
        for (int i = 0; i < ndims; i++) {
            unpack_string(f, f->dims[i].name, NCF_MAX_NAME);
            int32_t len; unpack_int32(f, &len);
            f->dims[i].length = len; /* 0 stays 0 = unlimited */
        }

        /* gatt_array */
        if (read_att_array(f, f->gatts, &f->ngatts) != NCF_OK) return NCF_ERR;

        /* var_array */
        if (fread(tag, 1, 4, f->fp) != 4) return NCF_ERR;
        if (memcmp(tag, TAG_ZERO, 4) != 0 && memcmp(tag, TAG_VAR, 4) != 0)
            return NCF_ERR;
        int32_t nvars; unpack_int32(f, &nvars);
        f->nvars = nvars;
        for (int vi = 0; vi < nvars; vi++) {
            ncf_var_t *v = &f->vars[vi];
            unpack_string(f, v->name, NCF_MAX_NAME);
            int32_t nd; unpack_int32(f, &nd);
            v->ndims = nd;
            for (int i = 0; i < nd; i++) {
                int32_t did; unpack_int32(f, &did);
                v->dimids[i] = did;
            }
            if (read_att_array(f, v->atts, &v->natts) != NCF_OK) return NCF_ERR;
            uint8_t ttag[4]; fread(ttag, 1, 4, f->fp);
            v->type = (nc_type_t)ttag[3];
            int32_t vsize; unpack_int32(f, &vsize);
            v->vsize = vsize;
            unpack_begin(f, &v->begin);
        }

        /* Compute recsize */
        f->recsize = 0;
        for (int vi = 0; vi < f->nvars; vi++)
            if (var_is_rec(f, &f->vars[vi]))
                f->recsize += f->vars[vi].vsize;

        /* Read variable data */
        for (int vi = 0; vi < f->nvars; vi++) {
            ncf_var_t *v = &f->vars[vi];
            if (!var_is_rec(f, v)) {
                /* Non-record: contiguous */
                size_t nelems = 1;
                for (int i = 0; i < v->ndims; i++)
                    nelems *= (size_t)f->dims[v->dimids[i]].length;
                size_t sz = nelems * ncf_type_size(v->type);
                v->data_size = sz;
                v->data = malloc(sz + 1);
                fseek(f->fp, (long)v->begin, SEEK_SET);
                fread(v->data, 1, sz, f->fp);
                from_be(v->data, nelems, ncf_type_size(v->type));
            } else {
                /* Record variable: de-interleave */
                size_t slice    = var_rec_slice_bytes(f, v);
                size_t typesize = ncf_type_size(v->type);
                size_t nelems_s = slice / typesize;
                size_t total_sz = slice * (size_t)f->nrecs;
                v->data_size = total_sz;
                v->data = malloc(total_sz + 1);
                for (int32_t r = 0; r < f->nrecs; r++) {
                    long pos = (long)v->begin + (long)r * f->recsize;
                    fseek(f->fp, pos, SEEK_SET);
                    fread((uint8_t *)v->data + (size_t)r * slice, 1, slice, f->fp);
                    from_be((uint8_t *)v->data + (size_t)r * slice,
                            nelems_s, typesize);
                }
            }
        }
        return NCF_OK;
    }

    /* ====
     * APPEND PATH — patch numrecs + write only new record slices
     * ==== */
    if (f->mode == 'a' && f->header_read) {
        if (f->nrecs <= f->nrecs_at_open) {
            fflush(f->fp);
            return NCF_OK; /* nothing new to write */
        }
        /* Write new record slices at their interleaved positions */
        write_rec_slices(f, f->nrecs_at_open, f->nrecs);

        /* Patch numrecs at offset 4 */
        fseek(f->fp, 4L, SEEK_SET);
        pack_int32(f, f->nrecs);
        fflush(f->fp);
        f->nrecs_at_open = f->nrecs; /* advance watermark */
        return NCF_OK;
    }

    /* ================================================================
     * WRITE PATH
     * ============================================================== */
    rewind(f->fp);

    fwrite(MAGIC, 1, 3, f->fp);
    uint8_t ver = (uint8_t)f->version;
    fwrite(&ver, 1, 1, f->fp);

    /* numrecs placeholder — backfilled after data */
    long numrecs_pos = ftell(f->fp);
    pack_int32(f, f->nrecs);

    /* dim_array */
    if (f->ndims == 0) {
        fwrite(ABSENT, 1, 8, f->fp);
    } else {
        fwrite(TAG_DIM, 1, 4, f->fp);
        pack_int32(f, f->ndims);
        for (int i = 0; i < f->ndims; i++) {
            pack_string(f, f->dims[i].name);
            pack_int32(f, f->dims[i].length);
        }
    }

    /* gatt_array */
    write_att_array(f, f->gatts, f->ngatts);

    /* var_array */
    if (f->nvars == 0) {
        fwrite(ABSENT, 1, 8, f->fp);
    } else {
        fwrite(TAG_VAR, 1, 4, f->fp);
        pack_int32(f, f->nvars);

        long *begin_pos = (long *)calloc(f->nvars, sizeof(long));

        for (int vi = 0; vi < f->nvars; vi++) {
            ncf_var_t *v = &f->vars[vi];
            pack_string(f, v->name);
            pack_int32(f, v->ndims);
            for (int i = 0; i < v->ndims; i++)
                pack_int32(f, v->dimids[i]);
            write_att_array(f, v->atts, v->natts);
            uint8_t ttag[4] = {0,0,0, type_tag(v->type)};
            fwrite(ttag, 1, 4, f->fp);
            v->vsize = var_vsize(f, v);
            pack_int32(f, v->vsize);
            begin_pos[vi] = ftell(f->fp);
            pack_begin(f, 0); /* placeholder */
        }

        f->recsize = compute_recsize(f);

        /* Non-record variables: contiguous */
        for (int vi = 0; vi < f->nvars; vi++) {
            ncf_var_t *v = &f->vars[vi];
            if (var_is_rec(f, v)) continue;

            long data_start = ftell(f->fp);
            fseek(f->fp, begin_pos[vi], SEEK_SET);
            pack_begin(f, (int64_t)data_start);
            fseek(f->fp, data_start, SEEK_SET);

            if (!v->data || v->data_size == 0) continue;
            size_t nelems = v->data_size / ncf_type_size(v->type);
            write_be(f, v->data, nelems, ncf_type_size(v->type));

            int pad = (-(int)v->data_size) & 3;
            uint8_t zeros[3] = {0,0,0};
            if (pad) fwrite(zeros, 1, pad, f->fp);
        }

        /* Record variables: interleaved layout */
        if (f->nrecs > 0 && f->recsize > 0) {
            long rec_base = ftell(f->fp);

            /* Backfill begin for each rec var */
            int32_t var_offset = 0;
            for (int vi = 0; vi < f->nvars; vi++) {
                ncf_var_t *v = &f->vars[vi];
                if (!var_is_rec(f, v)) continue;
                fseek(f->fp, begin_pos[vi], SEEK_SET);
                pack_begin(f, (int64_t)(rec_base + var_offset));
                v->begin = (int64_t)(rec_base + var_offset);
                var_offset += v->vsize;
            }

            /* Pre-allocate record section with zeros */
            size_t total_rec_bytes = (size_t)f->nrecs * (size_t)f->recsize;
            fseek(f->fp, rec_base, SEEK_SET);
            {
              void *zeros = calloc(1, total_rec_bytes);
              fwrite(zeros, 1, total_rec_bytes, f->fp);
              free(zeros);
            }

            /* Write each (rec, var) slice at its interleaved position */
            write_rec_slices(f, 0, f->nrecs);

            fseek(f->fp, rec_base + (long)total_rec_bytes, SEEK_SET);
        }
        free(begin_pos);
    }

    /* Backfill numrecs */
    long end_pos = ftell(f->fp);
    fseek(f->fp, numrecs_pos, SEEK_SET);
    pack_int32(f, f->nrecs);
    fseek(f->fp, end_pos, SEEK_SET);

    fflush(f->fp);
    return NCF_OK;
}
